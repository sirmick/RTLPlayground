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
  7 in isr_serial with the 256 B ring's 8-bit indexes); `tools/isr_frame.py` reads it from the
  `.asm` and checks the number in the source.
- `swdt_kick()` at each pass start sets `swdt_cnt = 1000`.
- **Also in `con_putc()`, when a wait for room in the TX ring ends:**
  output still moving is progress, not a hang. Until E1 chunks the dumps,
  one unconverted command can print for longer than 5 s in one step: `l2`
  prints ~35 characters a learned MAC, ~3.1 ms each, so ≥ ~1600 MACs pass
  5 s and a full 4096-entry table takes ~13 s; the same over the web (the
  `/cmd` mirror) and any dump that grows with the configuration. A step that
  hangs without printing still trips it at 5 s. Accepted, until E1 (owner):
  a loop that prints for ever isn't caught.
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
| 2 | 1 | cause: 1 software watchdog, 2 `^R ^R`, 3 `reset` command (4 reserved: see below) |
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

- `CRASH_NOTE(slot, state)` (in the loop, before each dispatch) keeps the
  slot, state, start tick and ring current: a few xdata writes, ~2 µs.
- `crash_die()` (resident) fills the rest and the sum and resets. The tick
  ISR and the `^R ^R` path jump to it; the `reset` command calls
  `crash_reset(cause)`.
- **No record for an update** (E3). Cause 4, "update", is dropped: the reset
  that applies an upload boots the loader, which installs the new image, and
  the record would be read by that image, not the one that wrote it (E0's
  build check ignores it). An update reports itself through the loader's
  own lines ("Installing application", "Installed") and the new version
  string at boot; the reset after an accepted upload stays a plain
  `reset_chip()`.
- "Resets since power-on" counts recorded resets: the boot that consumes a
  valid record carries its count + 1 into the next record.
- **Boot:** `crash_boot()`: if the magic and sum check out, print one line
  (`Last reset: watchdog in stp/STP_SECOND after 5000 ms, PC 0x1234 bank 2,
  states ...`, the last 8 states oldest first), copy it for `health`, then
  clear the magic. A power-on finds garbage: the magic or sum
  fails, and the record is ignored.
- The loader must not use the address (its XSEG ends at 0x4f6a; the address
  goes above both). The B5 test checks the record survives a reset through
  the loader.
