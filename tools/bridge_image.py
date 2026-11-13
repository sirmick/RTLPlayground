#!/usr/bin/env python3
"""
bridge_image.py - make a compact image installable by standard-layout firmware.

    tools/bridge_image.py COMPACT.bin [MDATA.bin] OUT.bin

Firmware with the standard layout only takes 512 KiB images and copies their
first 0x70000 bytes over itself, stopping short of its saved config at
0x70000. This pads a compact image to 512 KiB with erased flash, optionally
puts a machine data region (tools/mdata.py) at MDATA_XFER_START - inside that
copied span - and fixes the last two bytes so the CRC-16 over all of it is
0xb001. The compact firmware's first boot then moves the saved config
(0x70000 -> 0x7f000) and the machine data (-> MDATA_START), once each.

Only for boards with at least 1 MiB of flash: standard firmware refuses web
updates on 512 KiB, so those are migrated with a programmer.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout
from mdata import crc16

STANDARD_IMAGE = 0x80000


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    L = layout.read("-DLAYOUT_COMPACT")
    app = bytearray(open(sys.argv[1], "rb").read())
    if len(app) != L["IMAGE_SIZE"] or crc16(app) != 0xB001 or app[:3] != b"\x00\x40\x02":
        sys.exit(f"bridge: {sys.argv[1]} is not a valid {L['IMAGE_SIZE']:#x}-byte compact image")
    img = app + b"\xff" * (STANDARD_IMAGE - len(app))
    if len(sys.argv) == 4:
        md = open(sys.argv[2], "rb").read()
        if md[:4] != b"MDT1" or crc16(md) != 0xB001 or len(md) > L["MDATA_LEN"]:
            sys.exit(f"bridge: {sys.argv[2]} is not a valid machine data region")
        x = L["MDATA_XFER_START"]
        img[x:x + len(md)] = md
    c = crc16(img[:-2]) ^ 0xFFFF
    img[-2], img[-1] = c & 0xFF, c >> 8
    assert crc16(img) == 0xB001
    open(sys.argv[-1], "wb").write(img)
    print(f"bridge image: {len(img):#x} bytes" + (" with machine data" if len(sys.argv) == 4 else ""))


if __name__ == "__main__":
    main()
