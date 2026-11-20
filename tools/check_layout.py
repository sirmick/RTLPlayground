#!/usr/bin/env python3
"""
check_layout.py - check a built image against its flash layout.

The image is assembled from fixed regions (rtl837x_common.h): code from 0,
the web UI at HTML_LOCATION, the default config at DEFAULT_CONFIG_LOCATION,
the CRC in the last two bytes. Nothing stops one region from running into the
next - the tools just overwrite - so this fails the build when:

  - code (the imagebuilder output) has data at or past HTML_LOCATION;
  - the web UI (html_data.h's FDATA_START/FDATA_SIZE) runs past the default
    config;
  - the final image is not IMAGESIZE bytes;
  - the final image holds no valid default config at DEFAULT_CONFIG_LOCATION:
    binary records (om.c, DESIGN.md 15.3), "\0RC1", a length that fits the
    sector and the records' CRC. A boot whose saved config isn't valid loads
    this sector; a bad one would leave the switch on the schema's built-ins.
    A tool that overwrites it (fileadder zeroed it on the compact layout until
    the web data went in first) leaves zeros or erased flash.

Usage: check_layout.py --html <addr> --default-config <addr> --size <bytes>
         [--code <imagebuilder output>] [--html-defs html_data.h] [--image <final.bin>]
"""
import sys, re, argparse


def num(s):
    return int(s, 0)


CONFIG_LEN, OM_HDR = 0x1000, 8


def crc16(data):			# CRC-16/0xA001 from 0: om.c's crc16_bank1()
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c


def config_bad(sec):
    """Why the sector isn't a valid binary config, or None."""
    if sec[:4] != b"\0RC1":
        return "no \\0RC1 header (it starts %s)" % sec[:4].hex()
    n, c = int.from_bytes(sec[4:6], "little"), int.from_bytes(sec[6:8], "little")
    if n > CONFIG_LEN - OM_HDR:
        return "a length of %d B, past the sector" % n
    if crc16(sec[OM_HDR:OM_HDR + n]) != c:
        return "a bad CRC (0x%04x, the records' 0x%04x)" % (c, crc16(sec[OM_HDR:OM_HDR + n]))
    return None


def main(argv):
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument("--code")
    ap.add_argument("--html-defs")
    ap.add_argument("--html", type=num, required=True)
    ap.add_argument("--default-config", type=num, required=True)
    ap.add_argument("--size", type=num, required=True)
    ap.add_argument("--image")
    a = ap.parse_args(argv[1:])
    bad = []

    code_end = web_end = None
    if a.code:
        code = open(a.code, "rb").read().rstrip(b"\0")
        code_end = len(code)
        if code_end > a.html:
            bad.append(f"code ends at 0x{code_end:x}, past the web UI at 0x{a.html:x}")

    if a.html_defs:
        defs = open(a.html_defs).read()
        starts = dict(re.findall(r"#define FDATA_START_(\w+) (0x[0-9a-fA-F]+)", defs))
        sizes = dict(re.findall(r"#define FDATA_SIZE_(\w+) (\d+)", defs))
        web_end = max((int(starts[k], 16) + int(sizes[k]) for k in starts), default=a.html)
        if web_end > a.default_config:
            bad.append(f"web UI ends at 0x{web_end:x}, past the default config at 0x{a.default_config:x}")

    if a.image:
        img = open(a.image, "rb").read()
        n = len(img)
        if n != a.size:
            bad.append(f"image is 0x{n:x} bytes, expected 0x{a.size:x}")
        why = config_bad(img[a.default_config:a.default_config + CONFIG_LEN])
        if why:
            bad.append(f"no valid default config at 0x{a.default_config:x}: {why}")

    if code_end is not None:
        print(f"layout: code 0x0-0x{code_end:x} (web UI from 0x{a.html:x})")
    if web_end is not None:
        print(f"layout: web UI 0x{a.html:x}-0x{web_end:x} (default config at 0x{a.default_config:x})")
    for b in bad:
        print("Error:", b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
