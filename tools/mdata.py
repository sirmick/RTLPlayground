#!/usr/bin/env python3
"""
mdata.py - build a machine data region (layout.h, MDATA_*) for a board.

    tools/mdata.py pse IMAGE0 IMAGE1 OUT.bin

pse: the POE-2G040210GSM's PoE controller (RTL8238B) images; '-' for one
you don't have. The images are the PoE vendor's firmware and are not part of
this repository: take them from your own switch's stock flash, see
doc/devices/2M-PCB49-V1.1.md.

The region is "MDT1", the payload length (uint32 LE), the payload and a CRC-16
that makes the whole come to 0xb001. With the compact layout it lives at
MDATA_START, outside anything an update writes, and is written once: by a
programmer, or carried by a bridge image (tools/bridge_image.py) and moved
there by the first boot.
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout


def crc16(data, c=0):
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c


def region(payload, max_len):
    body = b"MDT1" + struct.pack("<I", len(payload)) + payload
    c = crc16(body) ^ 0xFFFF
    out = body + bytes((c & 0xFF, c >> 8))
    assert crc16(out) == 0xB001
    if len(out) > max_len:
        sys.exit(f"mdata: {len(out)} bytes, more than the region's {max_len}")
    return out


def main():
    if len(sys.argv) != 5 or sys.argv[1] != "pse":
        sys.exit(__doc__)
    imgs = [b"" if p == "-" else open(p, "rb").read() for p in sys.argv[2:4]]
    if not any(imgs):
        sys.exit("mdata: no image given")
    pse = b"PSE1" + struct.pack("<II", len(imgs[0]), len(imgs[1])) + imgs[0] + imgs[1]
    L = layout.read(os.environ.get("LAYOUT_DEFS", "-DLAYOUT_COMPACT"))
    out = region(pse, L["MDATA_LEN"])
    open(sys.argv[4], "wb").write(out)
    print(f"machine data: {len(out)} bytes (PSE images {len(imgs[0])} + {len(imgs[1])})")


if __name__ == "__main__":
    main()
