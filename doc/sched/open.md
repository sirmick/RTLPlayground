# 16. Decisions and open questions

## Decided (with Michael, 2026-11-18)

| # | Decision |
|---|---|
| 1 | Cooperative, single stack, run-to-completion steps. No preemption |
| 2 | Task = slot `{state, mask, wake}`; state = 1-byte ID; dispatch through a **generated 3-byte address table** (smaller than a `switch`) |
| 3 | **Python generator** from per-module `.def` files |
| 4 | **16-bit event mask**; events from ISR bits, sampled hardware state, and task posts |
| 5 | Time given in **ms** (rounded up to 5 ms ticks), stored as an absolute 16-bit tick |
| 6 | **Round robin over a generated run sequence**; frequency (appearances in the sequence) instead of priority classes |
| 7 | Step limit **20 ms** (target 5 ms); flash erase (~40 ms) the documented exception |
| 8 | Shared globals stay global; modules coordinate; no step holds a shared buffer across steps |
| 9 | TX ring big enough for one line; long prints chunk per line; per-module context carries the cursor |
| 10 | **One cli task** serving console and web, one command at a time |
| 11 | Granularity: module-level tasks; LLDP/SFP step per port/slot internally |
| 12 | Simulator first (P0: control socket over a unix socket, lockstep clock, UART timing, flash busy, coverage); virtual time P0 |
| 13 | Start with a small test scheduler with dummy tasks before the whole firmware |
| 14 | Work on the sirmick fork first; upstream later |
| 15 | Matching the sim's flash-busy garbage to silicon waits for bring-up (C) |
| 16 | (was O1, settled in B1) `step_addr` is C: `__code const` table of a `__banked` function-pointer type, 3 bytes per entry (low, high, bank), in a resident file. SDCC 4.6 rejects casts of a function address in an initializer, but `typedef void (*step_fn)(void) __banked` emits `.byte _f, (_f >> 8), (_f >> 16)`; linked through place_banks.py the bytes are right (bank 2 and 3 functions checked), and a call through it is SDCC's own `__sdcc_banked_call` with no DSEG. Evidence: `evidence/o1-bank-byte.txt` |
| 17 | (was O7, settled in B3) schedtest shares `rtlplayground.c`'s ISRs: the whole firmware is linked, and `SCHEDTEST` swaps in another `main()` (no switch init) and the ISRs' ring, watchdog and crash paths. The firmware build is unchanged (`evidence/b3-schedtest.txt`) |
| 18 | Steps end with return macros (`STEP_NEXT/SLEEP/WAIT/ARG/END`); a 16-bit per-slot `arg`. Michael, 2026-11-18 |
| 19 | Console RX ring 256 B, 8-bit index; covers 20 ms steps; erase + paste may lose. Michael, 2026-11-18 |

## Open

| # | Question | Leaning |
|---|---|---|
| O2 | `run_seq` length and the high/medium/once spacing numbers | B1 derived, for modules.md's set (4 high, 2 medium, 7 once, 11 dynamic slots): `--fill 2` (default) gives 72 positions, a high task at most 5 steps from its next run, medium every 24; fill 3: 56 / 6 / 20; fill 4: 48 / 7 / 16. Idle positions cost ~1 µs, so only the steps that run count: keep fill 2 until D2 measures the steps |
| O8 | Hand-written dispatch asm (load r0-r2 from `step_addr`, `lcall __sdcc_banked_call`) instead of SDCC's, which keeps r5-r7 pushed for the whole step | an option to win back 3 B of stack per step if B4's measurement is tight; not done |
| O3 | Does `uip_stop()`/`uip_restart()` throttle uploads cleanly with real browsers (Firefox, curl)? | test in E3 |
| O4 | Should `EV_CON_TX` be "room for a line" (100 B) or any room? | a line |
| O5 | The NIC RX ring's real capacity under a burst (sets how often `rx` must run) | measure in D0 on the 8x |
| O6 | Push branches, and when | local until Michael says |
