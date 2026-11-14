#!/usr/bin/env python3
"""
rtlsim_images.py - assemble an image set for rtlsim (github.com/sirmick/ovs-rtlpg).

    tools/rtlsim_images.py BUILDROOT OUT

BUILDROOT holds builds named <MACHINE> and <MACHINE>-compact (as
`make rtlsim-images` makes them); OUT gets the
images and a manifest.json that rtlsim's flash tests read:

  [{"name", "file", "machine", "layout", "flash": "512K|2M|4M", "loader": bool,
    "kind": "app|install|loader|chip|bridge|mdata", "map", "notes"}, ...]

"map" is a build's rtlplayground.map, copied with the .mem, every .rst and the
banks.txt into <name>.build/. Per board, besides the standard and compact
application images: the compact install image (-compact-install: the
application, then the loader), the programmer image (-compact-boot) and the
/loader image (<MACHINE>-loader); for the
boards of 1 MiB and up, a bridge image carrying the loader (standard firmware installs
it over the web), and a whole chip of old standard firmware, the size of the
board's flash, with a config at 0x70000 and the factory MAC of a board that has
one, the starting point of a migration. The PoE board gets a machine
data region with two SYNTHETIC PSE images (seeded random bytes, not PoE
firmware) and a bridge that carries it: the real images are the PoE vendor's
and never belong here.
"""
import glob, json, os, random, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mdata import crc16  # noqa: E402

BOARDS = {
    "SWGT024_V2_0_UNMANAGED": ("512K", "SWTG024AS V2.0, RTL8372, 512 KiB, no factory MAC (derived from the chip id)", None),
    "AMPCOM_WAM902_SWTG018AS": ("2M", "SWTG018AS, RTL8373, 2 MiB, factory MAC at 0x1FC000", "00e04c180a01"),
    "POE_2G040210GSM": ("2M", "MokerLink POE-2G040210GSM, RTL8372 + RTL8238B PSE on I2C 0x20 (GPIO46/47), 2 MiB, "
                        "factory MAC at 0x1FC000", "00e04c2b0402"),
    "KEEPLINK_KP_9000_9XHPML_V1_2": ("2M", "keepLink KP-9000-9XHPML V1.2 (MokerLink 2G080110GSM), RTL8373, 2 MiB "
                                     "W25Q16JV, factory MAC at 0x1FC000", "00e04c900912"),
    "MOKERLINK_POE_2G080110GS": ("4M", "MokerLink POE-2G080110GS, RTL8373, 4 MiB W25Q32JV: too big for a bank to "
                                 "wrap onto the loader; no factory MAC (derived from the chip id)", None),
}
SIZES = {"512K": 0x80000, "2M": 0x200000, "4M": 0x400000}
VARIANTS = (("", "standard"), ("-compact", "compact"))
OLD_CONFIG = b"ip 192.168.2.2\ngw 192.168.2.1\nnetmask 255.255.255.0\nhostname chip-old\n"
TOOLS = os.path.dirname(os.path.abspath(__file__))


