# 16. Decisions and open questions

## The decision log

Decisions 1-15 were taken with Michael on 2026-11-18, before any code; the
rest as the work went. A later decision that replaced part of an earlier
one says so; the earlier one is kept as it was decided, marked.

| # | Decision | Replaced |
|---|---|---|
| 1 | Cooperative, single stack, run-to-completion steps. No preemption | - |
| 2 | Task = slot `{state, mask, wake}`; state = 1-byte ID; dispatch through a **generated 3-byte address table** (smaller than a `switch`). The slot later got `arg` (18) and an 8-bit mask (23) | - |
| 3 | **Python generator** from per-module `.def` files. The `.def` files are now written by the sweep from inline markers (24) | - |
| 4 | **16-bit event mask**; events from ISR bits, sampled hardware state, and task posts. *Superseded by 23* | - |
| 5 | Time given in **ms** (rounded up to 5 ms ticks), stored as an absolute 16-bit tick | - |
| 6 | **Round robin over a generated run sequence**; frequency (appearances in the sequence) instead of priority classes | - |
| 7 | Step limit **20 ms** (target 5 ms); a flash erase (~40 ms) the documented exception. *Superseded by decision 25: 50 ms* | - |
| 8 | Shared globals stay global; modules coordinate; no step holds a shared buffer across steps (the one-connection exception: console.md, net.md) | - |
| 9 | TX ring big enough for one line; long prints chunk per line; per-module context carries the cursor. *The TX ring part superseded by 21* | - |
| 10 | **One cli task** serving console and web, one command at a time | - |
| 11 | Granularity: module-level tasks; LLDP/SFP step per port/slot internally. As built, SFP steps per slot (E2); LLDP sends from its one step | - |
| 12 | Simulator first (P0: control socket over a unix socket, lockstep clock, UART timing, flash busy, coverage); virtual time P0 | - |
| 13 | Start with a small test scheduler with dummy tasks before the whole firmware | - |
| 14 | Work on the sirmick fork first; upstream later | - |
| 15 | Matching the sim's flash-busy reads to silicon waits for bring-up (C). Done in C: the window reads 0x00, and rtlsim does too (silicon.md) | - |
| 16 | (was O1, settled in B1) `step_addr` is C: a `__code const` table of a `__banked` function-pointer type, 3 bytes per entry (low, high, bank), in a resident file. SDCC 4.6 rejects casts of a function address in an initializer, but `typedef void (*step_fn)(void) __banked` emits `.byte _f, (_f >> 8), (_f >> 16)`; linked through place_banks.py the bytes are right, and a call through it is SDCC's own `__sdcc_banked_call` with no DSEG. `evidence/o1-bank-byte.txt` | O1's alternative, an asm table |
| 17 | (was O7, settled in B3) schedtest shares `rtlplayground.c`'s ISRs: the whole firmware is linked, and `SCHEDTEST` swaps in another `main()` and the ISRs' ring, watchdog and crash paths; the firmware build is unchanged (`evidence/b3-schedtest.txt`). Since C, schedtest's `main()` runs the firmware's own boot first (`firmware_init()`, `evidence/c-firmware-init.txt`) | - |
| 18 | Steps end with return macros (`STEP_NEXT/SLEEP/WAIT/ARG/END`); a 16-bit per-slot `arg`. Michael, 2026-11-18. *The macros superseded by 22*; `arg` stays | - |
| 19 | Console RX ring 256 B, 8-bit index; covers 20 ms steps; an erase during a paste may lose bytes. Michael, 2026-11-18. *Note (Michael, 2026-11-20):* the ring also bounds type-ahead during a command, since the editor doesn't read while one runs (U2): up to 255 B survive, the rest (Enter included) is lost and counted as overruns; the old firmware lost all of it (evidence/f1-acceptance.txt item 3, feat/sched 2354e31; console.md) | the 16 B ring |
| 20 | (was O3, settled in E3) Uploads are flow-controlled by the advertised receive window (the stage's free space), not `uip_stop()`: Linux splits a 1-MSS window into two half segments and drops/RTOs the second (measured 2026-11-19: curl 0.5 KiB/s, then a reset). net.md; `evidence/e3-flash-job.txt` | the plan's `uip_stop()` after each segment |
| 21 | Console **TX ring 2 KiB, 16-bit index**: most commands' cost was the UART wait, not their work (measured 2026-11-19, `tools/cli_steps.py`: with the 128 B ring `version` 5.1 ms ... `history` 186 ms; without the wait all but `history`, `health tasks` and a large `l2` under 5 ms). Only dumps that grow with a table, or whose work passes 5 ms, are chunked (cli jobs). Orchestrator, E1, QA e1-tx-ring | 9's "TX ring big enough for one line" (128 B) |
| 22 | A **step returns the state it wants next**: `return S_X;`, `return STEP_STAY;`, `return STEP_END;` (0), `return STEP_WAIT(s, ev, ms)` / `STEP_SLEEP` / `STEP_ARG` (expressions). The dispatcher stores it; helpers can't choose the state (`task_next`, `task_end` are gone). A step that can fall off its end fails the build (SDCC warning 59 as an error for the step sources). Michael, 2026-11-19; E5, core.md | 18's macros, which set the state through `task_next()` |
| 23 | **Hardware-only 8-bit event mask** (`HW_RX`, `HW_LINK`, `HW_CON_RX`, `HW_CON_TX`; bit 7 `EV_FOREVER`, the scheduler's own); one task waking another is `task_wake(T_X)` (a job: its claimed slot), which makes the slot due now; the woken step re-checks why, a woken periodic task that it is due. `EV_LINE`, `EV_WEBCMD` became cli's wakes, `EV_UPLOAD_DONE` net's; `EV_CMD_DONE`, `EV_UPLOAD`, `EV_CFG`, `EV_LINKCHG` had no reader and are gone. A link-change wake (from `link_read`, with a due-check in the woken stp/lacp/loop) is the pattern for a future consumer, not built (QA e5-linkchg). Michael, 2026-11-19; E5, events.md | 4 |
| 24 | Tasks and steps are **declared inline**: `TASK(name, fixed\|job, high\|medium\|once [, count=N] [, first=step])` and `STEP(task, step)` (the function `task_step`, state `S_TASK_STEP`). A sweep preprocesses each step source with the build's flags (`#ifdef`'d steps are right) and writes the `.def` files into the build directory; the generator runs on them with all its checks. `sequence.def` stays for an explicit sequence (only the host test has one). Michael, 2026-11-19; E5, generator.md | the hand-written `tasks.def` and schedtest's `.def` files |
| 25 | **Step limit 50 ms, target 5 ms** (supersedes decision 7's 20 ms). `FL_ERASE` (~40 ms) is now inside the limit, not an exception; `SCHED_OVERRUN_FINE` counts a step of 50 ms or more. The parsing and formatting steps (the config POST's RX_STEP, 18.2 ms on the 8x; `/vlanlist.json` with 40 named VLANs, 34 ms) are left for the object-model project. Michael, 2026-11-20 |
| 26 | **Health rates:** each state's runs per second (the last whole second) replace the saturating 16-bit totals, and a **load %**, the part of the last second not asleep (`sched_idle()`'s sleeps timed by the fine clock, in 1% steps and a 16-bit remainder). The roll is the dispatcher's: at the first pass end at or past a fixed 200-tick grid, half a second off the 1 s tasks'; a late roll's time goes to the next second. `health clear` leaves the rates and load. +2 B x S_COUNT + 8 B xdata (66 B in the firmware), 301 B banked code (`sched_bank.c` +276, `sched_health.c` +25), internal RAM 0. Michael chose it (runs/s + load), 2026-11-20; FR, core.md "Measurement", `evidence/fr-rates.txt` | the saturating run totals |

