# 17. Silicon bring-up (phase C)

The schedtest image on the 8x (KeepLink 9XHPML V1.2, RTL8373N, W25Q16, 2 MiB),
2026-11-19, driven over the serial console from the bench host with
`schedtest/silicon.py`. Logs: `~/rtlpg-bench/sched-c/` on devel (`c-base.*`
without switch init, `si-base.*` and `si-hang.*` with it).

Images: base and `ST_NEG=hang`, LOADER=1, compact, firmware 567ce3f (switch
init) and c6bd58c (the first run, no switch init). All results below: **0
failures**.

## Scheduler timing: matches the simulator

Figures from the two logged runs, `c-base.json` (no switch init) and
`si-base.json` (with it), as "first / second":

| Check | Silicon | Sim (B4) |
|---|---|---|
| sleep 5 / 37 / 1000 ms, never early | shortest gaps 9.65 & 9.99 / 44.46 & 44.59 / 1003.6 & 1003.9 ms | 9.99 / 44.7 / 1004.0 ms |
| late by at most a pass | yes; longest pass 1.92 / 1.81 ms (quiet) | yes |
| post → waiter | 571 / 639 µs | 547 µs |
| 100 ms timeouts | 103.9-105.1 / 104.1-105.1 ms | 104.3-105.0 ms |
| hot's worst gap | 651 / 596 µs (bound 1223) | 332 µs |
| jobs: 11 dynamic slots, then `SCHED_NONE`, all end | yes, two rounds, both runs | yes |
| printer: 3000 lines, none lost; longest step | 8.5 s; 387 / 381 µs | 356 µs |
| paste 501 B, quiet / across a ~20.1 ms step | 501/501, 0 overruns, both runs | same |
| `slow 30`: one overrun, `SL_SLOW` named | 30.2 / 30.1 ms | 28 ms |
| stack headroom under `deep 9` + console | 63 B (high-water 0xc0), both runs | 61 B |

The 5 ms sleeper runs every 10 ms on both: `task_sleep` adds a tick so a wait
is never early. That is right for one-off waits, but a **periodic task must
use `task_until(last + period)`**, or STP's 20 ms timer would become 25 ms.
M2 (D2) applies this to every periodic task.

## Flash

| Operation | Silicon | Sim, A3 model | Sim now (0d03a2a) |
|---|---|---|---|
| sector erase (4 KiB) | 40.2 / 41.6 ms | 40 ms | 40.0 ms |
| `flash_write_bytes`, 4 B | 55 / 49 µs | 98 µs | 43 µs |
| `flash_write_bytes`, 256 B (64 commands) | 1.71 / 1.71 ms | 5.0 ms | 1.64 ms |
| flasher steps: program 128 B / verify | 1.1 / 1.4 ms | 2.7 / 1.4 ms | |

The per-command overhead was lower than A3 modelled (~27 µs per 4-byte
command in all). The firmware's 4-byte writes are fine; page programming is
not needed. The old "1.9 ms" figure was one 256-byte `flash_write_bytes`.
rtlsim now charges 1 µs per controller command and 10.5 + (n-1) µs per
program (ovs-rtlpg 0d03a2a); its 4-byte write stays ~6-12 µs short of the
8x's.

**During an erase, the bank window reads 0x00**: 64 of 64 spread bytes of
bank 2, on two runs, while the idle read gives the image's code. rtlsim
returned 0xFF and now returns 0x00 (0d03a2a). The earlier probe
(hardware.md) saw only some reads wrong, with a different read pattern;
either way, banked code must not run while the flash is busy.

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
- rtlsim did not model it, so B5 passed in the sim. It now does, scoped to
  this evidence (Michael's call, revisiting his f849c0c): a reset made by
  the application before its switch init (proxy: a 0x632c write with bits
  16-20 set, since power-on) holds the CPU until a power cut and reports
  `early-reset` (exit 7); a loader reset still boots (unconfirmed on
  silicon). On in control mode, off in plain mode (ovs-rtlpg 26830b7,
  05424fe). The exact silicon condition is unknown.

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
- schedtest confirms its boot to the loader 15 s after the scheduler starts
  (st_b2.c), ~26 s from power-on with the switch init, not 15 s from boot as
  the firmware does (rtlplayground.c). Before it confirmed at all, every
  power cycle counted as unconfirmed, and the third reached the recovery
  prompt. Even with it, three resets inside the window still do: in the
  hang run, the `reset` case (after two watchdog resets and a ^R ^R, each
  ~13 s apart) booted through "Recovery boot" and the app then used its
  default configuration (`si-hang.log`). The record survived and printed
  all the same. For M2: a crash loop faster than the confirm lands in the
  loader's recovery after three, which is the intended safety net, but a
  test that resets repeatedly must wait out the window.
- With the switch init, the fabric forwards, and the 8x's fronts 5-6 are
  looped by a patch cable: schedtest runs no loop detection. No storm was
  seen (the bench host saw no packets), but a schedtest with the switch init up is a
  loop risk on this rig.
- The 8x is on fcf16cc again after the run.
