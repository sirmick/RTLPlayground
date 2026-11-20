# 12. Memory

## The four spaces (KeepLink 9XHPML, compact, LOADER=1)

At 72cee59 (evidence/fb-build.txt), against fork main 612116e before the
scheduler (`baseline/resources-8x.txt`, `baseline/gate-8x.json` as A0 took
it):

| Space | Size | Before | Now | Free now |
|---|---|---|---|---|
| internal RAM | 256 B | data 105 B, overlay 8, bits 3; stack 131 B | data 90 B, overlay 8, bits 3; stack 146 B | **0** static; the stack's headroom is measured (below) |
| xdata | 48 KiB | 0x0001-0x41c8 | 0x0001-0x5109, 20745 B | ~27 KiB; the crash record at 0xbfe0 |
| common area (code RAM) | 16 KiB | ends 0x396a, 1558 B to the 0x3f80 gate | ends 0x3edd | 163 B to the gate |
| banks (compact) | 3 × 48 KiB | 46188 / 46202 / 45565 B | 48636 / 49003 / 48586 B | 516 / 149 / 566 B (1231 B) |

SWGT024 now: the same internal RAM and xdata, common area 164 B left, banks
130 / 550 / 507 B free.

**The scheduler's share** (fb-build.txt, from the `.rel` files):

- **Internal RAM: 0 bytes.** No scheduler file has DSEG. Two things sit in
  space that was already there: `sched_bank.c`'s `fine_read()` keeps two
  locals in the shared overlay, and `roll()` (decision 26) its `t`, 2 B
  (leaves both; the overlay stays 8 B), and
  `tasks.c`'s `cli_web_live()` has one bit, SDCC's for its `&&`, in BSEG
  (the bit space stays 3 B). That bit is known: compiler-generated, gate
  unchanged, sched7 to look at it. The gate counts bytes, so it doesn't see
  a new bit that fits an existing byte. Data went down, not up: 105 -> 98 B
  when E3 split `stream_upload()` (its 32-bit spills went), 98 -> 90 B with
  ES's loop flags.
- **Common area:** `sched.c` 618 B, `crash.c` 286, `con_ring.c` 456, the
  tables 171 (3 B per state + `run_seq` + `fixed_first`): 1531 B, plus the
  ring and watchdog macros in the ISRs and E4's `hw_start`, `hw_expired`,
  `hw_timeout`.
- **Banks:** `sched_bank.c` 1651 B, `sched_health.c` 1677, the names 488,
  `tasks.c` 1814, `flash_job.c` 793.

The banks are full: every one is over 47 KiB on both boards. ES measured
the options (evidence/es-code-space.txt); Michael chose SDCC's loop
optimizations off per file (`loopflags.mk`), which gave back 1262 B on the
8x and lowered internal RAM data to 90 B (es-loop-flags.txt).

**Stack.** The dispatch keeps r5-r7 pushed for the step's length (3 B), and
sched_run's and sched_pass's frames sit under every step. Measured
headroom: 72-73 B at the end of the sim's CLI and web tour on the firmware
(`tools/sched_tour.py`, d2-tasks.txt, d3-console-net.txt); on schedtest 61 B
in the sim and 63 B on the 8x under `deep 9` with console input
(silicon.md). The worst ISR frame is 10 B (8 pushes and the return
address). The stack's start has since moved down (0x7d -> 0x6e, 131 ->
146 B) as internal RAM data shrank.

## Why internal RAM is the limit

SDCC (non-reentrant) gives each non-leaf function's parameters, locals and
register spills ("sloc") fixed internal-RAM addresses, for good. Only leaf
functions share the 8-byte overlay area. Every new function with a spill
costs bytes. In the probe work, one 32-bit compare (4 bytes) broke the link
three times; in E4 a `||` cost a bit (79a8ad3 rewrote it as an `if`).

## Rules