def pick(bd, pattern, exclude=()):
    found = [f for f in glob.glob(os.path.join(bd, pattern)) if not any(x in f for x in exclude)]
    if len(found) != 1:
        sys.exit(f"rtlsim_images: expected one {pattern} in {bd}, found {len(found)}")
    return found[0]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    root, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    man = []

    def add(**k):
        man.append(k)

    for m, (fl, note, _) in BOARDS.items():
        for suf, layout in VARIANTS:
            t = m + suf
            bd = os.path.join(root, t)
            dst = os.path.join(out, t + ".build")
            shutil.rmtree(dst, ignore_errors=True)
            for f in glob.glob(bd + "/**/*", recursive=True):
                if f.endswith((".map", ".mem", ".rst")) or os.path.basename(f) == "banks.txt":
                    o = os.path.join(dst, os.path.relpath(f, bd))
                    os.makedirs(os.path.dirname(o), exist_ok=True)
                    shutil.copy(f, o)
            mp = os.path.relpath(os.path.join(dst, "rtlplayground.map"), out)
            shutil.copy(pick(bd, "rtlplayground-v*.bin"), os.path.join(out, t + ".bin"))
            add(name=t, file=t + ".bin", machine=m, layout=layout, flash=fl, loader=False, kind="app", map=mp,
                notes=f"{note}. Application image" + ("; the same image installs with or without the loader"
                                                      if layout == "compact" else ""))
            if layout == "compact":
                shutil.copy(pick(bd, "rtlplayground-install-*.bin"), os.path.join(out, t + "-install.bin"))
                add(name=t + "-install", file=t + "-install.bin", machine=m, layout=layout, flash=fl, loader=True,
                    kind="install", map=mp,
                    notes="the web update image: the application, then the 16 KiB loader (it lands on the stage "
                          "0x7A000). A board without the loader converts at the next boot and the loader then "
                          "installs the application; a board with it installs the application as usual")
                shutil.copy(pick(bd, "rtlplayground-boot-*.bin"), os.path.join(out, t + "-boot.bin"))
                shutil.copy(pick(bd, "rtlplayground-loader-*.bin"), os.path.join(out, m + "-loader.bin"))
                add(name=t + "-boot", file=t + "-boot.bin", machine=m, layout=layout, flash=fl, loader=True, kind="chip",
                    map=mp, notes="first 512 KiB for a programmer: loader at 0, app home bank in the slot, metadata")
                add(name=m + "-loader", file=m + "-loader.bin", machine=m, layout=layout, flash=fl, loader=True,
                    kind="loader", map=None, notes="16 KiB loader image, for the /loader upload (staged at 0x7A000)")

    # synthetic PSE images: seeded random bytes, NOT PoE firmware
    r = random.Random(8238)
    pse = []
    for i, n in ((0, 9000), (1, 7000)):
        p = os.path.join(out, f"synth-pse{i}.bin")
        open(p, "wb").write(bytes(r.randrange(256) for _ in range(n)))
        pse.append(p)
    md = os.path.join(out, "synth-pse-mdata.bin")
    subprocess.run([sys.executable, os.path.join(TOOLS, "mdata.py"), "pse", *pse, md], check=True, stdout=subprocess.DEVNULL)
    add(name="synth-pse-mdata", file="synth-pse-mdata.bin", machine="POE_2G040210GSM", layout="compact", flash="2M",
        loader=False, kind="mdata", map=None,
        notes="machine data region (MDT1) with a PSE1 header and two SYNTHETIC images (synth-pse0/1.bin, random "
              "bytes, not PoE firmware): image 0 9000 B, image 1 7000 B; the firmware streams image 1 when RTL8238B "
              "register 0xe8 reads 02 00 00 00, else image 0")

    for m, (fl, _, mac) in BOARDS.items():
        if SIZES[fl] < 0x100000:	# standard firmware takes no web update on 512 KiB
            continue
        with_md = m == "POE_2G040210GSM"
        bridge = os.path.join(out, m + "-bridge.bin")
        subprocess.run([sys.executable, os.path.join(TOOLS, "bridge_image.py"),
                        os.path.join(out, m + "-compact-boot.bin")]
                       + ([md] if with_md else []) + [bridge], check=True, stdout=subprocess.DEVNULL)
        add(name=m + "-bridge", file=m + "-bridge.bin", machine=m, layout="standard->compact", flash=fl, loader=True,
            kind="bridge", map=f"{m}-compact.build/rtlplayground.map",
            notes="install with standard-layout firmware (web update). It carries the loader (the "
                  "-compact-boot image: loader at 0, metadata 0x3B000, the application's home bank in the "
                  "slot 0x3C000): the board arrives with the loader, which starts the compact application; its "
                  "first boot moves the config 0x70000->0x7F000"
                  + (" and the synthetic PSE mdata 0x60000->0x80000" if with_md else ""))
        chip = bytearray(b"\xff" * SIZES[fl])
        std = open(os.path.join(out, m + ".bin"), "rb").read()
        chip[:len(std)] = std
        chip[0x70000:0x71000] = OLD_CONFIG + b"\xff" * (0x1000 - len(OLD_CONFIG))
        if mac:
            chip[0x1FC000:0x1FC006] = bytes.fromhex(mac)
        open(os.path.join(out, m + "-chip-old-standard.bin"), "wb").write(chip)
        add(name=m + "-chip-old-standard", file=m + "-chip-old-standard.bin", machine=m, layout="standard", flash=fl,
            loader=False, kind="chip", map=f"{m}.build/rtlplayground.map",
            notes=f"{fl[:-1]} {fl[-1]}iB chip: the standard image at 0, a saved config at 0x70000 ("
                  + OLD_CONFIG.decode().strip().replace("\n", "; ") + "), "
                  + ("factory MAC " + ":".join(mac[i:i + 2] for i in range(0, 12, 2)) + " at 0x1FC000"
                     if mac else "no factory MAC") + "; the start of a migration")

    json.dump(man, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print(f"rtlsim_images: {out}: {len(man)} entries")


if __name__ == "__main__":
    main()
