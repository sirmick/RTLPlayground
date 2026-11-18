# 11. Software watchdog and crash record

## Watchdog

There is no hardware watchdog (chapter 2). A software one in the tick ISR
was proven on silicon (a hung main loop reset 5.0 s later):

```c
/* isr_timer2 */
if (swdt_limit && ++swdt_cnt >= swdt_limit) {
	crash_save(CRASH_SWDT);   /* fills the record from xdata, no calls into banks */
	CHIP_RESET_NOW();
}
```

- `swdt_kick()` at each pass start sets `swdt_cnt = 0`.
- **Limit: 5 s** (1000 ticks). It must be far above the step limit (20 ms)
  and the flash exception (40 ms), and below LACP/STP's patience only by
  accident: the watchdog catches hangs, not slow steps.
- On from the first pass of the scheduler; off during boot (a reset mid-boot
  has hung boards, `rtl837x_sfr.h`).
- It can't catch a hang with interrupts off or inside an ISR at the tick's
  priority. Nothing on this chip can.

## Crash record

At a fixed xdata address ≥ 0x5000 (survives a chip reset, chapter 2),
outside every XSEG. **32 bytes:**

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
| 23 | 7 | spare |
| 30 | 2 | 16-bit sum of bytes 0-29 |

- `crash_note(slot, state)` (in the loop, before each dispatch) keeps the
  slot, state, start tick and ring current: a few xdata writes, ~2 µs.
- `crash_save(cause)` fills the rest and the sum. Called by the tick ISR,
  the `^R ^R` path and the `reset` command.
- **Boot:** if the magic and sum check out, print one line (`Last reset:
  watchdog in stp/STP_SECOND after 5.0 s, PC 0x1234 bank 2`), copy it for
  `health`, then clear the magic. A power-on finds garbage: the magic or sum
  fails, and the record is ignored.
- The loader must not use the address (its XSEG ends at 0x4f6a; the address
  goes above both). The B5 test checks the record survives a reset through
  the loader.
