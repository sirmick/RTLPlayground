# 17. Silicon bring-up (phase C)

The schedtest image on the 8x (KeepLink 9XHPML V1.2, RTL8373N, W25Q16, 2 MiB),
2026-11-19, driven over the serial console from the bench host with
`schedtest/silicon.py`. Logs: `~/rtlpg-bench/sched-c/` on devel (`c-base.*`
without switch init, `si-base.*` and `si-hang.*` with it).

Images: base and `ST_NEG=hang`, LOADER=1, compact, firmware 567ce3f (switch
init) and c6bd58c (the first run, no switch init). All results below: **0
failures**.

## Scheduler timing: matches the simulator

| Check | Silicon | Sim (B4) |
|---|---|---|
| sleep 5 / 37 / 1000 ms, never early | shortest gaps 9.99 / 44.8 / 1003.8 ms | 9.99 / 44.7 / 1004.0 ms |
| late by at most a pass | yes; longest pass 1.95 ms (quiet) | yes |
| post → waiter | 565 µs | 547 µs |
| 100 ms timeouts | 104.5-104.7 ms | 104.3-105.0 ms |
| hot's worst gap | 455 µs (bound 1223) | 332 µs |
| jobs: 11 dynamic slots, then `SCHED_NONE`, all end | yes, two rounds | yes |
| printer: 3000 lines, none lost; longest step | 8.5 s; 381 µs | 356 µs |
| paste 501 B, quiet / across a 20.1 ms step | 501/501, 0 overruns | same |
| `slow 30`: one overrun, `SL_SLOW` named | 30.1 ms | 28 ms |
| stack headroom under `deep 9` + console | 63 B (high-water 0xc0) | 61 B |

The 5 ms sleeper runs every 10 ms on both: `task_sleep` adds a tick so a wait
is never early. That is right for one-off waits, but a **periodic task must
use `task_until(last + period)`**, or STP's 20 ms timer would become 25 ms.
M2 (D2) applies this to every periodic task.

## Flash

| Operation | Silicon | Sim model (A3) |
|---|---|---|
| sector erase (4 KiB) | 40.2-41.6 ms | 40 ms |
| `flash_write_bytes`, 4 B | 49-55 µs | ~70 µs |
| `flash_write_bytes`, 256 B (64 commands) | 1.71 ms | ~4.5-5.8 ms |
| flasher steps: program 128 B / verify | 1.1 / 1.4 ms | 2.7 / 1.4 ms |

The per-command overhead is lower than modelled (~27 µs per 4-byte command
in all). The firmware's 4-byte writes are fine; page programming is not
needed. The old "1.9 ms" figure was one 256-byte `flash_write_bytes`.
**To do (sim):** lower the controller's per-command cost so 256 B costs
~1.7 ms.

**During an erase, the bank window reads 0x00**: 64 of 64 spread bytes of
bank 2, on two runs, while the idle read gives the image's code. rtlsim
returns 0xFF. The earlier probe (hardware.md) saw only some reads wrong, with
a different read pattern; either way, banked code must not run while the
flash is busy. **To do (sim):** return 0x00.

## Resets, the watchdog and the crash record

**A reset before the switch init hangs the chip.** The first images skipped
the switch init (no NIC reset, SMI, PHY/SerDes set-up). On them, every reset
(^R ^R, the `reset` command; the crash record at 0xbfe0 or at 0x8000) reset
the chip and then nothing ran, not even the loader's banner, until a power
cycle. This is the hang `rtl837x_sfr.h` records on a SWTG024AS ("a reset
landing mid-boot ... until its power was cycled"), now seen on the RTL8373
too, and it is not about the scheduler. schedtest now runs the firmware's own
init (`firmware_init()`, 567ce3f), and resets work. Consequences:

- The software watchdog must stay off until the switch init is done
  (watchdog.md already says off during boot); a watchdog reset during the
  boot would hang the board instead of resetting it.
- rtlsim did not model it, so B5 passed in the sim. It now models it
  (`early-reset`, a conservative proxy for "init done"); the exact silicon
  condition is unknown.

With the switch init, on silicon:

| Case | `Last reset:` line after the reboot through the loader |
|---|---|
| `hang` (watchdog), twice | `watchdog in slow/SL_HANG after 5000 ms, PC 0xfdb8 bank 3` (and 0xfdb4); both inside `sl_hang` (0xfda6+241) |
| ^R ^R | `^R ^R in con/CON_EDIT after 0 ms, PC 0x328e bank 1` (in `_sched_ticks`) |
| `reset` | `reset command in cli/CLI_RUN after 0 ms bank 1` |

The record at **0xbfe0 survives a chip reset through the loader** (the B2
review's open point). From `hang` to the line: 13.5 s (5 s + loader + init).

## Bench notes

- The loader's XMODEM takes at most 1856 blocks (237,568 B): send the app
  image, not the install image. An install image's first 237,568 bytes are
  its app image.
- schedtest confirms its boot to the loader at 15 s; before it did, every
  power cycle counted as unconfirmed, and the third reached the recovery
  prompt.
- With the switch init, the fabric forwards, and the 8x's fronts 5-6 are
  looped by a patch cable: schedtest runs no loop detection. No storm was
  seen (the bench host saw no packets), but a schedtest with the switch init up is a
  loop risk on this rig.
- The 8x is on fcf16cc again after the run.
