# 12. Memory

## The four spaces (KeepLink 9XHPML, compact, fork build fcf16cc)

| Space | Size | Used | Free | Scheduler's share |
|---|---|---|---|---|
| internal RAM | 256 B | everything outside the stack; stack 131 B, peak seen ~79 | **0**; ~50 B of stack headroom | **0 bytes** |
| xdata | 48 KiB | 16.8 KiB (0x0001-0x41c3) | ~31.8 KiB | < 1 KiB |
| common area (code RAM) | 16 KiB | to 0x396a | ~1.7 KiB | core ~0.5 KiB + `step_addr` (3 B per state) + `run_seq` |
| banks (compact) | 3 × 48 KiB | 45.4 / 46.1 / 46.0 KiB | ~10 KiB total | steps; state names |

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

- **internal RAM:** DSEG + OSEG + bits from the `.map`, and the stack size
  from the `.mem`. Fail if the stack drops below 120 B, or if DSEG grew
  against the baseline without the baseline being updated in the same commit.
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
| crash record | 32 | fixed, ≥ 0x5000, outside XSEG |
