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
  - the final image is not IMAGESIZE bytes.

Usage: check_layout.py --html <addr> --default-config <addr> --size <bytes>
         [--code <imagebuilder output>] [--html-defs html_data.h] [--image <final.bin>]
"""
import sys, re, argparse


def num(s):
    return int(s, 0)


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
        n = len(open(a.image, "rb").read())
        if n != a.size:
            bad.append(f"image is 0x{n:x} bytes, expected 0x{a.size:x}")

    if code_end is not None:
        print(f"layout: code 0x0-0x{code_end:x} (web UI from 0x{a.html:x})")
    if web_end is not None:
        print(f"layout: web UI 0x{a.html:x}-0x{web_end:x} (default config at 0x{a.default_config:x})")
    for b in bad:
        print("Error:", b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
