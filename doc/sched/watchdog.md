# 11. Software watchdog and crash record

## Watchdog

There is no hardware watchdog (chapter 2). A software one in the tick ISR
was proven on silicon (a hung main loop reset 5.0 s later):

```c
/* isr_timer2, after the tick: SWDT_ISR(pushes) from crash.h */
if (swdt_on && !--swdt_cnt) {
	crash_cause = CRASH_SWDT;
	crash_pc_sp = SP - pushes;     /* where the interrupted PC is */
	__asm__("ljmp _crash_die");    /* fills the record, resets; never returns */
}
```

- A jump, not a call: any call in an ISR makes SDCC save every register on
  every entry. `pushes` is the ISR prologue's push count (8 in isr_timer2,
  8 in isr_serial since E1's 16-bit TX indexes, 7 before); `tools/isr_frame.py` reads it from the
  `.asm` and checks the number in the source.
- `swdt_kick()` at each pass start sets `swdt_cnt = 1000`.
- **Not in `con_putc()` any more (E1).** Until E1 a wait for room in the TX
  ring that ended kicked it, because one unconverted dump (`l2`, ~3.1 ms a
  MAC) could print for longer than 5 s in one step. Since E1 the dumps that
  grow with a table are cli jobs, a line a step, and the TX ring is 2 KiB
  (decision 21), so a step waits on the UART only if it prints more than
  the free ring: a command starts with at least 1 KiB free (`CLI_ROOM`), a
  job's line with 100 B. To reach 5 s a step would have to print ~57 KB
  (5 s / 87 µs) in one go. The largest unchunked outputs are a few hundred
  bytes: with all the 8x's ports up, STP on, four LAGs and every port named
  31 characters, `vlan show` 465 B, `storm` 441, `stp status` 303, `port N`
  197 (`stat`'s 576 B is a job); evidence/e1-chunked-cli.txt.
  So a loop that prints for ever is now caught at 5 s like any other hang.
- **Limit: 5 s** (1000 ticks). It must be far above the step limit (20 ms)
  and the flash exception (40 ms), and below LACP/STP's patience only by
  accident: the watchdog catches hangs, not slow steps.
- On from the first pass of the scheduler; off during boot (a reset mid-boot
  has hung boards, `rtl837x_sfr.h`).
- It can't catch a hang with interrupts off or inside an ISR at the tick's
  priority. Nothing on this chip can.

## Crash record

At a fixed xdata address, **0xbfe0** (the last 32 bytes of the 48 KiB;
survives a chip reset, chapter 2), outside every XSEG: the app's ends at
0x41c8 (0x43e4 with B2 linked in), the loader's at 0x4f6a. **32 bytes:**

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | magic `0x5c4d` |
| 2 | 1 | cause: 1 software watchdog, 2 `^R ^R`, 3 `reset` command, 4 update |
| 3 | 1 | resets since power-on (no magic before → 0) |
| 4 | 2 | tick at the reset |
| 6 | 1 | running slot (`sched_cur`) |
| 7 | 1 | its state |
| 8 | 2 | tick when that step started |
| 10 | 1 | SP |
| 11 | 1 | PSBANK |
| 12 | 2 | PC of the interrupted code (from the stack in the ISR) |
| 14 | 8 | last 8 states run (ring) |
| 22 | 1 | ring index |
| 23 | 1 | spare |
| 24 | 4 | the image that wrote it: `CRASH_IMAGE_LO`, `_HI` (E0) |
| 28 | 2 | spare |
| 30 | 2 | 16-bit sum of bytes 0-29 |

- `CRASH_NOTE(slot, state)` (in the loop, before each dispatch) keeps the
  slot, state, start tick and ring current: a few xdata writes, ~2 µs.
- `crash_die()` (resident) fills the rest and the sum and resets. The tick
  ISR and the `^R ^R` path jump to it; the `reset` command and an update
  call `crash_reset(cause)`.
- "Resets since power-on" counts recorded resets: the boot that consumes a
  valid record carries its count + 1 into the next record.
- **Boot:** `crash_boot()`: if the magic and sum check out, print one line
  (`Last reset: watchdog in stp/STP_SECOND after 5000 ms, PC 0x1234 bank 2,
  states ...`, the last 8 states oldest first), copy it for `health`, then
  clear the magic. A power-on finds garbage: the magic or sum
  fails, and the record is ignored.
- **Another image's record** (E0; found on silicon in DB: a schedtest
  record survived a web update and was decoded with the firmware's slot and
  state names) is dropped unread. `crash_die()` stores the build's identity,
  32 bits of a SHA-1 over the version (`v0.1.0-<hash>[-dirty]`), the build
  date and the build directory (machine, layout, schedtest and its
  variants), which `sched.mk` writes per build into `build_id.h`;
  `crash_boot()` takes a record only if it matches, else clears the magic.
  So a record from another commit, another board, schedtest, or another
  build of the same commit is not printed. `tools/crash_foreign.py` checks
  it in rtlsim; `test_sched` on the host.
- The loader must not use the address (its XSEG ends at 0x4f6a; the address
  goes above both). The B5 test checks the record survives a reset through
  the loader.
