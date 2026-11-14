#!/usr/bin/env python3
"""
bridge_image.py - make a compact image installable by standard-layout firmware.

    tools/bridge_image.py BOOT.bin [MDATA.bin] OUT.bin

Firmware with the standard layout only takes 512 KiB images and copies their
first 0x70000 bytes over itself, stopping short of its saved config at
0x70000. BOOT.bin is a compact build's programmer image
(rtlplayground-boot-*.bin: the loader at 0, its metadata, the application's
home bank in the slot, erased from 0x40000), which lies inside that span, so
the board arrives with the loader. This optionally puts a machine data region
(tools/mdata.py) at MDATA_XFER_START - also inside the copied span - and
fixes the last two bytes so the CRC-16 over all of it is 0xb001. The loader
starts the compact application, whose first boot moves the saved config
(0x70000 -> 0x7f000) and the machine data (-> MDATA_START), once each.

Only for boards with at least 1 MiB of flash: standard firmware refuses web
updates on 512 KiB, so those are migrated with a programmer.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout
from mdata import crc16

STANDARD_IMAGE = 0x80000


def boot_image(L, img, name):
    """A compact programmer image, checked as the loader and the application
    check it: the loader whole and for this layout, the metadata's
    application CRC right, nothing but erased flash from LDR_DATA_END on."""
    n = L["LDR_LOADER_LEN"]
    meta, slot, home = L["LDR_META"], L["LDR_SLOT"], L["LDR_HOME_LEN"]
    if img[:2] != b"\x00\x40" or crc16(img[:n]) != 0xB001 \
            or img[n - 6:n - 2] != b"LDR" + bytes((L["LDR_LAYOUT"],)):
        sys.exit(f"bridge: {name}: no compact loader at 0")
    c = crc16(img[L["LDR_APP_A"]:L["LDR_APP_A_END"]], crc16(img[slot:slot + home]))
    c = crc16(img[L["LDR_APP_B"]:L["LDR_APP_B_END"]], c)
    if img[meta:meta + 7] != b"RTLB" + bytes((c & 0xFF, c >> 8, L["LDR_LAYOUT"])):
        sys.exit(f"bridge: {name}: the loader metadata doesn't match the application")
    if img[L["LDR_DATA_END"]:] != b"\xff" * (len(img) - L["LDR_DATA_END"]):
        sys.exit(f"bridge: {name}: not erased from {L['LDR_DATA_END']:#x} on")
    return bytearray(img)


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    L = layout.read("-DLAYOUT_COMPACT")
    app = bytearray(open(sys.argv[1], "rb").read())
    if len(app) != STANDARD_IMAGE:
        sys.exit(f"bridge: {sys.argv[1]} is not a compact build's programmer image (rtlplayground-boot-*.bin)")
    img = boot_image(L, app, sys.argv[1])
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
    print(f"bridge image: {len(img):#x} bytes, with the loader" + (" and machine data" if len(sys.argv) == 4 else ""))


if __name__ == "__main__":
    main()
