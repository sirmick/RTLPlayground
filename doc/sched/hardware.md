# 2. Hardware facts the design rests on

All measured on a KeepLink 9XHPML (RTL8373) on the bench, 2026-11-18, with
the probe build `scratch/irq-probe` unless marked otherwise. Details:
`hw-research/README.md`; the SFR side is folded into `doc/sfr.md`
(`doc/sfr-map`).

## CPU and timers

- DW8051 at **125 MHz**, 4 clocks per machine cycle. Measured: Timer 2's
  200 Hz tick ran 60.0279 s in 60.027 s of wall clock.
- **Timer 2** is the 200 Hz tick (`isr_timer2`), auto-reload at clk/12.
  Between ticks its count runs from 0x34ad to 0xffff: **a free 96 ns clock**.
  `fine_now()` (ticks + TH2/TL2, overflow-corrected) costs ~6 µs.
- **Timer 0** is unused. **Timer 1** is the UART baud generator.
- **The PTP clock** (48-bit seconds + ns) runs at real rate once started
  (#879's sequence). Not needed by the scheduler; noted as a spare timebase.

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
| console character | **87 µs** (115200 baud; `write_char` busy-waits today) |
| banked call | under ~1 µs |
| sector erase (4 KiB) | ~35-40 ms |
| page program (256 B) | ~1 ms (datasheet; to confirm). But `flash_write_bytes()` programs 4 bytes per command and waits for each: ~1 ms per 4 bytes in rtlsim, so 256 B take ~66 ms (B3). Silicon to confirm in C |

## Today's longest main-loop pass

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
wrong data, with no stall** (15-16 of 64 checksum passes over 512 spread
addresses were bad, then good again). Repeated reads of one address stay
correct (some read buffer). So **no banked code may run while the flash is
busy**: only the common area (code RAM) and ISRs. That is today's design
too; every erase/program poll loop already lives in `rtl837x_flash.c`,
which is resident.

## Reset

- No hardware watchdog exists: the DW8051 has none, and the switch has none.
- **xdata above the C start-up's clear survives a chip reset.** SDCC clears
  only XSEG (app 0x0001-0x448e, loader 0x0001-0x4f6a); bytes at 0x5000,
  0x8000 and 0xbf00 survived `^R ^R` through the loader. 0x5000-0xbfff can
  carry a crash record across a reset (not across power-off).
- Never write `RST_GLB_CTRL_0` (0x24) bit 4: it holds the 8051 in reset
  until power-off.
