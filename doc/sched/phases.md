# 15. Phases

The wash plan (`.wash/plan.toml`) mirrors this. Each package has a "done
when" that a reviewer checks before it is accepted.

## M1: simulator + scheduler test image + silicon bring-up

| Pkg | Repo | Work | Needs | Done when |
|---|---|---|---|---|
| A0 | both | branches; baselines: every ovs-rtlpg test, RTLPlayground host tests, the 8x image's boot transcript in the sim | - | baselines recorded in `doc/sched/baseline/` |
| A1 | ovs-rtlpg | control socket, test-driven clock, `rtlsim_ctl.py` (ch. 13) | A0 | a test boots the 8x image, runs until the prompt, peeks a symbol; two runs give byte-identical stamped transcripts |
| A2 | ovs-rtlpg | UART at baud rate | A1 | `bench` in the sim ~87 µs/char; an unread 16 B ring overflows on a paste |
| A3 | ovs-rtlpg | flash busy: timings, 0xFF reads, fetch checker | A1 | the jump-into-bank-during-erase program is flagged; the correct one isn't |
| A4 | ovs-rtlpg | coverage + `cov_report.py` | A1 | a boot's function list; two runs diff clean |
| A5 | ovs-rtlpg | regression | A2, A3, A4 | every existing test passes; boot transcript matches the baseline in content |
| B1 | RTLPlayground | generator + `.def` format + host tests | A0 | host tests pass incl. broken specs; the 3-byte table question (C or asm) settled |
| B2 | RTLPlayground | scheduler core, rings, swdt, crash record, `health tasks` + host tests | A0 | host tests pass; core compiles into HOME within budget |
| B3 | RTLPlayground | `make schedtest`, dummy tasks across all banks | B1, B2 | builds; the resource report printed |
| B4 | both | sim tests for the schedtest image (ch. 14 layer 2) | B3, A2, A3, A4 | all pass, deterministic, < 2 min wall |
| B5 | both | negative tests | B4 | each fails exactly as intended |
| B6 | RTLPlayground | resource gate | B3 | gate runs in the test target with a baseline |
| C | bench | schedtest on the 8x; record flash-busy garbage values; match the sim | B4, B5 | timing within the sim's bounds; crash record survives the reset; findings in `doc/sched/silicon.md` |

## M2 (D): foundation on the real firmware, no behaviour change

D0 sim P1 (NIC ring, LACP partner) + the old-vs-new harness + reference
scenarios; D1 UART rings; D2 today's loop phases become fixed tasks; D3
control-frame TX buffer, web command buffer, one cli task.

Carried from B2's review, for D1 and the crash line:
- `con_putc()` busy-waits on a full TX ring for the ISR to drain it, so it
  must never run with interrupts (or ES) off: code that prints with EA = 0
  today (the `if (EA)` debug prints) has to stay polled.
- `crash_boot()`'s line can be longer than the 127 free bytes of the TX
  ring: print it with interrupts on, or polled, before the rings take over.

## M3 (E): break up the blockers

E1 chunked CLI dumps; E2 SFP slot tasks + DDM cache; E3 flash job for
upload, config, factory reset; E4 hardware-poll timeouts, `eee` waits, VLAN
bitmap.

## M4 (F): validation

F1 the full acceptance suite + old-vs-new on every reference scenario; F2
bring-up on the 8x, then the 4+2 (RTL8372, web only); F3 a 24 h bench soak;
F4 the estate in rollout order (switch A or switch B first; switch C and switch D
only when very sure).

## Independent of the phases

Bugs found by the audit, each a small PR with a sim test: the shared console
and web command buffer (also fixed by D3), the CRC routine leaving `PSBANK` at
1, the health phase-ID clash.