1. Scheduler and step data: `__xdata`, always.
2. Locals of functions that call others: `static __xdata`.
3. 8- and 16-bit arithmetic only on scheduler and step paths. A 32-bit add
   can be done by 16-bit halves (`FL_ADVANCE`, chapter 10).
4. Step functions take no parameters (`uint8_t f(void)`, the return is the
   next state); per-instance data comes from the slot's `task_arg()` or
   from `sched_cur` (an index into the module's xdata).
5. No function takes a second parameter on these paths: SDCC puts it in
   internal RAM.
6. ISRs set `__bit` flags only; no new bits beyond the event sources in
   chapter 6. No new `__bit` variables elsewhere (bit space is internal RAM
   too); watch for SDCC's own, as `&&` and `||` can make.

## The resource gate (B6)

`tools/resource_gate.py BUILDDIR [--baseline FILE] [--update]`:

- **internal RAM:** data, overlay and bits, counted from the `.mem` map.
  (The `.map`'s DSEG size is the area's span from 0, not its use.)
  - **Fail** if any of them grew against the baseline. The message lists
    every new DSEG symbol by name and kind (sloc, PARM, local).
  - Growth that is meant needs the baseline updated in the same commit:
    `--update` rewrites it from the build.
  - New DSEG symbols are listed even when the total didn't grow.
- **Stack:** the `.mem`'s stack size. **Fail** below 120 B.
- **Common area:** the end address from the `.map` (XINIT's end). **Fail**
  above 0x3f80.
- **Banks:** each bank's use and free bytes. **Warn** above 47 KiB.
- **Baselines** (JSON, in `baseline/`): `gate-8x.json`, the 8x firmware;
  `gate-schedtest.json`, schedtest. Both were lowered to data 90 B with
  ES's loop flags (27d0ebf).
- **Where it runs:**
  - every `make schedtest` build, after the image (a FAIL fails the build);
  - `make gate MACHINE=... [LAYOUT=compact]` on a firmware build (with the
    8x baseline when MACHINE is the 8x);
  - `make -C test quick`: `test_resource_gate` checks the gate on fixtures
    (a real SDCC spill, `test/gate/spill.c`; a common-area overflow; a short
    stack), then gates the 8x schedtest and firmware builds in `output/`
    when they exist.
- At runtime, schedtest paints the stack and `sim_tests` fails a headroom
  under 30 B; `tools/sched_tour.py` does the same on the firmware.

It has caught: E3's first 32-bit adds in `flash_job.c` (+2 B), and E4's
`||` in `hw_expired()` (bits 3 -> 4) (e3-flash-job.txt,
e4-polls-vlan-eee.txt).

## xdata the scheduler work added

| What | Bytes | Where |
|---|---|---|
| task table | 144 (24 × 6 B; the mask is 8 bits since E5) | `sched.c` |
| the rest of `sched.c`'s data: per-state longest step and runs (29 states × 4 B), the globals | 139 (283 in all with the table) | `sched.c` |
| console rings | 2316 in all: TX 2048, RX 256 (was 16), indexes and counts | `con_ring.c` |
| control-frame TX buffer | 268 (`RTL_FRAME_DESC_SIZE` 12 + `CTL_FRAME_MAX` 256) | `frame_tx.c` |
| web command buffer | 0: `config_buf`, with uIP's one connection (chapter 7) | - |
| upload stage | 0: `config_buf`, the same argument (chapter 8); the flash job's request and state, the stage's bookkeeping, `uip_rcv_wnd`, net's due tick: 58 (e3-flash-job.txt) | `flash_job.c`, `httpd.c`, `tasks.c` |
| SFP caches (E2) | 231 in all: `sfp_id` 2 × 40, `sfp_diag` 2 × 56, the slots' flags and cursors, the helpers' statics | `sfp.c`, `tasks.c` |
| VLAN bitmap (E4) | 512 (`vlan_bits`) + 3; the eee job 8; the poll bounds 6 | `rtl837x_port.c`, `rtlplayground.c` |
| crash record | 32, fixed at 0xbfe0, outside every XSEG (chapter 11) | `crash.c` |
