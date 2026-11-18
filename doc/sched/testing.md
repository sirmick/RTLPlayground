# 14. Testing

Four layers, cheapest first.

## Layer 1: host unit tests (RTLPlayground `test/`, gcc)

- Scheduler logic compiled for the host with a fake tick and fake events:
  - the ready check, including 16-bit tick wrap at both ends;
  - `ms → ticks` rounding (never early);
  - the event snapshot and post-for-next-pass semantics;
  - claim and free, and a full table;
  - default-yield after a step that sets nothing;
  - a task appearing several times in `run_seq`.
- Generator: parse, every check with a broken input, a golden output for a
  known input, and the sequence derivation's spacing.
- Run on every build. Seconds.

## Layer 2: the schedtest image in the simulator

`make schedtest MACHINE=KEEPLINK_KP_9000_9XHPML_V1_2 LAYOUT=compact LOADER=1`
(built: `schedtest/README.md` has the commands, the symbols and the
variants): a
minimal image built with the **real** toolchain flow (crt, banking,
`place_banks`, the generator, the 3-byte table). It has the scheduler, the
UART rings, the software watchdog and the crash record, but no switch init.
Dummy tasks are spread over **every bank**:

| Dummy | Checks |
|---|---|
| `sleep_a/b/c`: 5 ms, 37 ms, 1000 ms periods; each logs `fine_now()` per run into an xdata ring | wake accuracy: never early, late by ≤ pass length |
| `waiter`: waits on an event posted by `poster`, 100 ms timeout | event latency ≤ one pass (the waiter is before the poster in `run_seq`; in general up to ~2 passes); the timeout path |
| `isr_ev`: waits on `EV_CON_RX`; the test writes UART bytes | ISR → event → task |
| `hot`: `freq=high`, 1 µs work | spacing: worst gap ≤ its spacing × the longest step |
| `jobber`: claims jobs that run N steps and end; claims until the table is full | claim, free, `SCHED_NONE` |
| `printer`: prints a 3000-line dump, one line per step | TX ring, `EV_CON_TX`, nothing lost, no step > 5 ms |
| `flasher`: erases and programs a sector of the upload area, then verifies | the flash job; checker clean |
| `slow`: one 25 ms step on command | the overrun counter and its state |
| `paste`: the test pastes 500 characters | the RX ring and `con` frequency: nothing lost |

Assertions over the control socket:

- At `sched_pass_end` breakpoints (every pass in short tests, sampled in
  soaks):
  - every slot's state is 0 or a valid ID;
  - masks use only defined bits;
  - no wake lies further ahead than its maximum wait;
  - `sched_posted` is cleared by each snapshot.
- The xdata logs, read at the end, satisfy the table's timing checks.
- Coverage: every state's step function ran; every line of `sched.c` ran.
- `checks` is empty: no bank, flash-busy or stack violations.
- Stack headroom ≥ 30 B (the paint).
- A **soak**: 10 minutes of firmware time with everything running, a few
  seconds of wall time, deterministic.

## Layer 2b: negative tests (must fail as intended)

| Variant | Expected |
|---|---|
| `bad_flash`: starts an erase and returns | the A3 checker reports a fetch from a bank while busy |
| `hang`: a step that spins forever | the software watchdog resets after 5 s; after the reset, the crash record names the slot and the state, and the boot prints it |
| `overrun`: a 25 ms step | counted, state named in `health` |
| `bad_spec` | the generator fails the build with a clear message |

## Layer 3: the same image on silicon (phase C)

The schedtest image on the 8x: the timing checks against `health`, the
flash job against real flash, a real hang leaving a real crash record across
the loader. It also records what garbage reads return during an erase, so
the sim's 0xFF can be matched.

## Layer 4: old vs new (phases D-F)

The same scenario in lockstep on two sims (the old build and the new),
compared on **results**, not traces (the scheduler deliberately changes the
order and timing of register writes):

- each command's output, compared per command;
- web JSON responses;
- the final register file, VLAN and L2 tables;
- flash contents (config);
- frames out per port: kinds and counts.

**Reference scenarios** (recorded from fork main): boot; a CLI tour; every
web page; VLAN, STP, LACP, loop, IGMP setups; config save; upload; SFP
insert and remove; link flaps.

**New-only acceptance checks:**
- LACP PDU gap < 1.2 s while `l2` dumps 1000 entries;
- BPDU hello spacing within ±50 ms during an upload;
- a pasted 500-character line arrives intact;
- no step > 20 ms except `FL_ERASE`;
- stack and internal RAM within the gate;
- coverage of the new build ⊇ the old build's, by function, minus the
  functions deliberately replaced.
