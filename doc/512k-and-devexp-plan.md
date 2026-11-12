# Easier to work on, and fitting 512 KiB devices

## Two goals, one effort

1. **Developer experience** — make the firmware easier and safer to change, and easier to know a change still works, without a programmer or a switch on the bench.
2. **512 KiB flash support** — those boards can't be updated over the web UI today. A web update stages a second full copy of the image above the running one, and a 512 KiB chip has no room for it.

These are the same project. The image is mostly padding — a real build is roughly 140 KiB of code and 60 KiB of compressed web UI inside a 512 KiB slot, and the rest is empty. So the thing that blocks 512 KiB web updates is size and the lack of a safe, tested update path. The changes that make the code easier to work on are the same ones that make 512 KiB fit.

## What is already in flight (and why this isn't a fifth approach)

Several people are already working on the update path:

- **#555** stages the upload into the gaps between banks.
- **wrcrooks' `compact-512k-layout` branch** packs the image smaller.
- **#852** adds an optional bootloader that installs updates safely and offers recovery.
- **#864** blocks web updates on flash that is too small.
- **#442** fails the build when a code bank grows into the web UI.

We don't want to add another competing update mechanism. We want to build the shared foundation that all of these need — tests you can run without hardware — and the mechanical size wins that help every one of them. The compaction work then rebases cleanly onto current `main`, and the safe-copy step can be driven by #852's loader rather than reinvented.

## The plan, in three strands

### (a) Strengthen flash testing — first

- A faithful model of the flash chip: erase sets a sector to `0xFF`, a write can only clear bits, sectors are 4 KiB, the device has a fixed size, and addresses wrap at that size exactly as the SoC's SPI addressing does.
- Host tests that run the **real** upload and boot-time copy against that model, using **real built images** as the payload.

This catches, as ordinary unit-test failures and with no board:

- an erase or copy that runs off the end of a small chip and wraps onto the running image at address 0 (this bug was real; it bit the compact-layout work);
- a write to a sector that wasn't erased first;
- the 512 KiB "too small, refuse the upload" path;
- the saved configuration surviving an update;
- a power cut part-way through a copy, and the resume.

It is first because it is the safety net every other change relies on, and because it lets the 512 KiB update work be written test-first instead of tested by reflashing a switch.

### (b) Bin-pack the flash — automatic bank placement

Today each source file is pinned to a code bank by hand with `#pragma codeseg`. The result is poorly packed: one bank is nearly empty and about 35 KiB is wasted, which pushes the code further up the flash than it needs to go.

The compiled objects don't actually care which bank they end up in — the bank number is filled in by the linker, not baked in at compile time. So we can compile once, measure each file's size, pack the files into banks, generate the placement, and link once. No recompiling, and no hand-balancing as the code grows.

The one real risk is a call that crosses banks without going through the banked-call path; that crashes at runtime. A small checker reads the compiled objects and flags every such call by name. Here the call graph is almost entirely direct calls, so the checker is authoritative. It also subsumes #442 — "code bank too large" stops being a thing.

Payoff: a freed bank, a smaller image, and a layout that stays packed on its own.

### (c) Reduce the low-hanging fruit — smaller web UI

- A build option to keep only the languages you want. The UI ships six; English-only saves about 24 KiB.
- Fix the image tool leaving the uncompressed copy of each web file behind after it compresses it. (Both of these are already carried in the in-flight branches.)

Pure size wins, useful on every board.

## How it adds up for 512 KiB

With the web UI trimmed, the banks packed, and the web UI placed right after the code instead of at a fixed address, the image comes down to roughly 180 KiB. On a 512 KiB chip that leaves room for the running image, a staged update, and the saved config, with headroom to spare — so web updates work, using a copy that writes the reset sector last and can resume after a power cut.

## What we will and won't build

**Will:** the flash model and the tests; the bank packer and the cross-bank checker; the web-UI trims; a compact layout rebased on current `main`.

**Won't:** a competing bootloader (defer to #852); a full switch-fabric emulator; any arbitrary-code-execution facility in a normal image.

## Validating without bricking

- Host tests cover the logic and the flash process with no hardware.
- A small 8051 emulator can answer the one thing the host tests can't — does the real banked image actually boot — and that only needs doing once, since the boot and banking machinery doesn't change when files move between banks.
- Spare boards, a serial console, and a programmer for recovery cover the final sign-off. A 512 KiB board is on hand.

## Status

- **(a) flash testing: done.** A flash device model and its self-test; the real
  boot-copy (`update_apply.c`, lifted out of `rtlplayground.c`) run against it
  with a bit-exact host CRC (checked against the 0xB001 residual on real
  images): the 512 KiB refusal, copy-with-config-preserved, and power-cut
  resume, all in `make -C test`.
- **(b) bin-pack: done.** A static cross-bank checker (`check_cross_bank.py`,
  zero violations on the current build), and a packer (`pack_banks.py`) that
  keeps direct-call groups together, pins self-banking asm, and packs to
  headroom. Applied, the build goes from four code banks to three - a freed
  bank - and two machines build with the checker confirming zero cross-bank
  calls on the linked result. (The placement is applied via regenerated
  codeseg banks; emitting it straight into the link, so the banks need no hand
  pragmas at all, is a follow-up.)
- **(c) web-UI trims: done.** A `LANGS=` build option prunes the UI's
  languages (English-only ~24 KiB smaller compressed), and `fileadder` no
  longer leaves each file's uncompressed tail in the image. Together the web
  region goes from 188 KiB to 61.5 KiB (all languages) or 37.3 KiB (English).
- **Next:** the compact layout and the safe, resumable copy on top of (a) -
  with the code a bank shorter and the web UI ~150 KiB smaller, the image now
  fits a 512 KiB web update with room to spare.
