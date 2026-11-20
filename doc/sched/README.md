# The cooperative scheduler

Status: **as built**, 2026-11-20. M1, M2 and M3 are done; M4, validation,
is in progress (phases.md). The code is branch `feat/sched` of the sirmick
fork (this book describes 72cee59, off fork main 612116e); the simulator
side is ovs-rtlpg, branch `feat/sched-sim`.

RTLPlayground used to run one super-loop: every pass called each piece of
work in turn, and each ran to completion. Any long piece of work stalled
everything else. A 1000-entry `l2` dump held the loop for 3.2 s, a firmware
upload for 1.9 s in one pass (hardware.md). Either is long enough to time
out a fast LACP partner and delay STP.

The firmware now runs a **cooperative scheduler**. Work is split into short
**steps**: functions that do a bounded piece of work and return the state
they want next. A table of 24 **task** slots says which step each slot runs
next, and when: at a tick, or on a hardware event. The main loop walks a
generated **run sequence** and calls the steps that are ready. Nothing is
preemptive: one stack, no reentrancy, and not one byte of internal RAM.

What it gave, measured in rtlsim (requirements.md): LACPDUs 1.01-1.03 s
apart while `l2` prints 1000 entries, where the old loop went 3.86 s
without one; BPDU hellos 1.97-2.04 s apart during an upload, where the old
loop was silent for 2.84 s; no step over 20 ms in any reference scenario
but the flash erase. On the 8x, `l2` with 1000 MACs ran as 1966 steps, the
longest 429 µs, with loop detection polling throughout (silicon.md).

To add a task, start at [howto.md](howto.md): five small examples on the
real API, the rules, and the common mistakes.

Where it is in the code:

| What | Files |
|---|---|
| the core | `sched.h`, `sched.c` (resident), `sched_bank.c`, `sched_health.c` (banked) |
| the generator | `tools/sched_sweep.py`, `tools/sched_gen.py`, `sched.mk` |
| the firmware's tasks | `tasks.c`, `flash_job.c`; their bodies in `rtlplayground.c`, `cmd_parser.c`, `httpd/httpd.c`, `sfp.c` |
| the console rings | `con_ring.h`, `con_ring.c` |
| the watchdog and the crash record | `crash.h`, `crash.c`, the ISRs in `rtlplayground.c` |
| the test image | `schedtest/` |
| the resource gate | `tools/resource_gate.py`, `doc/sched/baseline/` |

## Chapters

| # | File | What |
|---|---|---|
| 1 | [requirements.md](requirements.md) | goals, deadlines, constraints, and which are met |
| 2 | [hardware.md](hardware.md) | the measured facts the design rests on |
| 3 | [architecture.md](architecture.md) | tasks, steps, slots, events, the run sequence |
| 4 | [core.md](core.md) | the scheduler core: data, loop, API, measurement |
| 4a | [howto.md](howto.md) | how to add a task: five examples, rules, common mistakes |
| 5 | [generator.md](generator.md) | `TASK`/`STEP`, the sweep, the generated tables, the checks |
| 6 | [events.md](events.md) | the hardware events and `task_wake` |
| 7 | [console.md](console.md) | UART rings, the `con` and `cli` tasks, cli jobs, web commands |
| 8 | [net.md](net.md) | rx and net, uIP, httpd, the control-frame TX buffer, uploads |
| 9 | [modules.md](modules.md) | every task and its states; SFP, polls, VLAN bitmap |
| 10 | [flash.md](flash.md) | the flash job, and the "nothing banked while busy" rule |
| 11 | [watchdog.md](watchdog.md) | the software watchdog and the crash record |
| 12 | [memory.md](memory.md) | memory as built, the IRAM rules, the resource gate |
| 13 | [rtlsim.md](rtlsim.md) | the simulator's side: control socket, timing models, the harness |
| 14 | [testing.md](testing.md) | the test layers, the tools, the silicon runs |
| 15 | [phases.md](phases.md) | milestones and packages, each with its evidence |
| 16 | [open.md](open.md) | the decision log, and what is still open |
| 17 | [silicon.md](silicon.md) | the bench runs on the 8x (C, DB, EB) |
| - | [upstream-bugs.md](upstream-bugs.md) | upstream bugs found on the way |
| - | [evidence/](evidence/) | each package's proof: commands and results |

## Words used throughout

- **Step:** a `uint8_t f(void) __banked` function (`STEP(task, name)`) that
  does a bounded amount of work and returns the state it wants next.
  Target under 5 ms, limit 50 ms (chapter 1, decision 25).
- **State:** a one-byte ID naming a step. A slot's state is the step it runs
  next; 0 is a free slot.
- **Task:** a slot in the task table, `{state, mask, wake, arg}`.
- **Fixed task:** has its slot from the start (rx, link, con, cli, stp, ...).
- **Job:** claims a free slot, runs, frees it. In the firmware: the flash
  job. A long console command is not a job of its own but states of `cli`
  (a "cli job", chapter 7).
- **Hardware event:** one of 4 bits (8 in the mask), from an ISR flag or
  sampled at the pass's start. Latched for one pass.
- **Wake:** one task making another due now (`task_wake`). Not an event.
- **Run sequence:** the generated order of slot positions the loop walks; a
  task can appear several times to be checked more often.
- **Pass:** one walk through the run sequence.
- **Common area / HOME:** the 16 KiB of code RAM at 0x0000-0x3fff, always
  mapped. **Banks:** the 48 KiB window at 0x4000-0xffff, switched by `PSBANK`.

## Sources

- The evidence files, one per package (`evidence/`), and the bench runs
  (silicon.md).
- Silicon measurements and the hardware map: `private notes: README.md` in the
  RTLPlayground checkout (private notes), sections "Interrupts, timer,
  watchdog" and "Timing for a cooperative OS"; `doc/sfr.md` on `doc/sfr-map`.
- Code audit of every blocking path: `private notes: os-audit.md`.
- Prior art (Contiki on the 8051, RTX51 Tiny, QP-nano, ...) and what upstream
  maintainers want: `private notes: os-prior-art.md`.
- Upstream discussion: logicog/RTLPlayground #501 (timers), #557 (tasks),
  #761 (shared timers, merged).
