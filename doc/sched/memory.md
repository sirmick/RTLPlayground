# 12. Memory

## The four spaces (KeepLink 9XHPML, compact LOADER=1, fork main 612116e)

Baseline: `baseline/resources-8x.txt`. Scheduler share: measured in B2 with
the B2 code linked in (`evidence/b2-resources.txt`).

| Space | Size | Used | Free | Scheduler's share |
|---|---|---|---|---|
| internal RAM | 256 B | data 105 B, overlay 8, bits 3+1; stack 131 B, peak seen ~79 | **0**; ~50 B of stack headroom | **0 bytes** static. Stack: ~9-10 B more per step than today's direct call: r5-r7 kept pushed across the dispatch (+3), sched_pass's lcall (+2), sched_run's banked frame (+3-4), and the worst ISR frame stays 10 (isr_timer2's; the serial ISR pushes 8 + 2 since E1's 16-bit TX indexes, 7 + 2 before). B4 measures it (schedtest's stack paint, `deep`) |
| xdata | 48 KiB | 16.8 KiB (0x0001-0x41c8) | ~31 KiB | 540 B at B2, +48 B for the slots' `arg` (decision 18); crash record at 0xbfe0 |
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
4. Step functions take no parameters (`uint8_t f(void)`, the return is the
   next state); per-instance data comes from the slot's `task_arg()` or
   from `sched_cur` (an index into the module's xdata).
5. ISRs set `__bit` flags only; no new bits beyond the event sources in
   chapter 6.
6. No new `__bit` variables elsewhere (bit space is internal RAM too).

## The resource gate (B6)

`tools/resource_gate.py BUILDDIR [--baseline FILE] [--update]`:

- **internal RAM:** data, overlay and bits, counted from the `.mem` map.
  (The `.map`'s DSEG size is the area's span from 0, 128, not its use, 105.)
  - **Fail** if any of them grew against the baseline. The message lists
    every new DSEG symbol by name and kind (sloc, PARM, local).
  - Growth that is meant needs the baseline updated in the same commit:
    `--update` rewrites it from the build.
  - New DSEG symbols are listed even when the total didn't grow.
- **Stack:** the `.mem`'s stack size. **Fail** below 120 B.
- **Common area:** the end address from the `.map` (XINIT's end). **Fail**
  above 0x3f80.
- **Banks:** each bank's use and free bytes. **Warn** above 47 KiB.
- **Baselines** (JSON, in `baseline/`):
  - `gate-8x.json`: the 8x firmware, taken from A0's build; equal to
    `resources-8x.txt`.
  - `gate-schedtest.json`: schedtest.
- **Where it runs:**
  - every `make schedtest` build, after the image (a FAIL fails the build);
  - `make gate MACHINE=... [LAYOUT=compact]` on a firmware build (with the
    8x baseline when MACHINE is the 8x);
  - `make -C test quick`: `test_resource_gate` checks the gate on fixtures
    (a real SDCC spill, `test/gate/spill.c`; a common-area overflow; a short
    stack), then gates the 8x schedtest and firmware builds in `output/`
    when they exist.
- At runtime, B4 reads the stack high-water (schedtest's paint,
  `st_stack_hw`) and fails if headroom < 30 B.

## xdata map additions

| What | Bytes | Where |
|---|---|---|
| task table | 144 (24 x 6 B: the mask is 8 bits since E5, decision 23) | XSEG |
| scheduler globals, per-state stats (~40 states × 3) | ~140 | XSEG |
| TX ring / RX ring growth | 2048 / +240 (TX 2 KiB, 16-bit indexes, decision 21, E1: 128 B until then; RX 256 B from 16) | XSEG |
| web command buffer | 0: `config_buf`, with uIP's one connection (console.md); its own buffer (up to 4480 B, today's `/cmd` body limit) only if connections ever grow above 1, or smaller with a deliberate, tested limit (E phase) | XSEG |
| control-frame TX buffer | 268 (`RTL_FRAME_DESC_SIZE` 12 + `CTL_FRAME_MAX` 256 = `LLDP_MAX_FRAME`; LLDP the largest, its worst case bounded in `rtl837x_lldp.c`) | XSEG |
| upload stage | 0: `config_buf`, with uIP's one connection (net.md item 1, as the web command buffer); the window is its free space (decision 20). E3 adds 57 B of xdata: the flash job's request and state, the stage's bookkeeping, `uip_rcv_wnd`, net's due tick | XSEG |
| crash record | 32 | fixed at 0xbfe0, outside every XSEG (chapter 11). Survival across a reset was measured at 0xbf00 (chapter 2); B5 confirms 0xbfe0 |
