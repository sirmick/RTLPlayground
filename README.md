# RTLPlayground
A Playground for Firmware development for advanced user of RTL8372/RTL8373 based 2.5GBit Switches.

> **This is an experimental branch** (`dev` of the [sirmick/RTLPlayground](https://github.com/sirmick/RTLPlayground)
> fork of [logicog/RTLPlayground](https://github.com/logicog/RTLPlayground)). It changes the flash
> layout and the update path. Keep a flash programmer at hand for any switch you put it on. The
> intent is to merge the pieces back upstream once they have proven themselves.
>
> What it adds:
> - **Compact flash layout** (`make LAYOUT=compact`): a 232 KiB image, defined once in `layout.h`,
>   so that **512 KiB switches can be updated through the web UI**. The saved config moves once,
>   at first boot, to its fixed place at `0x7f000`.
> - **DrDoof's loader on every compact switch, without a programmer**
>   ([#852](https://github.com/logicog/RTLPlayground/pull/852)). The loader installs every
>   update, web or XMODEM, verifies it first, and recovers a switch whose application fails. A
>   compact build makes, besides the application image:
>   - `rtlplayground-install-*.bin`, the web update image: the application followed by the loader.
>     A switch without the loader converts to it at the next boot, then the loader installs the
>     application. A switch with the loader just installs the application.
>   - `rtlplayground-boot-*.bin`, the image for a flash programmer: loader, application, metadata.
>   - `rtlplayground-loader-*.bin`, the 16 KiB loader alone, for the Loader card of the web UI.
>
>   Upstream standard-layout switches (1 MiB flash or more) move over with one web upload of a
>   bridge image (`tools/bridge_image.py`), which carries the loader. Firmware from before the
>   install image can't take it: give such a switch one plain application image first.
> - **Machine data region** at `0x80000` (1 MiB flash and more) for a board's own blobs, such as
>   the PoE controller firmware of the MokerLink POE-2G040210GSM: written once, never by an update.
> - **A web UI card per upload**: firmware, PoE controller firmware (on boards that have one) and
>   the loader, each refusing the others' files, with the switch's firmware, loader and PoE state
>   at the top. The startup config can be downloaded and loaded back from a file.
> - **Automatic code placement, a major refactor**: the 8051 code banks used to be assigned by
>   hand, with `#pragma codeseg` in 37 source files upstream, to keep in step whenever code grew or
>   moved. Now there is no bank placement in the sources or anywhere else: every build packs the
>   banked files into the three code banks itself, by a heuristic that fills them evenly and keeps
>   files that call each other or read each other's `__code` data together
>   (`tools/place_banks.py`, result in the build's `banks.txt`). The only decision left to a
>   developer is the short list of files that stay in the home bank (`RESIDENT_SRCS` in the
>   Makefile), so adding or growing code needs no bank bookkeeping. Every link is checked: calls and `__code` data reads that cross a bank, banks that overflow,
>   `__at` variables the linker also hands out, PSBANK not restored (`tools/check_cross_bank.py`,
>   `tools/check_linked.py`); `tools/check_layout.py` for region overlaps; a smaller web UI
>   (`LANGS=`).
> - **Tested in a simulator, in CI**: every push runs the real firmware image in
>   [rtlsim](https://github.com/sirmick/ovs-rtlpg), a simulator of the RTL837x, see
>   [Testing and CI](#testing-and-ci). `tools/docker-make.sh` builds in parallel.
> - **Low-hanging upstream PRs pulled in**: [#747](https://github.com/logicog/RTLPlayground/pull/747)
>   (MokerLink POE-2G040210GSM), [#370](https://github.com/logicog/RTLPlayground/pull/370) and parts
>   of [#305](https://github.com/logicog/RTLPlayground/pull/305) (code and internal RAM),
>   [#839](https://github.com/logicog/RTLPlayground/pull/839) (`flash r`),
>   [#843](https://github.com/logicog/RTLPlayground/pull/843) (installer checksum),
>   [#876](https://github.com/logicog/RTLPlayground/pull/876) (upgrade check against the PCB).
>
> On hardware so far:
> - **A 512 KiB SWTG024AS:** compact web updates, the loader's web and XMODEM installs and
>   self-update, config kept across updates, bad uploads refused.
> - **A 2 MiB MokerLink POE-2G040210GSM:** migrated from the standard layout with the bridge,
>   then converted to the loader over the web. Its config and PoE firmware were kept throughout.
>
> One known silicon quirk: a chip reset issued early in the boot (before the clock set-up) can
> hang a switch until a power cycle. The cause isn't known. Installs and the loader conversion
> reset late in the boot, where it has never happened.

For each hardware configuration of these devices, there is usually a managed and an
unmanaged version sold, with mostly identical hardware. The aim is to provide management
features also for unmanaged devices with additional features such as Management VLAN,
DHCP servers, multi-language support, IPv6 and TLS-encrypted web-pages. At present, however
only the following features are provided:
- A modern web-interface with mouse-over to display further information
- A serial console interface to configure all features
- IGMP to configure Multicast streaming
- Port configuration showing detailed information about own and Link-partner advertised
  Speed settings and configuration of these settings on the local side
- Per-port configuration of frame sizes (MTUs) for Jumbo-Frame support or limiting MTUs
  for particular devices
- EEE (Energy Efficient Ethernet) can be configured per-port. Detailed information is
  provided for support offered by the link partner and the EEE status of a port.
- VLAN configuration
- SFP information is displayed on the inserted modules, the current sensor values such as
  temperatures, RX and TX power are displayed in the CLI and as mouse-over on the web
- Mirror configuration
- Link Aggregation Groups can be set up
- Detailed information on port packet statistics
- Configuration saved to flash via the web-interface
- Firmware updates via the web
- Installation as a firmware upgrade from the original web-interface

<img width="1673" height="977" alt="GUI" src="doc/images/gui.png" />

While the firmware provides already considerable improvements over the original managed firmware,
the firmware still lacks support for the proprietary loop prevention
protocols as well as DHCP. Spanning Tree is available (see doc/stp.md), but is
a simplified implementation - read that document before enabling it on a
switch you administer over the network. If you need these features, do not install the playground on your managed
devices. In any case, installation is strongly discouraged unless you can at least make
a backup of the original flash content via a SOIC clamp such as also used for BIOS
backups and can re-install that firmware in case something is wrong. For this no soldering
skills are necessary.

The firmware supports all hardware features of devices with
- 4 2.5GBit ports + 2 SFP+ ports
- 5 2.5GBIT + 1 SFP+ port
- 8 2.5GBit + 1 SFP+ port
Devices sold usually have a fairly common design, however there may be differences in the LED
configuration (switches have LEDs with different colours and use types of LEDs). The list
of tested devices can be found in [Supported devices](doc/supported_devices.md).

To do meaningful development you will need to use a serial console, so soldering skills
are required. Flashing must be done via a SOIC-8 PatchClamp or by soldering a socket
for the flash chip.

If you don't want to open your device, you can use the project's code to learn about the
devices by looking at the image using e.g. Ghidra. If you want to contribute to the
design of the web-interface or get a feeling for the interface first, a standalone
device simulator is provided, which runs entirely under Linux as a local webserver.

## (0) Compiling Requirements

Install the following particular build requisites (Debian 12/13):
```
sudo apt install make gcc g++ bison flex libboost-dev patch bzip2 curl xxd python-is-python3 libjson-c-dev zlib1g-dev
```

The first `make` downloads SDCC 4.6.0, applies the fixes in `sdcc/` and builds it into
`toolchain/`, which takes a few minutes. Later builds reuse it. The released SDCC 4.5.0
and 4.6.0 miscompile parts of this code, see [SDCC bug #4153](https://sourceforge.net/p/sdcc/bugs/4153/)
and [#4154](https://sourceforge.net/p/sdcc/bugs/4154/); both fixes are in SDCC trunk since
r17039. The third patch is for [#4070](https://sourceforge.net/p/sdcc/bugs/4070/), where the
assembler drops the top byte of a `__sfr32` address list; it is proposed upstream and lets
`SFR_DATA_U32` be used again. To use an SDCC you installed yourself instead, run `make CC=sdcc ...`.

<details>
<summary>If using Docker (click to expand)</summary>

### Prerequisites

Install Docker for your platform:

- **Linux (Debian/Ubuntu)**: `sudo apt install docker.io` then `sudo usermod -aG docker $USER` (log out and back in)
- **Linux (other distros)**: Follow the [Docker Engine install guide](https://docs.docker.com/engine/install/)
- **Windows**: Install [Docker Desktop for Windows](https://docs.docker.com/desktop/setup/install/windows-install/)
- **macOS**: Install [Docker Desktop for Mac](https://docs.docker.com/desktop/setup/install/mac-install/)

### Usage

A Dockerfile is provided for a reproducible build environment:

```
docker build -t rtlplayground-dev .
```

Build the firmware (replace MACHINE with your target, e.g. `DEFAULT_8C_1SFP`):

```
docker run --rm -v $(pwd):/workspace rtlplayground-dev make MACHINE=DEFAULT_8C_1SFP
```

The resulting `.bin` file appears in `output/` on your host.

Build the OEM web upgrade bin (after building the firmware bin)

```
docker run --rm -v $(pwd):/workspace rtlplayground-dev make -C installer
```

The resulting `rtlplayground_oem_upgrade.bin` file appears in `installer/output/` on your host.

Build host tools only:

```
docker run --rm -v $(pwd):/workspace rtlplayground-dev make -C tools
```

Run the web-interface simulator through docker:

```
docker run --rm -p 8081:8081 -v $(pwd):/workspace rtlplayground-dev \
  tools/output/httpd_sim.py /workspace/html
```

Edit `config.txt` on your host, then re-run `make` — the
source directory is mounted into the container, so changes take effect
immediately. To build for a different machine, pass `MACHINE=<name>`, which
selects `machine/<name>.c` (see [Adding a new device](doc/adding-a-device.md)).

</details>

## (1) Compiling for direct chip flashing AND upgrading an existing RTLPlayground running device

Pass the machine that matches your device, for example:
```
make MACHINE=DEFAULT_8C_1SFP
```
The name selects the board definition `machine/<name>.c`; `make` without a
`MACHINE` value stops with an error listing what is expected. If your board is
not in the list yet, see [Adding a new device](doc/adding-a-device.md).

To embed a machine-specific startup configuration, pass `CONFIG`:
```
make MACHINE=DEFAULT_8C_1SFP CONFIG=configs/DEFAULT_8C_1SFP.txt
```
`CONFIG` defaults to `config.txt`; the selected file is embedded in the image
and changing it triggers a rebuild.

To choose the output directory while keeping the automatic file name (with
version, git hash and machine), pass `OUTPUT_DIR`:
```
make MACHINE=DEFAULT_8C_1SFP OUTPUT_DIR=/tmp/firmware
```
To also change the leading part of the name while keeping that automatic
suffix, use `OUTPUT_PREFIX` (it is prepended verbatim):
```
make MACHINE=DEFAULT_8C_1SFP OUTPUT_DIR=/tmp/firmware OUTPUT_PREFIX=my-switch-
```
This produces `/tmp/firmware/my-switch-v<version>-<git>-<machine>.bin`.
To set the complete file name inside `OUTPUT_DIR`, use `OUTPUT_NAME` (it replaces
the automatic name, so the version/git suffix is not appended):
```
make MACHINE=DEFAULT_8C_1SFP OUTPUT_DIR=/tmp/firmware OUTPUT_NAME=my-switch.bin
```
`OUTPUT_PREFIX` and `OUTPUT_NAME` are alternatives: use only one of them. If
`OUTPUT_NAME` is set, `OUTPUT_PREFIX` is ignored.
To set the complete path and file name at once, use `OUTPUT`:
```
make MACHINE=DEFAULT_8C_1SFP OUTPUT=/tmp/firmware/my-switch.bin
```
When `OUTPUT` is a custom path, `output/rtlplayground.bin` is left untouched:
only the default build updates that symlink (and the installer uses it).
Without overrides the image is `output/<machine>/rtlplayground-v<version>-<git>-<machine>.bin`.
`BUILDDIR` still selects where intermediate objects and tools are written.

> [!TIP]
> You can write configuration parameters in config.txt (see below) in order your switch to get
> straight at the first boot, a correct IP configuration.

Now, building the firmware image should work.

Note, that the image generated ends in .bin, not .img, in order to make IMSProg happy.

image location is stored in `RTLPlayground/output/rtlplayground_version_machine.bin`
for example
```
rtlplayground-v0.1.0-12c98ba-dirty-LIANGUO_ZX_SWTGW215AS.bin
```

> [!CAUTION]
> This image can be flashed directly to the chip OR through the firmware update/upgrade
> interface of RTLPlayground interface

## (2) Compiling for OEM running device with management options (web upgrade)

Managed switches can be updated from the existing original firmware using a SPECIFIC upgrade image.
You first need to build the firmware for direct chip flashing : See below (1)

Then

```
cd installer
make 
```
image location is stored in  `RTLPlayground/installer/output/rtlplayground_oem_upgrade.bin`

> [!CAUTION]
> This image must ONLY be used for original OEM firmware web interface firmware upgrade.
> You do not need this image if you are already on RTLplayground firmware.
> Unless you go back to the original OEM firmware, you would only flash this specific firmware
> only once. Future upgrades of RTLPlayground will only need to follow (1)

example of compilation console output

```
RTLPlayground/installer$ make
mkdir -p output
gcc updatebuilder.c -o output/updatebuilder
../toolchain/sdcc-3390765201/bin/sdas8051 -plosgff -o output/crtstart.rel crtstart.asm
../toolchain/sdcc-3390765201/bin/sdcc -mmcs51 --code-loc 0x1000 -o output/installer.rel -c installer.c
../toolchain/sdcc-3390765201/bin/sdcc -mmcs51 -Wl-bHOME=0x1100 -Wl-r -o output/rtlinstaller.ihx output/crtstart.rel output/installer.rel
./output/updatebuilder -i output/rtlinstaller.ihx -o output/rtlplayground_oem_upgrade.bin ../output/rtlplayground.bin
Input file size: 524288
Bytes read: 524288
EOF
Payload sum 1 is: 0x25100
Payload sum 2 is: 0x25100
Payload sum with header is: 0x264ec
Payload sum is: 0xf8fe94
Header checksum is: 0x5a1
```

## (3) Sandbox Usage with Ghidra (optional)

You can play with the image using ghidra or flash real Switch Hardware. For
ghidra see this information about [Ghidra images](doc/ghidra.md).

## (4) Installation through the Web interface (software way)

Managed switches (OEM firmware of RTLplayground firmware) can be upgraded via the web interface.
Unmanaged switch cannot be flashed this way (see 5).

Go to "Firmware update" tab, select the correct file.

> [!IMPORTANT]
> On this branch with `LAYOUT=compact`, upload `rtlplayground-install-*.bin` in the Firmware
> card: it brings the loader to a switch that doesn't have it yet. A switch running RTLPlayground
> from before the install image takes the plain `rtlplayground-v*.bin` once; use the install
> image from then on. A standard-layout switch moves to compact with one upload of a bridge image
> (`tools/bridge_image.py`).
>
> If your device already runs RTLPlayground, you must upload the binary file /RTLPlayground/output/rtlplayground_Version_Machine.bin
> If your device is OEM, you must upload the binary file /RTLPlayground/installer/output/rtlplayground_oem_upgrade.bin

> [!CAUTION]
> Check one more time that your device matches the machine type before flashing.
> Be sure you have a backup of the original firmware before diving in RTLPlayground.

Finally, push the Upload File Button and you're done !


## (5) Flashing the ROM directly (hardware way, but also only way to rescue)

This procedure is the only way to flash unmanaged switches, if the ROM chip is large enough.
This is also the only way to unbrick your device if something went wrong.

> [!IMPORTANT]
> You need a SOIC-8 clip to flash the ROM chip directly onboard.
> Alternatively you can de-solder the flash chip and install a SOIC adapter).
> For flashing the chip directly, you must use the binary file /RTLPlayground/output/rtlplayground_Version_Machine.bin
> (on this branch with `LAYOUT=compact`: `rtlplayground-boot-*.bin`, which includes the loader).

> [!CAUTION]
> As you need to open your switch case, consider that the warranty is gone.

- Disconnect power from switch.
- Open the switch.
- Attach the clip onto the flash chip (Red line on Pin 1, Pin 1 has a point marker).
- Connect USB of flash programmer, the power LED on the switch will light up, check cabling if not.
- Don't panic, mixing up GND and 3.3V usually does not destroy the switch.
- Use IMSProg, Flashrom, or whatever Programmer to detect the chip.
- MAKE A BACKUP (DUMP) OF THE EXISTING FIRMWARE !
- ERASE THE ROM (BLANK) !
- Load the firmware into IMSProg.
- Flash is to the ROM chip.
- Disconnect the clip from the ROM chip.
- You're done, ready for the first boot.

## (6) Connecting a serial interface (optional)

You can connect a serial cable to the UART port found on all the devices, set 8N1 @ 115200 baud.

## (7) Power Up

When you power up the switch, the device will perform some examples and provide a minimal console
(if wired to a serial interface), the documentation of which can be found in the source code rtlplayground.c`.

To restart the switch from the console, type `reset`, or send two Ctrl-R (`^R ^R`, 0x12 0x12)
bytes in a row: once the switch has booted, those reset it at once from the UART interrupt, even
when the console is busy or stuck (sent during the boot, they take effect as soon as it
completes). They also work at any prompt of the optional loader (not inside an XMODEM transfer).

## (8) The web-interface

The web-interface can be reached under the [default 192.168.10.247](http://192.168.10.247) unless you
specified an IP address in the config.txt before compilation.

> [!TIP]
> The default password is `1234`.

## (9) The command line

The command line is very rudimentary and mostly for testing purposes.
The following is a boot-log with some examples:
```
Detecting CPU
RTL8373 detected
Starting up...
  Flash controller

NIC reset
rtl8372_init called

RTL837X_REG_SDS_MODES: 0x00000bed

phy_config_8224 called

phy_config_8224 done

rtl8224_phy_enable called

rtl8224_phy_enable done

rtl8372_init done

A minimal prompt to explore the RTL8372:

CPU detected: RTL8373
Clock register: 0x00001101
Register 0x7b20/RTL837X_REG_SDS_MODES: 0x00000bed
Verifying PHY settings:

 Port   State   Link    TxGood          TxBad           RxGood          RxBad
1       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
2       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
3       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
4       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
5       On      2.5G    0x00000008      0x00000000      0x00000000      0x00000000
6       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
7       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
8       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
9       NO SFP  Down    0x00000000      0x00000000      0x00000000      0x00000000

> port 5 1g
  CMD: port 5 1g
PORT 04 1G

> stat
  CMD: stat
 Port   State   Link    TxGood          TxBad           RxGood          RxBad
1       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
2       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
3       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
4       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
5       On      1000M   0x00000035      0x00000000      0x00000017      0x00000000
6       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
7       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
8       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
9       NO SFP  Down    0x00000000      0x00000000      0x00000000      0x00000000

>
<SFP-RX OK>

<MODULE INSERTED>  Rate: 67  Encoding: 01
Lightron Inc.   WSPXG-ES3LC-IHA 0000

> stat
  CMD: stat
 Port   State   Link    TxGood          TxBad           RxGood          RxBad
1       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
2       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
3       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
4       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
5       On      1000M   0x00000065      0x00000000      0x0000003b      0x00000000
6       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
7       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
8       On      Down    0x00000000      0x00000000      0x00000000      0x00000000
9       SFP OK  10G     0x00000000      0x00000000      0x0000001c      0x00000000

> sfp
  CMD: sfp
Rate: 67  Encoding: 01
Lightron Inc.   WSPXG-ES3LC-IHA 0000
```

## (10) Advanced configuration

You can configure more deeply the switch without the need of the console mode.

While in compilation part, you might write directly to config.txt file before making the binary firmware

```
nano config.txt
```

If you want to modify settings after the flash is done, go to the System tab and find the Startup Configuration.

<img width="1673" height="978" alt="ADVANCED SETTINGS" src="doc/images/advanced_settings.png" />

```
ip xxx.xxx.xxx.xxx      = IP address of the switch
gw yyy.yyy.yyy.yyy      = IP address of the gateway
netmask zzz.zzz.zzz.zzz = Network mask of the switch 
port x name xxx         = Name xxx the port number x
port z 1g               = Set 1g speed for port z
igmp on/off             = Turn IGMP on or off
session xxxx            = Web session timeout in seconds (default 3600)
```
[To be continue]

Enjoy playing!

## (13) HTTP Simulator for front-end development

The simulator is available in tools as `./httpd_sim.py`
By default it will serve the content in the current working directory so the easiest way to run this is to:
```
cd ./html
../tools/httpd_sim.py
```
And the simulator will be available on http://127.0.0.1:8081

There are commandline options available:
```
usage: httpd_sim.py [-h] [--machine MACHINE] [--port PORT] [--bindaddr BINDADDR] [--verbose]

options:
  -h, --help           show this help message and exit
  --machine MACHINE    path to machine.c or .h file to parse (optional)
  --port PORT          port to bind to (default 8081)
  --bindaddr BINDADDR  address to bind (default 127.0.0.1)
  --verbose            enable verbose output
```

The machine parsing is somewhat experimental.

## Testing and CI

The firmware is tested without hardware in **rtlsim**, a simulator of the RTL837x switch chips:
[github.com/sirmick/ovs-rtlpg](https://github.com/sirmick/ovs-rtlpg). It runs the real built
image, unmodified: the 8051 core with its code banks, the flash controller and SPI flash, the
switch registers, the NIC and the PoE controller on I2C. Linux in a network namespace talks to
it over a tap or an Open vSwitch bridge.

**CI.** Every push and pull request runs two GitHub workflows:
- **`build.yml`** builds the firmware with warnings as errors, for several machines and
  variants, compiles every machine definition, and runs the host unit tests (`make -C test`).
- **`rtlsim.yml`** builds the SWTG024AS compact image and the image set below, checks out
  rtlsim, and runs the image through it (`tools/ci/rtlsim-ci.sh` there). The tests check:
  - the boot log against silicon, and ARP, ICMP and TCP through the NIC
  - switching, with Linux in network namespaces over a tap and over Open vSwitch
  - rtlsim's Alpine switch VM booting under QEMU
  - a power cut at every step of a web firmware install
  - standard-to-compact migrations, the PoE controller's firmware load, and the loader
    starting the application

There is no CD yet: the workflows don't publish the images they build.

**Locally.** `make rtlsim-images OUT=dir` builds the images rtlsim's flash tests read, with a
`manifest.json`, for three boards (the SWTG024AS, a 2 MiB RTL8373 board, and the PoE board).
It includes the bridges and an old-firmware chip to migrate from. rtlsim's own README explains
how to run its tests against them.

**What rtlsim can't vouch for.** Its tests are functional, not timing-faithful. Its flash and
CPU timing aren't the real chip's, and it doesn't model why early resets hang on silicon. A
green run means the logic and the flash contents are right, not that an update is safe on a
real switch. A switch with serial on the bench is the last check.

The host unit tests in `test/` (`make -C test -j8 quick` takes seconds) cover the web server,
the command editor, the port tables and the update code against a model of the flash. Most of
the flash-model tests are on their way out, now that rtlsim covers that ground better.

## (12) Other documents

The following documents give further documentation on specific features of the RTL837x SoCs:
- [Adding a new device](doc/adding-a-device.md)
- [RTL8372/3 Feature support](doc/hardware.md)
- [CPU Port](doc/CpuPort.md)
- [L2 learning](doc/l2.md) 
- [IGMP (IP-MC streaming)](doc/igmp.md)
- [SFP+ ports](doc/sfp.md) 
- [Trunking aka. port aggregation](doc/link_aggregation.md)
- [LACP (802.3ad link aggregation)](doc/lacp.md)
- [VLAN](doc/vlan.md)
- [Storm control](doc/storm_control.md)
- [Modifications and Flash replacement](doc/mods.md)