## Open

| # | Question | Leaning |
|---|---|---|
| O2 | `run_seq` length and the high/medium/once spacing | the firmware uses `--fill 2`: 72 positions, a high task every 6, medium every 24 (evidence/fb-build.txt). Idle positions cost a load and a compare, so only the steps that run count; nothing measured so far asks for a change |
| O5 | The NIC RX ring's real capacity under a burst (sets how often `rx` must run) | not measured on silicon; rtlsim models 10 KiB. A burst test on the 8x (F2) |
| O6 | Push branches, and when | Michael says, each time |
| O8 | Hand-written dispatch asm (load r0-r2 from `step_addr`, `lcall __sdcc_banked_call`) instead of SDCC's, which keeps r5-r7 pushed for the whole step | would win back 3 B of stack per step; not needed while the headroom is 60-70 B (memory.md); not done |
| O9 | Advertise more than one MSS of receive window when the stage has room? An upload on the 8x took 16.2 s against fork main's 14.9 s; decision 20 allows it | done in F1b (58c1601): the window is the stage's free space; the sim is flash-bound (5.12 s, unchanged), the 8x is F2's to measure |

## Closed questions

| # | Question | Became |
|---|---|---|
| O1 | the 3-byte table: C or asm | decision 16 |
| O3 | upload flow control | decision 20 |
| O4 | `HW_CON_TX` (was `EV_CON_TX`): room for a line, or any room? | a line: 100 B (`CON_TX_LINE`), as built (events.md) |
| O7 | how schedtest gets the ISRs | decision 17 |
