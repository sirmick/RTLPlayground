#!/usr/bin/env python3
"""
mkchip.py - assemble a whole-chip image for a programmer, compact layout.

    tools/mkchip.py --app APP.bin [--loader LOADER.bin] [--mdata MDATA.bin]
                    [--config CONFIG.txt | --config-from CHIP.bin]
                    [--base CHIP.bin | --size 512K|1M|2M|4M] -o OUT.bin

  --app      a compact application image (the build's rtlplayground-*.bin)
  --loader   the #852 loader (rtlplayground-loader-*.bin): it goes to 0, the
             application's home bank to the slot, plus the metadata - as the
             build's boot image (tools/bootpack.py)
  --mdata    a machine data region (tools/mdata.py), at MDATA_START
  --config   a saved config to write at CONFIG_START; --config-from takes the
             saved-config sector from a chip read instead
  --base     a read of the chip: everything this doesn't write is kept from
             it (factory data such as a MAC address, a config); without it the
             rest is erased flash and --size gives the chip size

Always written: the image area, the loader's data area and the upload staging
area (erased: no half-staged upload or stale boot marks survive).
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout
from mdata import crc16

SIZES = {"512K": 0x80000, "1M": 0x100000, "2M": 0x200000, "4M": 0x400000}


def main():
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument("--app", required=True)
    ap.add_argument("--loader")
    ap.add_argument("--mdata")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--config")
    g.add_argument("--config-from")
    b = ap.add_mutually_exclusive_group()
    b.add_argument("--base")
    b.add_argument("--size", choices=SIZES)
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    L = layout.read("-DLAYOUT_COMPACT")

    chip = bytearray(open(a.base, "rb").read()) if a.base else bytearray(b"\xff" * SIZES[a.size or "512K"])
    if len(chip) not in SIZES.values():
        sys.exit(f"mkchip: {len(chip):#x} bytes is not a chip size")
    app = open(a.app, "rb").read()
    if len(app) != L["IMAGE_SIZE"] or crc16(app) != 0xB001 or app[:3] != b"\x00\x40\x02":
        sys.exit(f"mkchip: {a.app} is not a valid {L['IMAGE_SIZE']:#x}-byte compact image")

    # image, loader data and staging: rewritten / erased
    end = L["UPLOAD_START"] + L["IMAGE_SIZE"]
    chip[0:end] = b"\xff" * end
    chip[0:len(app)] = app
    if a.loader:
        ldr = open(a.loader, "rb").read()
        if len(ldr) != L["LDR_LOADER_LEN"] or crc16(ldr) != 0xB001:
            sys.exit(f"mkchip: {a.loader} is not a valid loader image")
        slot, home = L["LDR_SLOT"], L["LDR_HOME_LEN"]
        chip[slot:slot + home] = app[2:2 + home]
        chip[0:len(ldr)] = ldr
        c = crc16(chip[slot:slot + home])
        for lo, hi in ((L["LDR_APP_A"], L["LDR_APP_A_END"]), (L["LDR_APP_B"], L["LDR_APP_B_END"])):
            c = crc16(chip[lo:hi], c)
        meta = L["LDR_META"]
        chip[meta:meta + 7] = b"RTLB" + bytes((c & 0xFF, c >> 8, L["LDR_LAYOUT"]))
    if a.mdata:
        md = open(a.mdata, "rb").read()
        if md[:4] != b"MDT1" or crc16(md) != 0xB001 or len(md) > L["MDATA_LEN"]:
            sys.exit(f"mkchip: {a.mdata} is not a valid machine data region")
        if len(chip) < L["MDATA_START"] + L["MDATA_LEN"]:
            sys.exit("mkchip: this chip is too small for machine data")
        m = L["MDATA_START"]
        chip[m:m + L["MDATA_LEN"]] = b"\xff" * L["MDATA_LEN"]
        chip[m:m + len(md)] = md
    cs, cl = L["CONFIG_START"], L["CONFIG_LEN"]
    if a.config or a.config_from:
        cfg = open(a.config, "rb").read() if a.config else open(a.config_from, "rb").read()[cs:cs + cl]
        if a.config and len(cfg) >= cl:
            sys.exit("mkchip: the config is larger than its sector")
        chip[cs:cs + cl] = b"\xff" * cl
        chip[cs:cs + len(cfg)] = cfg
    open(a.out, "wb").write(chip)
    print(f"chip image: {len(chip):#x} bytes, app" + (" + loader" if a.loader else "")
          + (" + machine data" if a.mdata else "")
          + (" + config" if a.config or a.config_from else ""))


if __name__ == "__main__":
    main()
