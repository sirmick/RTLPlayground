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

## Open

| # | Question | Leaning |
|---|---|---|
| O1 | Can C express a banked function's bank byte in a `__code` initializer, or must `step_addr` be generated asm? | settle in B1 |
| O2 | `run_seq` length and the high/medium/once spacing numbers | derive in B1, tune with D2's measurements |
| O3 | Does `uip_stop()`/`uip_restart()` throttle uploads cleanly with real browsers (Firefox, curl)? | test in E3 |
| O4 | Should `EV_CON_TX` be "room for a line" (100 B) or any room? | a line |
| O5 | The NIC RX ring's real capacity under a burst (sets how often `rx` must run) | measure in D0 on the 8x |
| O6 | Push branches, and when | local until Michael says |
| O7 | Does the schedtest image share `rtlplayground.c`'s ISRs or have its own? | share: it tests the real ISRs |
