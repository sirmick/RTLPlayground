# 3. Architecture

```
            ISRs (common area)                         xdata
  INT0 link ─┐  INT1 rx ─┐  UART ─┐  T2 tick ─┐
             └── set __bit flags / ring bytes ┘
                                                  ┌──────────────────────┐
  sched_run (banked): kick the watchdog, pass     │ sched_task[24]       │
  ┌─────────────────────────────────────────┐     │ {state,mask,wake,arg}│
  │ sched_pass (common area, sched.c)       │◄───►│ sched_ev (8 bits)    │
  │ pass start: tick, hardware events       │     │ per-state max, runs  │
  │ for pos in run_seq:                     │     └──────────────────────┘
  │   slot = run_seq[pos]                   │
  │   if ready(slot): time it, dispatch,    │──► step_addr[state]: the 3-byte
  │     store the state the step returns    │    address of a banked step
  │ nothing ran: sched_idle (PCON)          │
  └─────────────────────────────────────────┘
```

Code: `sched.h` (the API), `sched.c` (resident), `sched_bank.c` (banked),
the generated `sched_gen.h`/`.c`/`_names.c`; the firmware's tasks in
`tasks.c` and `flash_job.c`.

## Tasks and steps

- A **task** is a slot in a fixed table of 24 in xdata:
  `struct task { uint8_t state; uint8_t mask; uint16_t wake; uint16_t arg; }`,
  6 bytes. `state == 0` means the slot is free; `arg` is the step's own 16
  bits (a cursor, a poll tick).
- A **state** is a one-byte ID. The generator (chapter 5) maps each to one
  step function and emits a table of their 24-bit addresses (the bank in
  bits 16-23, as SDCC encodes banked calls).
- Tasks and steps are **declared where they are written** (decision 24):
  `TASK(stp, fixed, medium)` and `STEP(stp, tick) { ... }`, which is the
  function `uint8_t stp_tick(void) __banked` and the state `S_STP_TICK`.
- A step **does a bounded amount of work, then returns the state it wants
  next** (decision 22): `return S_FOO;`, `return STEP_STAY;` (the same
  state), `return STEP_END;` (the slot is free), or `return STEP_SLEEP(...)`,
  `STEP_WAIT(...)`, `STEP_ARG(...)`, which say when, or set `arg`, as well
  (chapter 4). The dispatcher stores it; nothing else sets a slot's state.
  A step that can fall off its end fails the build.
- A module's own data lives in the module, in static xdata, including any
  cursor a cli job needs. A task with several instances (`count=2`: the SFP
  slots) tells them apart by `sched_cur - T_SFP0`.
- **Fixed tasks** have their slots from `sched_init()`: 12 in the firmware.
  The other 12 slots are **dynamic**: `task_claim()` gives one to a job. The
  firmware's only job is the flash job (chapter 10).

## Readiness

A task is ready when **one of its events fired or its wake time has come**:

```c
ready = (t->mask & sched_ev) || (int16_t)(sched_now - t->wake) >= 0;
```

- `sched_now` is the 16-bit tick count (5 ms), read at the pass's start and
  again after each step. A wake is at most 0x7fff ticks (~163 s) ahead.
- `sched_ev` is the pass's hardware events (chapter 6), taken at the pass's
  start: every task sees the same, and several can wake on one.
- A wait without a timeout (`SCHED_FOREVER`) keeps a horizon 0x7fff ticks
  ahead, marked `EV_FOREVER` in the mask; when the horizon comes, the pass
  pushes it on instead of running the step.
- One task wakes another directly (`task_wake(T_X)`): the woken slot is due
  now and runs at its next position.

## The run sequence

The loop walks a generated array of slot indices, not the table in order.
A task appears in it as often as its frequency asks. The firmware's, 72
positions (evidence/fb-build.txt):
`rx link loop con stp cli | rx link loop con net lldp | rx link loop con lacp sfp0 | ...`.
The four high tasks come every 6 positions, stp and lacp 3 times a pass,
the rest once; each of the 12 dynamic slots has one position. A task's
worst latency is about its spacing in steps times the longest step.
Frequency is how priority is expressed; there are no priority classes. A
free or not-ready position costs a load and a compare.

## Rules every step follows

1. Return within 50 ms; aim for under 5 ms (decision 25). A sector erase
   (`FL_ERASE`, ~40 ms) is inside the limit.
2. Leave shared buffers (`uip_buf`, `sfr_data`, `cmd_buffer`, `ctl_buf`)
   consistent, and never hold one across steps. The exception is uIP's one
   TCP connection: a `/cmd` or an upload holds `config_buf` (and a `/cmd`
   `outbuf`) across steps, which is safe because no other request can
   arrive meanwhile (chapters 7, 8).
3. No `delay()` in a step; wait by returning with a wake time
   (`STEP_SLEEP`). The old code's `delay()`s that a step can reach were
   taken out where measured (`eee`, chapter 9); `cmd_finish()`, which runs a
   cli job to its end outside the scheduler (the config at boot), keeps
   one.
4. No 32-bit arithmetic on scheduler and step paths; locals of functions
   that call others are `static __xdata` (chapter 12).
5. Never start a flash erase or program and return while it is busy: the
   primitives wait in the common area (chapter 10).
6. Don't print more than the free TX ring holds. A command starts with 1 KiB
   of the 2 KiB ring free; a dump that grows with a table is a cli job, a
   line a step, each after room for a line (chapter 7).

## What stayed as it was

- ISRs: they set bits and fill rings. The tick ISR also counts the
  watchdog down, and the serial ISR resets on `^R ^R` (chapter 11).
- uIP: driven by the `rx` and `net` tasks; httpd remains uIP callbacks.
- The register, SerDes, MDIO and flash primitives; their polls got bounds
  (chapter 9).
- The boot path, up to `sched_init()`.
