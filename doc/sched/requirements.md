# 1. Requirements

Each goal with where it stands at 72cee59. "Sim" is rtlsim (chapter 13),
"8x" the bench switch (silicon.md).

## Goals

1. **Cooperative time slicing** between switch upkeep, the console/CLI and
   the web server, so no one of them can starve another.
   **Met.** A 1000-entry `l2` runs as a cli job, a line a step: on the 8x
   1008 lines in 3.15 s over 1966 `CLI_MORE` steps, the longest 429 µs, the
   longest pass 2.0 ms, loop detection polling throughout (silicon.md, EB).
2. **Bounded steps.** No step runs longer than 20 ms; one documented
   exception, a sector erase (`FL_ERASE`: 40 ms in the sim, 35.5-41.6 ms on
   the 8x, chapter 10). Target under 5 ms.
   **Met for the limit in every reference scenario:** in the sim no state's
   longest step is over 20 ms but `FL_ERASE` in any of the 17 old-vs-new
   scenarios, on both boards (evidence/e1e3-merge.txt; the last known long
   step, `/vlanlist.json`'s RX_STEP, went in E4: e4-polls-vlan-eee.txt).
   Every cli state is under 5 ms (`tools/cli_steps.py --web`, e1, e4).
   **Not met everywhere:**
   - over 20 ms outside the scenarios: `/vlanlist.json` with 40 VLANs of
     20-character names, 34 ms in one RX step (was 67-100 ms;
     e4-polls-vlan-eee.txt: the reply's size bounds it);
   - over the 5 ms target: `FL_VERIFY` 14.2 ms and `FL_PROG` 8.1 ms
     (e3-flash-job.txt), a forced poll timeout's step 9.5-16.6 ms
     (modules.md), l2_big's RX_STEP 11.1 ms (es-loop-flags.txt), and on the
     8x RX_STEP 18.2 ms while a config POST arrives (silicon.md, EB; F1b
     owns it). Chapter 9 lists what is left.
3. **Latency by design.** Each task's worst-case latency is set by how
   often it appears in the run sequence times the longest step. Tasks that
   must react fast (receive, link, loop detection, console input) appear
   often.
   **Built:** the firmware's sequence has 72 positions; rx, link, loop and
   con appear 12 times, at most 6 positions apart; stp and lacp 3 times;
   the rest once (evidence/fb-build.txt). Measured on schedtest's `hot`
   task (high): worst gap 332 µs in the sim, 596-651 µs on the 8x, under
   its bound of 1223 µs (silicon.md).
4. **No behaviour change by itself.** Moving the main loop onto the
   scheduler must leave every observable result the same.
   **Met for M2:** old-vs-new matched 13 of 13 scenarios at D1 and D2, 14
   of 14 at D3, on both boards, with every difference in an expect file and
   explained: output no longer waits on the UART, so the firmware is a
   little ahead at fixed times (d1-uart-rings.txt, d2-tasks.txt,
   d3-console-net.txt). M3's differences are intended and listed the same
   way (e1-e4 `*-oldnew.expect`).
5. **Observability.** Every task's current state, every state's longest
   step, overruns and stack high-water are visible; a hang leaves a crash
   record that survives the reset (chapter 11).
   **Met**, with one difference: `health tasks`, built in every firmware,
   shows each slot's state, mask and wake, each state's runs and longest
   step, passes, idle passes, the longest pass, overruns with the last
   one's state, console RX overruns, hardware-poll timeouts with the last
   register, and the last reset's line. The stack's high-water is in bare
   `health`, which only HEALTH=1 builds have, and in the sim's paint scan
   (`tools/sched_tour.py`). The record survived chip resets through the
   loader on the 8x in C, DB and EB (silicon.md).
6. **Testable in the simulator first.** Every mechanism is exercised in
   rtlsim, deterministically, before it runs on silicon.
   **Met:** every package's evidence is from rtlsim (`--twice` runs give
   the same numbers); the bench runs (C, DB, EB) came after (chapter 14).

## Non-goals

- Preemption, threads, per-task stacks. SDCC's non-reentrant code and 256
  bytes of internal RAM rule them out (chapter 12).
- Rewriting protocols. STP, LACP, LLDP, IGMP keep their logic; only how they
  are called changed.
- The boot path. It stays a straight run before the scheduler starts.
- Converting every CLI command. Only dumps that grow with a table, or whose
  work passes 5 ms, are cli jobs; the rest run in one step (decision 21).
- Interrupt-driven deferred work (EX2-EX5 as software interrupts). Kept in
  reserve; not used.

## Deadlines the scheduler must protect

| Consumer | Tolerance | Consequence when missed | As built |
|---|---|---|---|
| Loop detection (RLDP poll, 1 s) | as small as possible; each ms of delay is ms of storm | broadcast storm until blocked | `loop` is high; on the 8x it found the 5-6 cable and blocked port 6 after every reset (DB, EB) |
| Console RX | the ring's length: 256 B = ~22 ms of pasted text at 115200 (decision 19) | lost characters | 501 of 501 pasted bytes taken across a 20.1 ms step; across a 40 ms erase 294 of 501 (accepted, decision 19; b4-sim-tests.txt) |
| NIC RX ring | 10 KiB: ~6 full frames, ~150 small ones | the ASIC drops frames, BPDUs and LACPDUs included | `rx` is high and stays ready while frames wait; the ring's real capacity under a burst is not measured (O5) |
| Link change | users notice > ~0.5-1 s | LAG member or STP port acted on late | `link` is high on INT0; LINK_READ 337 µs on a flap on the 8x (EB) |
| LACP, fast rate | we send every 1 s; partner times out at 3 s | LAG member flaps | sim: 1.011-1.029 s gaps (8x), 1.010-1.030 s (SWGT024) during a 1000-entry `l2`; the old firmware's gap was 3.86 s (e1-chunked-cli.txt). On silicon: F2 |
| RSTP | hello 2 s; aged out after 3 missed (6 s) | topology change, flush | sim: hellos 1.966-2.036 s (8x), 1.968-2.034 s (SWGT024) during an upload; the old firmware was silent 2.84 s (e1e3-merge.txt, e3-flash-job.txt) |
| uIP TCP | RTO 15 ms | retransmits (harmless) | - |
| STP (legacy), LLDP, DHCP, NTP, ARP | ≥ 20 s | none at these scales | - |

## Constraints

- **Internal RAM is full.** The scheduler adds 0 bytes of it; all scheduler
  data is xdata (chapter 12). Held: data 90 B, overlay 8, bits 3 at 72cee59,
  each at or below its baseline (fb-build.txt).
- **Common area:** 1558 B free to the gate at the start (baseline). Only the
  dispatch, the calls every step makes, the crash path, the rings and the
  tables went there; 163 B are left (fb-build.txt).
- **Code banks (compact layout):** 9501 B free over three banks at the
  start (b6-gate.txt); 1231 B now (8x), 1187 B (SWGT024). Every bank is
  over 47 KiB.
- **Banked code must not run while the flash is busy** (it reads 0x00 on
  silicon, chapter 2). rtlsim fails a run that does (chapter 13).
- **8- and 16-bit arithmetic** in scheduler and step code: SDCC spills
  32-bit temporaries into internal RAM. The gate catches a spill (E3's
  first `flash_job.c` did: e3-flash-job.txt).
- **Fit the project:** cooperative, explicit state machines, no protothread
  macros (#761 deleted them), core code in the common area, PRs split by
  topic (the maintainers' stated preferences, `os-prior-art.md`).
