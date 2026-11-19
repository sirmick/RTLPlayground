# The cooperative scheduler: design book

Status: **draft 1**, 2026-11-18. Branch `feat/sched` (RTLPlayground, off the
sirmick fork main 612116e); the simulator side lives in ovs-rtlpg, branch
`feat/sched-sim`.

RTLPlayground runs one super-loop today: every pass calls each piece of
work in turn, and each runs to completion. That is simple and has served
well, but any long piece of work stalls everything else. A 1000-entry `l2`
dump holds the loop for 3.2 s, and a firmware upload for 1.9 s in one pass.
Either is long enough to time out a fast LACP partner, delay STP, and let a
loop storm run.

This book designs the replacement: a **cooperative scheduler**. Work is
split into short **steps**, and a table of **tasks** says which step each
runs next and when it wants to run again. The main loop walks a generated
**run sequence** and calls the ready steps. Nothing is preemptive: one
stack, no reentrancy, and nothing that needs internal RAM.

## Chapters

| # | File | What |
|---|---|---|
| 1 | [requirements.md](requirements.md) | goals, non-goals, deadlines, constraints |
| 2 | [hardware.md](hardware.md) | the measured facts the design rests on |
| 3 | [architecture.md](architecture.md) | tasks, steps, slots, events, the run sequence |
| 4 | [core.md](core.md) | the scheduler core: data, loop, API |
| 5 | [generator.md](generator.md) | `TASK`/`STEP`, the sweep, the generated tables, the checks |
| 6 | [events.md](events.md) | the 16 event bits and where they come from |
| 7 | [console.md](console.md) | UART rings, the `con` and `cli` tasks, chunked output, web commands |
| 8 | [net.md](net.md) | rx, uIP, httpd, the control-frame TX buffer |
| 9 | [modules.md](modules.md) | every task and its states, module by module |
| 10 | [flash.md](flash.md) | the flash job, and the "nothing banked while busy" rule |
| 11 | [watchdog.md](watchdog.md) | the software watchdog and the crash record |
| 12 | [memory.md](memory.md) | memory budget, the IRAM rules, the resource gate |
| 13 | [rtlsim.md](rtlsim.md) | simulator work: control socket, lockstep, UART, flash busy, coverage |
| 14 | [testing.md](testing.md) | test layers, the schedtest image, negative tests, old-vs-new |
| 15 | [phases.md](phases.md) | milestones and packages with "done when" (the wash plan mirrors it) |
| 16 | [open.md](open.md) | open questions and decisions taken |

## Words used throughout

- **Step:** a `void f(void)` function that does a bounded amount of work and
  returns. Target under 5 ms, limit 20 ms (chapter 4).
- **State:** a one-byte ID naming a step. A task's state is the step it runs next.
- **Task:** a slot in the task table: `{state, mask, wake}`.
- **Fixed task:** claims its slot at init and keeps it (STP, LACP, the console...).
- **Job:** a dynamic task: claims a free slot, runs, frees it (a CLI dump, an
  upload's flash work, a cable test).
- **Event:** one of 16 bits, set by an ISR, sampled from hardware, or posted
  by a task. Latched for one pass.
- **Run sequence:** the generated order of slot positions the loop walks; a
  task can appear several times to be checked more often.
- **Pass:** one walk through the run sequence.
- **Common area / HOME:** the 16 KiB of code RAM at 0x0000-0x3fff, always
  mapped. **Banks:** the 48 KiB window at 0x4000-0xffff, switched by `PSBANK`.

## Sources

- Silicon measurements and the hardware map: `hw-research/README.md` in the
  RTLPlayground checkout (private notes), sections "Interrupts, timer,
  watchdog" and "Timing for a cooperative OS"; `doc/sfr.md` on `doc/sfr-map`.
- Code audit of every blocking path: `hw-research/os-audit.md`.
- Prior art (Contiki on the 8051, RTX51 Tiny, QP-nano, ...) and what upstream
  maintainers want: `hw-research/os-prior-art.md`.
- Upstream discussion: logicog/RTLPlayground #501 (timers), #557 (tasks),
  #761 (shared timers, merged).
