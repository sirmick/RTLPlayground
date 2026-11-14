<!-- DRAFT (2026-11-13), not committed. Close but not final: the host-side flash
model is moving to the emulator (ovs-rtlpg fade8d2), so the sentence about it
below must change once RTLPlayground's copy is stripped out and the update,
config and machine-data paths are tested in the simulator instead. -->

This branch makes the cheapest RTL837x switches, those with 512 KiB of flash, maintainable without a programmer. A compact layout shrinks the firmware to a 232 KiB image, so a web update has room to stage a second copy. DrDoof's optional bootloader (#852) works in both the compact and the standard layout: it verifies every staged install before copying it, offers a recovery mode with XMODEM over the serial console after three failed boots, and can update itself. The firmware page accepts four kinds of upload, each recognised by its header and read back before it's accepted: the application, the loader, the configuration, and the board's machine data (such as the PoE controller firmware of the MokerLink POE-2G040210GSM). To make room, the web UI is minified and gzipped, `LANGS=` keeps only the languages you want, and `fileadder` no longer leaves the uncompressed copy behind. The code is bin-packed into three banks instead of four: every build sizes each file, groups the files that must share a bank, and packs the groups largest first into the least-full bank, leaving 3.3–4.6 KiB free in each.

| Address | Contents (compact layout) |
|---|---|
| `0x00000`–`0x03FFF` | Home bank. With the loader: the loader, and the app's home bank lives in the slot at `0x3C000` |
| `0x04000`–`0x0FFFF` | Bank 1 |
| `0x10000`–`0x1BFFF` | Bank 2 |
| `0x1C000`–`0x27FFF` | Bank 3 |
| `0x28000` | Web UI |
| `0x39000` | Default config (image CRC at `0x39FFE`) |
| `0x3A000` | Loader data: boot counter `0x3A000`, metadata `0x3B000`, app home-bank slot `0x3C000` |
| `0x40000` | Upload staging: application image, web or XMODEM |
| `0x7A000` | Loader update staging |
| `0x7E000` | Install progress record |
| `0x7F000` | Saved config |
| `0x80000` | Machine data (1 MiB chips and up): written once, never by an update |
| `0x1FC000` | Factory MAC address (2 MiB boards), left as the vendor wrote it. 512 KiB boards have none and derive a locally administered MAC from the chip's ID |

Bank bookkeeping is now maintenance-free. There are no `#pragma codeseg` lines and no bank list to keep. A new file goes into `SRCS`, a file that grows is simply placed again on the next build, and the only placement decision left is the short list of files that must stay in the home bank. Behind that sits a set of static analysis tools that run in every build and stop it with the cause named:
- **`place_banks.py`** reads the compiled objects and generated assembly to find which files must share a bank: a direct call, a direct read of another file's `__code` table, or a pointer to a function that isn't `__banked`. Then it does the packing.
- **`check_cross_bank.py`** checks the linked image for any direct call or `__code` data read that still crosses a bank.
- **`check_linked.py`** catches:
  - a bank that overflows its 48 KiB window;
  - an `__at` variable placed where the linker also puts other variables;
  - code that changes the bank register (PSBANK) and returns without restoring it. It follows each function's saves and restores instruction by instruction.
- **`check_layout.py`** and the build-time asserts in `layout.h` keep the flash regions from overlapping and check the finished image against the layout.

[TO REVISE: the host-side flash model has moved to the emulator.] Alongside these, a host-side model of the SPI flash runs the real boot-time update, config move and machine-data move, cutting the power at every single flash operation and checking that the switch still boots.

The complexity hasn't gone away; it has moved to a few places that are written once and tested hard:
- **Update and recovery paths.** The loader's install and recovery, the app's own staged copy with its progress record, and the one-time moves of the config and the machine data. Each has to survive a power cut at any point.
- **The upload handler.** It tells the four upload types apart, stages each in the right region, and refuses anything that doesn't read back intact.
- **The analysis tools themselves.** They parse SDCC's output formats. They're conservative and fail loudly, but an SDCC upgrade is the moment to rerun the tests and the full machine sweep.
