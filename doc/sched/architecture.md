# 3. Architecture

```
            ISRs (common area)                         xdata
  INT0 link ─┐  INT1 rx ─┐  UART ─┐  T2 tick ─┐
             └──── set __bit flags / ring bytes ┘
                                                  ┌────────────────────┐
  main loop (common area: sched.c)                │ task table [24]    │
  ┌─────────────────────────────────────────┐     │ {state,mask,wake}  │
  │ pass start: snapshot events, kick swdt  │◄───►│ events (16 bits)   │
  │ for pos in run_sequence:                │     │ step maxima, ...   │
  │   slot = run_sequence[pos]              │     └────────────────────┘
  │   if ready(slot): time it, dispatch     │──► step_table[state] = 24-bit
  │ if nothing ran: PCON idle               │      address of a banked
  └─────────────────────────────────────────┘      step function
```

## Tasks and steps

- A **task** is a slot in a fixed table of 24 in xdata:
  `struct task { uint8_t state; uint16_t mask; uint16_t wake; uint16_t arg; }`,
  7 bytes; `arg` is the step's own 16 bits (a cursor, a count, a port).
  `state == 0` means the slot is free.
- A **state** is a one-byte ID. The generator (chapter 5) maps each ID to
  one step function, `uint8_t step(void)`, and emits a table of their 24-bit
  addresses (the bank is in bits 16-23, as SDCC encodes banked calls).
- A step **does a bounded amount of work, then returns the state it wants
  next** (decision 22): `return S_FOO;`, `return STEP_STAY;` (the same
  state), `return STEP_END;` (the slot is free), or `return STEP_SLEEP(...)`,
  `STEP_WAIT(...)`, `STEP_ARG(...)`, which say when as well (chapter 4).
  The scheduler stores it; nothing else sets a slot's state. A step that can
  fall off its end fails the build.
- A module's own data lives in the module, in static xdata ("the module
  context"), including any cursor a chunked job needs ("next param"). Steps
  find their instance through `sched_cur` (the slot index) where a module
  runs several (SFP slots, HTTP connections).

## Readiness

A task is ready when **its wake time has come or one of its events fired**:

```c
ready = (int16_t)(now - t->wake) >= 0 || (t->mask & ev);
```

- `now` is the 16-bit tick count (5 ms). Waits up to ~163 s; longer waits
  re-arm (chapter 4).
- `ev` is the pass's event snapshot (chapter 6): taken at pass start, so
  every task sees the same events, and several can wake on one.

## The run sequence

The loop walks a generated array of slot indices, not the table in order.
A task appears in it as often as its declared frequency asks:
`rx link loop con | stp | rx link loop con | lacp | rx link loop con | cli | ...`.
Its worst latency is about its spacing × the longest step. Frequency is how
priority is expressed; there are no priority classes. Dynamic jobs get fixed
positions; a free position costs one compare.

## Rules every step follows

1. Return within 20 ms; aim for under 5 ms.
2. Leave shared buffers (`uip_buf`, `outbuf`, `sfr_data`, `cmd_buffer`)
   consistent. Never hold one across steps (chapter 8).
3. No `delay()`; wait by returning with a wake time.
4. No 32-bit arithmetic on hot paths; locals that would spill are
   `static __xdata`.
5. Never start a flash erase or program and return while it is busy
   (chapter 10).
6. Print at most what fits the TX ring (one line); chunk longer output
   (chapter 7).

## What stays as it is

- ISRs: they set bits and fill rings. Nothing more.
- uIP: driven by the `rx` and `net` tasks; httpd remains uIP callbacks.
- The register, SerDes, MDIO and flash primitives.
- The boot path.
