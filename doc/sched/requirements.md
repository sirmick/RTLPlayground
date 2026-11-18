# 1. Requirements

## Goals

1. **Cooperative time slicing** between switch upkeep, the console/CLI and
   the web server, so no one of them can starve another.
2. **Bounded steps.** No step runs longer than 20 ms (one documented
   exception: a flash erase, ~40 ms, chapter 10). Target under 5 ms.
3. **Latency by design.** Each task's worst-case latency is set by how
   often it appears in the run sequence times the longest step. Tasks that
   must react fast (receive, link, loop detection, console input) appear
   often.
4. **No behaviour change by itself.** Moving today's main loop onto the
   scheduler (phase D2) must leave every observable result the same; the
   old-vs-new harness proves it (chapter 14).
5. **Observability.** Every task's current state, every state's longest
   step, overruns and stack high-water are visible in `health`; a hang
   leaves a crash record that survives the reset (chapter 11).
6. **Testable in the simulator first.** Every mechanism is exercised in
   rtlsim, deterministically, before it runs on silicon.

## Non-goals

- Preemption, threads, per-task stacks. SDCC's non-reentrant code and 256
  bytes of internal RAM rule them out (chapter 12).
- Rewriting protocols. STP, LACP, LLDP, IGMP keep their logic; only how they
  are called changes.
- The boot path. It stays a straight run (~10 s) until a later project.
- Converting every CLI command. Only long printers are chunked; a short
  command runs in one step, as today.
- Interrupt-driven deferred work (EX4/EX5 software interrupts). Kept in
  reserve; not needed for the first version.

## Deadlines the scheduler must protect

| Consumer | Tolerance | Consequence when missed |
|---|---|---|
| Loop detection (RLDP poll, 1 s) | as small as possible; each ms of delay is ms of storm | broadcast storm until blocked |
| Console RX | ring overflow; 256 B ring = ~22 ms of pasted text at 115200 (decision 19), more than the 20 ms step limit; only the ~40 ms flash erase can lose pasted text | lost characters |
| NIC RX ring | 10 KiB: ~6 full frames, ~150 small ones | the ASIC drops frames, BPDUs and LACPDUs included |
| Link change | users notice > ~0.5-1 s | LAG member or STP port acted on late, traffic black-holed |
| LACP, fast rate | we send every 1 s; partner times out at 3 s | LAG member flaps |
| RSTP | hello 2 s; aged out after 3 missed (6 s) | topology change, flush |
| uIP TCP | RTO 15 ms | retransmits (harmless) |
| STP (legacy), LLDP, DHCP, NTP, ARP | ≥ 20 s | none at these scales |

## Constraints

- **Internal RAM is full.** The scheduler adds 0 bytes of it; all scheduler
  data is xdata (chapter 12).
- **Common area:** ~1.7 KiB free. Only the scheduler core and flash waits go
  there.
- **Code banks (compact layout):** ~10 KiB free over three banks.
- **Banked code must not run while the flash is busy** (it reads garbage on
  silicon, chapter 2).
- **Stick to 8- and 16-bit arithmetic** in scheduler and step code: SDCC
  spills 32-bit temporaries into internal RAM.
- **Fit the project:** cooperative, explicit state machines, no protothread
  macros (#761 deleted them), core code in the common area, PRs split by
  topic (the maintainers' stated preferences, `os-prior-art.md`).
