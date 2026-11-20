# 2. Hardware facts the design rests on

Measured on a KeepLink 9XHPML (RTL8373N, W25Q16, 2 MiB) on the bench:
first with the probe build `scratch/irq-probe` (2026-11-18; details in
`hw-research/README.md`, the SFR side in `doc/sfr.md` on `doc/sfr-map`),
then with the schedtest image and the scheduler firmware (phases C, DB, EB;
silicon.md). Where the two disagree, the later run is the one given, and
the earlier is noted.

## CPU and timers

- DW8051 at **125 MHz**, 4 clocks per machine cycle. Measured: Timer 2's
  200 Hz tick ran 60.0279 s in 60.027 s of wall clock.
- **Timer 2** is the 200 Hz tick (`isr_timer2`), auto-reload at clk/12.
  Between ticks its count runs from the reload to 0xffff: **a free 96 ns
  clock**. The scheduler reads it as fine units of 64 counts, 6.144 µs, 813
  to a tick (core.md).
- **Timer 0** is unused. **Timer 1** is the UART baud generator.
- **The PTP clock** (48-bit seconds + ns) runs at real rate once started
  (#879's sequence). Not used by the scheduler; a spare timebase.

## Interrupts

| Vector | Source |
|---|---|
| INT0 | the switch's interrupt tree (link change; any unmasked `ISR_INT_*` bit). Edge-triggered: every pending unmasked bit must be cleared before EX0 is re-enabled |
| INT1 | NIC RX |
| serial 0 | UART RX/TX |
| Timer 2 | tick |
| EX2-EX5, PFI, WDTI | no hardware source; settable by software (10/10 fired) |

Interrupt entry from a software-set flag to the handler: ~1.5 µs.

## Costs of the primitives

| Operation | Cost |
|---|---|
| switch register read / write | ~0.8 / ~3.4 µs |
| SerDes read | ~2.9 µs |
| **MDIO (PHY) read** | **~161 µs** |
| console character | **87 µs** (115200 baud) |
| banked call | under ~1 µs |
| sector erase (4 KiB) | 40.2 / 41.6 ms (C, two runs); 35.5 ms in EB's config save |
| `flash_write_bytes()`, 4 B (one program command) | 49-55 µs (C) |
| `flash_write_bytes()`, 256 B (64 commands) | 1.71 ms (C) |
| I2C transaction (an SFP read, 16 B at 100 kHz) | ~2 ms (estimate, not measured: e2-sfp.txt) |

The firmware programs 4 bytes per command and that is fast enough: ~27 µs
of overhead a command, so no page programming is needed. The old "1.9 ms"
figure was one 256-byte `flash_write_bytes()` (silicon.md).

## The old main loop's longest passes

Fork main 612116e, before the scheduler (the probe build):

| Scenario | Longest pass |
|---|---|
| idle | 5 ms (one pass per tick) |
| web pages, JSON | 8-22 ms |
| CLI `port`/`stat`/`stp`/`sfp` | 17/50/21/20 ms, mostly console output |
| config save | 57 ms |
| **firmware upload** | **1893 ms**, plus 87 passes of 100-500 ms |
| **CLI `l2` with 1000 MACs** | **3156 ms** (~3.1 ms per entry, console-bound) |

## Flash busy

During a sector erase, **memory-mapped reads from the bank window return
0x00**, with no stall: 64 of 64 spread bytes of bank 2, on two runs, while
the idle read gives the image's code (C). The earlier probe, with another
read pattern, saw only some reads wrong (15-16 of 64 checksum passes over
512 addresses), and repeated reads of one address right (some read
buffer). Either way **no banked code may run while the flash is busy**:
only the common area (code RAM) and ISRs. Every erase and program wait
lives in `rtl837x_flash.c`, which is resident (chapter 10).

## Reset

- No hardware watchdog exists: the DW8051 has none, and the switch has none.
- **xdata above the C start-up's clear survives a chip reset.** SDCC clears
  only XSEG; bytes at 0x5000, 0x8000 and 0xbf00 survived `^R ^R` through the
  loader (probe), and the crash record at **0xbfe0** survived every reset
  through the loader in C, DB and EB. Not across a power cut. The app's
  XSEG ends at 0x5109 (fb-build.txt), the loader's at 0x4f6a.
- **A chip reset before the switch init hangs the chip** until power is
  cycled: no loader banner, nothing (C, on images that skipped the NIC
  reset, SMI and PHY/SerDes set-up). `rtl837x_sfr.h` records the same on a
  SWTG024AS. The exact condition is unknown. So the software watchdog stays
  off until the scheduler runs (chapter 11), and rtlsim models the hang,
  scoped to this evidence (chapter 13).
- Never write `RST_GLB_CTRL_0` (0x24) bit 4: it holds the 8051 in reset
  until power-off.
- The loader's XMODEM takes at most 1856 blocks (237,568 B): the app image,
  not the install image (silicon.md).
