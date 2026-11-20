# 15. Phases

The wash plan (`.wash/plan.toml`) mirrors this. Each package had a "done
when" that a reviewer checked before it was accepted; its evidence file
gives the commands and results. M1, M2 and M3 are done; M4 is in progress.

## M1: simulator + scheduler test image + silicon bring-up (done)

| Pkg | Repo | What was done | Evidence |
|---|---|---|---|
| A0 | both | branches; baselines: every ovs-rtlpg test, the host tests, the 8x image's boot transcript, resources | `baseline/` |
| A1 | ovs-rtlpg | the control socket, the test-driven clock, `rtlsim_ctl.py`: deterministic runs | ovs-rtlpg 4afe1d0 .. cc6d513 |
| A2 | ovs-rtlpg | the UART at its baud rate | ovs-rtlpg 6d8d57d, e614896 |
| A3 | ovs-rtlpg | the flash busy model and the fetch checker | ovs-rtlpg 0ce53de, 07c6457, 224393b |
| A4 | ovs-rtlpg | coverage and `cov_report.py` | ovs-rtlpg 1de392d, 7ed1d8e |
| A5 | ovs-rtlpg | regression: every existing test passes, the boot transcript as the baseline | `baseline/a5-regression.txt` |
| B1 | RTLPlayground | the generator, the `.def` format, host tests; the 3-byte table is C (decision 16) | `evidence/o1-bank-byte.txt` |
| B2 | RTLPlayground | the core, the rings, the watchdog, the crash record, `health tasks`; the resident/banked split | `evidence/b2-resources.txt` |
| B3 | RTLPlayground | `make schedtest`, dummies in every bank (decision 17) | `evidence/b3-schedtest.txt` |
| B4 | both | the sim tests for schedtest: all pass, deterministic | `evidence/b4-sim-tests.txt` |
| B5 | both | the negative tests, each failing as intended | `evidence/b5-neg-tests.txt` |
| B6 | RTLPlayground | the resource gate | `evidence/b6-gate.txt` |
| C | bench | schedtest on the 8x: timing as in the sim, the record survives, the bank window reads 0x00, the early-reset hang; schedtest then runs the firmware's own init | `silicon.md`, `evidence/c-firmware-init.txt` |

## M2 (D): the scheduler in the real firmware, no behaviour change (done)

| Pkg | What was done | Evidence |
|---|---|---|
| D0a | sim: the NIC RX ring's capacity and drops, an LACP partner, MAC learning | ovs-rtlpg ceb7e0a, fa7c7ed, b24a643 |
| D0b | the old-vs-new harness and the reference scenarios | ovs-rtlpg 2f3b6dd .. bb0ee65, 307c230 |
| D1 | the UART rings in the firmware: RX 256 B, TX ring, drain before reset; old-vs-new 13/13 with explained differences | `evidence/d1-uart-rings.txt`, `d1-oldnew.expect` |
| D2 | the main loop's phases as fixed tasks (`tasks.def` then); the old loop's long steps listed, each with an owner | `evidence/d2-tasks.txt`, `d2-oldnew.expect` |
| D3 | `ctl_buf` for control frames, the console's own line, one cli task, the web command in `config_buf`; U2 and U4 fixed | `evidence/d3-console-net.txt`, `d3-oldnew.expect` |
| DB | bench: the M2 firmware on the 8x; found HOUSE_SEC's 40 ms and the foreign crash record | `silicon.md` |

## M3 (E): break up the blockers (done)

| Pkg | What was done | Evidence |
|---|---|---|
| E0 | M2's leftovers: the crash record carries the image's identity; HOUSE_SEC traced to the boot-count erase | `evidence/e0-leftovers.txt` |
| E1 | cli jobs for the long dumps, the 2 KiB TX ring (decision 21), the web `/cmd` a line a step | `evidence/e1-chunked-cli.txt`, `e1-oldnew.expect` |
| E2 | SFP slot tasks, a transaction a step, and the DDM cache | `evidence/e2-sfp.txt`, `e2-oldnew.expect` |
| E3 | the flash job: upload, config save, factory reset, the boot count; the window stage (decision 20); U9 | `evidence/e3-flash-job.txt`, `e3-oldnew.expect`, `e3-known`; merged with E1: `e1e3-merge.txt`, `e1e3-oldnew.expect` |
| E4 | hardware-poll timeouts, `eee` as a cli job, the VLAN bitmap; U10 | `evidence/e4-polls-vlan-eee.txt`, `e4-oldnew.expect` |
| EH | the harness's new-only checks and `known-failures` (ovs-rtlpg) | ovs-rtlpg d3d1843, 9020430 .. 83efa85 |
| E5 | the explicit step API: steps return their state (22), direct wakes and an 8-bit hardware mask (23), inline `TASK`/`STEP` (24) | `evidence/e5-step-api.txt` |
| ES | code space measured; SDCC's loop optimizations off per file (Michael's choice) | `evidence/es-code-space.txt`, `es-loop-flags.txt` |
| EB | bench: the M3 firmware on the 8x; found the slower upload and the 18 ms config-POST step | `silicon.md` |

## M4 (F): validation (in progress)

| Pkg | Work | Needs |
|---|---|---|
| F1 | full acceptance in the sim, both boards: old-vs-new on all 17 scenarios with the final expect file; the new-only checks; no step > 20 ms but `FL_ERASE` (`known-failures` shrunk to what's left, each with its owner); a 500-character paste; the gate; coverage ⊇ the old build's by function, minus the replaced; a 10-minute schedtest soak. One report: `evidence/f1-acceptance.txt` | - |
| F1b | EB's follow-ups in the sim and the firmware. Done: an upload's window is the stage's free space, not one MSS (O9; 58c1601), its right edge kept from moving left when a job ends during a multipart preamble (flash_job_steps S7); `vlan_setup`'s table scan ends at its first timeout, a sim test bounding the boot (`hw_timeouts.py boot`; 3effe95); `cli_web_live()` without a BSEG bit (7b26aa3); the step limit is 50 ms (decision 25; 8e52aaf). Moved to the object-model project by decision 25: the config POST's RX_STEP (18 ms on the 8x) and `/vlanlist.json` with 40 named VLANs (backed out in d31f91c; modules.md) | - |
| F2 | bench: LACP on the 8x against a Linux bond (aggregate, fast timeout, member flap, `l2` of 1000 MACs and an upload during it); the L2 flush and `SFP_CONFIG` on silicon; upload timing per segment. Then the 4+2 (RTL8372, web only), only with Michael's go | F1, F1b |
| F3 | a 24 h soak on the 8x: no watchdog reset or crash record, no LACP member lost, no overruns but `FL_ERASE`, no hw timeouts, stack headroom ≥ 30 B | F2 |
| F4 | the estate, in Michael's rollout order, each switch with his go: a config backup, the install image, a watch period, the way back ready | F3 |
| FB | this book, from plan to as built | - |

## Independent of the phases

Bugs found on the way, each a small upstream PR when Michael says:
upstream-bugs.md. The audit's CRC routine leaving `PSBANK` at 1 (U5) was
already fixed on fork main (67d09dd, in 612116e).
