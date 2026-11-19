# 16. Decisions and open questions

## Decided (with Michael, 2026-11-18)

| # | Decision |
|---|---|
| 1 | Cooperative, single stack, run-to-completion steps. No preemption |
| 2 | Task = slot `{state, mask, wake}`; state = 1-byte ID; dispatch through a **generated 3-byte address table** (smaller than a `switch`) |
| 3 | **Python generator** from per-module `.def` files |
| 4 | **16-bit event mask**; events from ISR bits, sampled hardware state, and task posts (superseded by 23) |
| 5 | Time given in **ms** (rounded up to 5 ms ticks), stored as an absolute 16-bit tick |
| 6 | **Round robin over a generated run sequence**; frequency (appearances in the sequence) instead of priority classes |
| 7 | Step limit **20 ms** (target 5 ms); flash erase (~40 ms) the documented exception |
| 8 | Shared globals stay global; modules coordinate; no step holds a shared buffer across steps |
| 9 | TX ring big enough for one line; long prints chunk per line; per-module context carries the cursor (the TX ring part superseded by 21) |
| 10 | **One cli task** serving console and web, one command at a time |
| 11 | Granularity: module-level tasks; LLDP/SFP step per port/slot internally |
| 12 | Simulator first (P0: control socket over a unix socket, lockstep clock, UART timing, flash busy, coverage); virtual time P0 |
| 13 | Start with a small test scheduler with dummy tasks before the whole firmware |
| 14 | Work on the sirmick fork first; upstream later |
| 15 | Matching the sim's flash-busy garbage to silicon waits for bring-up (C) |
| 16 | (was O1, settled in B1) `step_addr` is C: `__code const` table of a `__banked` function-pointer type, 3 bytes per entry (low, high, bank), in a resident file. SDCC 4.6 rejects casts of a function address in an initializer, but `typedef void (*step_fn)(void) __banked` emits `.byte _f, (_f >> 8), (_f >> 16)`; linked through place_banks.py the bytes are right (bank 2 and 3 functions checked), and a call through it is SDCC's own `__sdcc_banked_call` with no DSEG. Evidence: `evidence/o1-bank-byte.txt` |
| 17 | (was O7, settled in B3) schedtest shares `rtlplayground.c`'s ISRs: the whole firmware is linked, and `SCHEDTEST` swaps in another `main()` (no switch init) and the ISRs' ring, watchdog and crash paths. The firmware build is unchanged (`evidence/b3-schedtest.txt`) |
| 18 | Steps end with return macros (`STEP_NEXT/SLEEP/WAIT/ARG/END`); a 16-bit per-slot `arg`. Michael, 2026-11-18 (the macros superseded by 22) |
| 19 | Console RX ring 256 B, 8-bit index; covers 20 ms steps; erase + paste may lose. Michael, 2026-11-18 |
| 20 | (was O3, settled in E3) Uploads are flow-controlled by the advertised receive window (the stage's free space), not `uip_stop()`: Linux splits a 1-MSS window into two half segments and drops/RTOs the second (measured 2026-11-19: curl 0.5 KiB/s, then a reset). Chapter 8 item 1; `evidence/e3-flash-job.txt` |
| 21 | Console **TX ring 2 KiB, 16-bit index**: most commands' cost was the UART wait, not their work (measured 2026-11-19, `tools/cli_steps.py`: with the 128 B ring `version` 5.1 ms ... `history` 186 ms; without the wait all but `history`, `health tasks` and a large `l2` under 5 ms). Only dumps that grow with a table, or whose work passes 5 ms, are chunked (cli jobs: `l2`, `stat`, `lacp show`, `history`, `health tasks`). Supersedes decision 9's "TX ring big enough for one line". Orchestrator, E1, QA e1-tx-ring |
| 22 | A **step returns the state it wants next**: `return S_X;`, `return STEP_STAY;`, `return STEP_END;` (0), `return STEP_WAIT(s, ev, ms)` / `STEP_SLEEP` / `STEP_ARG` (expressions). The dispatcher stores it; helpers can't choose the state (`task_next`, `task_end` are gone). A step that can fall off its end fails the build (SDCC warning 59 as an error for the step sources). Michael, 2026-11-19; E5, core.md |
| 23 | **Hardware-only 8-bit event mask** (`HW_RX`, `HW_LINK`, `HW_CON_RX`, `HW_CON_TX`; bit 7 `EV_FOREVER`, the scheduler's own); one task waking another is `task_wake(T_X)` (a job: its claimed slot), which makes the slot due now; the woken step re-checks why, a woken periodic task that it is due. `EV_LINE`, `EV_WEBCMD` became cli's wakes, `EV_UPLOAD_DONE` net's; `EV_CMD_DONE`, `EV_UPLOAD`, `EV_CFG`, `EV_LINKCHG` had no reader and are gone. A link-change wake (from `link_read`, with a due-check in the woken stp/lacp/loop) is the pattern for a future consumer, not built: nothing consumes it today (QA e5-linkchg). Michael, 2026-11-19; E5, events.md |
| 24 | Tasks and steps are **declared inline**: `TASK(name, fixed\|job, high\|medium\|once [, count=N] [, first=step])` and `STEP(task, step)` (the function `task_step`, state `S_TASK_STEP`). A sweep preprocesses each step source with the build's flags (`#ifdef`'d steps are right) and writes the `.def` files into the build directory; the generator runs on them with all its checks. The hand-written `.def` files are gone; `sequence.def` stays. Michael, 2026-11-19; E5, generator.md |

## Open

| # | Question | Leaning |
|---|---|---|
| O2 | `run_seq` length and the high/medium/once spacing numbers | B1 derived, for modules.md's set (4 high, 2 medium, 7 once, 11 dynamic slots): `--fill 2` (default) gives 72 positions, a high task at most 5 steps from its next run, medium every 24; fill 3: 56 / 6 / 20; fill 4: 48 / 7 / 16. Idle positions cost ~1 µs, so only the steps that run count: keep fill 2 until D2 measures the steps |
| O8 | Hand-written dispatch asm (load r0-r2 from `step_addr`, `lcall __sdcc_banked_call`) instead of SDCC's, which keeps r5-r7 pushed for the whole step | an option to win back 3 B of stack per step if B4's measurement is tight; not done |
| O4 | Should `HW_CON_TX` (was `EV_CON_TX`) be "room for a line" (100 B) or any room? | a line |
| O5 | The NIC RX ring's real capacity under a burst (sets how often `rx` must run) | measure in D0 on the 8x |
| O6 | Push branches, and when | local until Michael says |
