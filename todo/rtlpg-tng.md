# rtlpg-tng: one firmware for every board

Status: **pencilled in for design**, 2026-11-21. Nothing built yet. The
evidence below is from main (0fc9e38) and rtlsim (ovs-rtlpg); none of it
has run on silicon.

## The idea

One firmware image for every RTL8372/RTL8373 board with 512 KiB of flash or
more. The board's differences become runtime data, not a compile-time
`make MACHINE=`. It is shipped in a few wrappers, one per starting point,
around the same build.

The upstream alpha (v0_1_alpha, d48235f) shipped this idea once: no board
files at all, the chip ID read at boot, one compile-time switch (`NSFP`,
whether port 5 is copper or a second SFP+ cage). Upstream since went the
other way, to 86 compile-time board files. rtlpg-tng takes the alpha's
runtime approach, upstream's board data, and this fork's install and
config work (compact layout, the loader, the object model).

## Decisions so far

- **Compact is the only layout.** A first install on a standard-layout
  switch converts it and brings the loader.
- **First install on a 512 KiB board is by clip.** Upstream can't stage a
  web upload there (its upload area is at 0x80000), and stock firmware
  can't take it either. After that, rtlpg-tng to rtlpg-tng is a web update.
- **The board's identity is in the config** (a field such as `sys.board`),
  so rtlpg-tng to rtlpg-tng keeps it with everything else.
- **No config is carried from any other firmware.** It is radically
  different; the offline converter (tools/rtlpg_client.py) covers that.
- **The loader is installed with the first rtlpg-tng.** Updating it later
  is a separate, optional step (the web UI's Loader card); an app update
  never touches it.
- **Fork only.** Upstream's machine model is compile-time and will stay so.

## Evidence

- **Four board shapes.** The 66 board files rtlsim has (one per machine):
  29 RTL8372 4 copper + 2 SFP+, 27 RTL8373 8 copper + 1 SFP+, 8 RTL8372
  5 copper + 1 SFP+, 2 RTL8372 4 copper + 2 x 10G copper. What really
  differs is wiring: pins, LEDs, port order.
- **A wrongly configured board still comes up.** A generic image for the
  right chip (DEFAULT_8C_1SFP on RTL8373, the SWGT024AS image on RTL8372)
  booted on all 66 board files, and each answered ARP, ICMP and a TCP SYN
  to the web server on its first copper port (rtlpg-tests net/test_nic.py
  --board; the sweep takes 10 s).
- **What breaks it is the compile-time port range.** The RTL8373 image
  works on RTL8372 boards; the RTL8372 image on an RTL8373 board doesn't
  answer at all, because `MACHINE_MIN_PORT`/`MAX_PORT` leave out its first
  ports.
- **Code space.** Free in the three code banks, compact, help and debug in:
  8x 10,640 B, DEFAULT_8C_1SFP 10,633 B, SWGT024AS 10,591 B, the PoE board
  8,589 B (PoE costs ~2 KB). Both chips' code is already in every build.
  A release build without `OM_DBG` gets back ~3.8 KB.
- **Compile-time coupling is small.** About 60 uses outside machine/,
  nearly all `MACHINE_MIN_PORT`/`MAX_PORT` port-loop bounds (LACP, RLDP,
  STP, IGMP, LLDP, LEDs); plus `MACHINE_POE`, the board name (LLDP, the
  version string, #876's upgrade check) and 20 per-board `_init.c` files of
  1-5 LED register writes each.

## What can be detected, and what can't

| Detectable at boot | Not detectable |
|---|---|
| the chip and N variant (done today) | front-panel port order |
| flash size and part (JEDEC, done today) | LED wiring (wrong is only cosmetic) |
| the RTL8224 behind an RTL8373's first ports | the reset button pin (a wrong guess resets or factory-resets) |
| an external PHY on a SerDes (its ID over MDIO): copper port, or an SFP cage (the alpha's `NSFP`) | the SFP cages' GPIOs (detect, LOS, TX disable) and I2C bus |
| a PoE controller on I2C | whether the flash works in dual I/O |

The chip plus a PHY probe tells the four shapes apart; only the wiring
needs the board picked.

## Phases

1. **Runtime chip and port range; the `_init.c` files as data** (register,
   set, clear). Mechanical. Its test is the sweep above: one image per chip
   on every board file.
2. **Board identity in the config and the board table.** A generator makes
   a blob of every board's data from machine/*.c, in flash outside the code
   banks (as om's help blob is); the config gains the board field; the boot
   reads it before the ports are set up; the board name comes from the
   table.
3. **Safe mode when no board is set.** The chip's own copper ports only;
   SFP and 10G ports off; no reset pin; GPIOs left as inputs; single-I/O
   flash; a board picker in the web UI and the CLI, filtered by what was
   detected.
4. **The wrappers.** The chip image (clip), the OEM upgrade (installer/,
   the Realtek header), the bridge from upstream, the install image from
   this fork's earlier branches, the app image. Most exist per board today;
   they become board-agnostic, and 66 builds a release become one.

Then: rtlpg-tests boots the one image on all 66 board files, in safe mode
and with each board's own entry; and one board of each shape, plus the PoE
board, on the bench.

## Risks

- **Code space** is the main one: the PoE build starts at ~8.5 KB free,
  before the conversion and the picker. Measure the all-board image early.
- **rtlsim models port mapping, not wiring.** PHY polarity, the external
  PHY on a 5th copper port, a board whose SFP SerDes must be set up first:
  none of it shows in the sim. Safe mode promises the chip's own copper
  ports only; the bench confirms it per shape.
- **A wrong board drives the wrong pins**: reset loops from the reset pin,
  outputs such as PoE enable or SFP TX disable. Mitigations: the picker
  filters by detection; the loader's recovery; `do sys.factory_reset` back
  to safe mode.
- **The two 10G-copper boards** reach only their four own copper ports in
  safe mode.
- **The stock web updater** must accept the generic OEM image; vendors may
  add checks of their own. The clip covers every board regardless.

## From the user's side

| You have | You use | What happens |
|---|---|---|
| a stock 512 KiB switch | `rtlpg-tng-chip.bin`, by clip | loader + app; boots in safe mode |
| a stock switch, 1 MiB+ | `rtlpg-tng-oem.bin` in the stock web updater (where it takes it), else the clip | the same |
| upstream RTLPlayground, 1 MiB+ | `rtlpg-tng-bridge.bin` in its web updater | converts to compact, installs the loader, boots in safe mode |
| this fork's compact-layout or feat/sched | `rtlpg-tng-install.bin` in its web updater | app + loader, safe mode |
| rtlpg-tng | `rtlpg-tng.bin` in the web updater | app only; config and board kept; the loader untouched |

First boot, any route: the switch answers at 192.168.10.247 on its copper
ports. The web UI shows "board not set" with what it found ("RTL8373,
RTL8224, 2 MiB flash") and the boards that match. Pick one (or
`set sys.board <name>`, `apply`, `save`); after a reboot every port, the
LEDs, the cages and the button work. A wrong pick is re-picked; if it keeps
resetting, the loader's recovery or a factory reset brings back safe mode.

## Open questions

- Does a switch with no board set forward traffic, or only answer
  management until a board is picked?
- Should the picker ever choose by itself when only one board matches?
- Does the stock web updater take the generic OEM image? (Test against
  stock dumps in rtlsim, then on the bench: the first experiment.)
- The board table in flash, or kept on the host (only the chosen entry
  written to the switch)?
