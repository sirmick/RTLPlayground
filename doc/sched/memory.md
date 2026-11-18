# 12. Memory

## The four spaces (KeepLink 9XHPML, compact LOADER=1, fork main 612116e)

Baseline: `baseline/resources-8x.txt`. Scheduler share: measured in B2 with
the B2 code linked in (`evidence/b2-resources.txt`).

| Space | Size | Used | Free | Scheduler's share |
|---|---|---|---|---|
| internal RAM | 256 B | data 105 B, overlay 8, bits 3+1; stack 131 B, peak seen ~79 | **0**; ~50 B of stack headroom | **0 bytes** static. Stack: ~9-10 B more per step than today's direct call: r5-r7 kept pushed across the dispatch (+3), sched_pass's lcall (+2), sched_run's banked frame (+3-4), and the worst ISR frame 10 -> 11 (the serial ISR's rings). B4 measures it (schedtest's stack paint, `deep`) |
| xdata | 48 KiB | 16.8 KiB (0x0001-0x41c8) | ~31 KiB | 540 B; crash record at 0xbfe0 |
| common area (code RAM) | 16 KiB | to 0x396a | 1558 B to the 0x3f80 gate | 1378 B with modules.md's task set: core 606, crash 266, rings 149, tables 172 (3 B per state + `run_seq`), ISRs +134, start-up +42 |
| banks (compact) | 3 × 48 KiB | 46188 / 46202 / 45565 B | 8.5 KiB total | measurement, init, idle, health, names: ~3.4 KiB |

## Why internal RAM is the limit

SDCC (non-reentrant) gives each non-leaf function's parameters, locals and
register spills ("sloc") fixed internal-RAM addresses, for good. Only leaf
functions share the 8-byte overlay area. Every new function with a spill
costs bytes. In this project's probe work, one 32-bit compare (4 bytes)
broke the link three times.

## Rules

1. Scheduler and step data: `__xdata`, always.
2. Step locals that would spill: `static __xdata`.
3. 8- and 16-bit arithmetic only on scheduler and step paths. A 32-bit
   compare can be done by 16-bit halves.
4. Step functions take no parameters (`void f(void)`); per-instance data
   comes from `sched_cur`.
5. ISRs set `__bit` flags only; no new bits beyond the event sources in
   chapter 6.
6. No new `__bit` variables elsewhere (bit space is internal RAM too).

## The resource gate (B6)

`tools/resource_gate.py BUILDDIR [--baseline FILE]`, run on every build of
the test target:

- **internal RAM:** data, overlay and bits counted from the `.mem` map (the
  `.map`'s DSEG size is the area's span from 0, 128, not its use, 105), and
  the stack size from the `.mem`. Fail if the stack drops below 120 B, or if
  the data grew against the baseline without the baseline being updated in
  the same commit.
- **Common area:** the end address from the `.map`. Fail above 0x3f80.
- **Banks:** each bank's use. Warn above 47 KiB.
- **Spills:** list every new `sloc` and `PARM` symbol in DSEG against the
  baseline (the script `/tmp/ds-*.txt` diff from the probe work, made
  permanent).
- At runtime, the sim test reads the stack high-water (`health`'s paint) and
  fails if headroom < 30 B.

## xdata map additions

| What | Bytes | Where |
|---|---|---|
| task table | 120 | XSEG |
| scheduler globals, per-state stats (~40 states × 3) | ~140 | XSEG |
| TX ring / RX ring growth | 128 / +48 | XSEG |
| web command buffer | 128 | XSEG |
| control-frame TX buffer | 128 | XSEG |
| upload stage | ~1,500 | XSEG |
| crash record | 32 | fixed at 0xbfe0, outside every XSEG (chapter 11). Survival across a reset was measured at 0xbf00 (chapter 2); B5 confirms 0xbfe0 |
