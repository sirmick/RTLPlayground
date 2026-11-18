#!/usr/bin/env python3
"""
resource_report.py - the resource picture of a linked build (doc/sched/memory.md).

  resource_report.py BUILDDIR

Internal RAM from the .mem map (data, overlay, bits, stack), the common
area's end, xdata's end and each bank's use from the .map. B6's resource
gate checks these against a baseline.
"""
import os, re, sys

BANK_SIZE = 0xC000


def areas(mapfile):
    out = {}
    for name, addr, size in re.findall(r"^(\S+)\s+([0-9A-F]{8})\s+([0-9A-F]{8}) =", open(mapfile).read(), re.M):
        out.setdefault(name, (int(addr, 16), int(size, 16)))
    return out


def iram(memfile):
    mem = open(memfile).read()
    grid = "".join(l.split(":", 1)[1] for l in mem.splitlines()
                   if re.match(r"0x[0-9a-f]0:\|", l)).replace("|", "")
    stack = re.search(r"with (\d+) bytes available", mem)
    return dict(data=sum(c.islower() for c in grid), overlay=grid.count("Q"),
                bits=grid.count("B"), stack=int(stack.group(1)) if stack else 0)


def report(builddir):
    a = areas(os.path.join(builddir, "rtlplayground.map"))
    r = iram(os.path.join(builddir, "rtlplayground.mem"))
    xinit = a["XINIT"]
    xiseg = a.get("XISEG", a["XSEG"])
    r["common_end"] = xinit[0] + xinit[1]
    r["xdata_end"] = xiseg[0] + xiseg[1]
    r["banks"] = [a[f"BANK{b}"][1] if f"BANK{b}" in a else 0 for b in (1, 2, 3)]
    return r


def main(argv):
    if len(argv) != 1:
        sys.exit(__doc__)
    r = report(argv[0])
    print(f"resources ({argv[0]}):")
    print(f"  internal RAM: data {r['data']} B, overlay {r['overlay']} B, bits {r['bits']} B, "
          f"stack {r['stack']} B")
    print(f"  common area:  ends 0x{r['common_end']:04x} ({0x3f80 - r['common_end']} B to the 0x3f80 gate)")
    print(f"  xdata:        ends 0x{r['xdata_end']:04x}")
    print("  banks:        " + ", ".join(f"{b}: {u} B ({BANK_SIZE - u} free)"
                                         for b, u in zip((1, 2, 3), r["banks"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
