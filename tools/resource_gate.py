#!/usr/bin/env python3
"""
resource_gate.py - the scheduler's resource gate (doc/sched/memory.md).

  resource_gate.py BUILDDIR [--baseline FILE] [--update]

Reads a linked build (rtlplayground.map, rtlplayground.mem, the .asm files)
and checks it, against FILE (JSON) where a check needs a baseline:

  FAIL  internal RAM (data, overlay or bits, counted from the .mem map) grew
        against the baseline: every new DSEG symbol is listed by name. The
        scheduler adds none (memory.md); growth that is meant needs the
        baseline updated in the same commit (--update writes it)
  FAIL  the stack below 120 B
  FAIL  the common area ending above 0x3f80
  WARN  a bank above 47 KiB (free bytes are always reported)
  note  new DSEG symbols even when the total did not grow (names moved)

Exit status 1 on any FAIL. --update writes FILE from BUILDDIR instead.
"""
import argparse, glob, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from resource_report import areas, iram, BANK_SIZE    # noqa: E402

STACK_MIN = 120
COMMON_MAX = 0x3f80
BANK_WARN = 47 * 1024
SUFFIX = re.compile(r"_\d+_\d+$")       # SDCC's numbering of locals and slocs


def dseg_symbols(builddir):
    """'file symbol: size' for every DSEG label in the application's .asm
    files (the loader is a program of its own), numbering suffixes cut."""
    out = []
    for f in sorted(glob.glob(os.path.join(builddir, "**", "*.asm"), recursive=True)):
        rel = os.path.relpath(f, builddir)
        if rel.startswith(("loader" + os.sep, "placed" + os.sep, "tmp" + os.sep)):
            continue
        text = open(f).read()
        for blk in re.findall(r"\.area DSEG\s*\(DATA\)(.*?)(?=\.area|\Z)", text, re.S):
            for lab, n in re.findall(r"^(\S+):\s*\n\s*\.ds (\d+)", blk, re.M):
                out.append(f"{rel} {SUFFIX.sub('', lab)}: {n}")
    return out


def kind(sym):
    name = sym.split()[1]
    if "_sloc" in name:
        return "sloc"
    if "_PARM_" in name:
        return "PARM"
    return "local/static"


def measure(builddir):
    a = areas(os.path.join(builddir, "rtlplayground.map"))
    r = iram(os.path.join(builddir, "rtlplayground.mem"))
    xinit = a["XINIT"]
    r["common_end"] = xinit[0] + xinit[1]
    r["banks"] = [a[f"BANK{b}"][1] if f"BANK{b}" in a else 0 for b in (1, 2, 3)]
    r["dseg"] = dseg_symbols(builddir)
    return r


def gate(builddir, baseline_file):
    m = measure(builddir)
    base = json.load(open(baseline_file)) if baseline_file else None
    fails, warns, lines = [], [], []
    bl = (lambda k: f" (baseline {base[k]})") if base else (lambda k: "")

    lines.append(f"internal RAM: data {m['data']} B{bl('data')}, overlay {m['overlay']} B{bl('overlay')}, "
                 f"bits {m['bits']} B{bl('bits')}")
    new = sorted(set(m["dseg"]) - set(base["dseg"])) if base else []
    gone = sorted(set(base["dseg"]) - set(m["dseg"])) if base else []
    if base:
        grew = [k for k in ("data", "overlay", "bits") if m[k] > base[k]]
        if grew:
            fails.append("internal RAM grew: " + ", ".join(f"{k} {base[k]} -> {m[k]} B" for k in grew)
                         + ". The scheduler adds none (doc/sched/memory.md): keep step data __xdata, "
                         "no second parameters, no 32-bit temporaries in non-leaf functions. "
                         "If the growth is meant, update the baseline in the same commit: "
                         f"tools/resource_gate.py {builddir} --baseline {baseline_file} --update")
        shrank = [k for k in ("data", "overlay", "bits") if m[k] < base[k]]
        if shrank:
            lines.append("  less than the baseline (" + ", ".join(f"{k} {base[k]} -> {m[k]}" for k in shrank)
                         + "): the baseline can be lowered with --update")
    if new:
        lines.append(f"new DSEG symbols ({len(new)}):")
        lines += [f"  + {s}  [{kind(s)}]" for s in new]
    if gone:
        lines.append(f"DSEG symbols gone ({len(gone)}):")
        lines += [f"  - {s}" for s in gone]
    if base and not new:
        lines.append(f"DSEG symbols: {len(m['dseg'])}, none new")

    lines.append(f"stack: {m['stack']} B (at least {STACK_MIN})")
    if m["stack"] < STACK_MIN:
        fails.append(f"the stack is {m['stack']} B, under {STACK_MIN} B: internal RAM is full")

    left = COMMON_MAX - m["common_end"]
    lines.append(f"common area: ends 0x{m['common_end']:04x} (gate 0x{COMMON_MAX:04x}, {left} B left)")
    if left < 0:
        fails.append(f"the common area ends at 0x{m['common_end']:04x}, {-left} B above 0x{COMMON_MAX:04x}: "
                     "move code into a bank (doc/sched/memory.md)")

    for b, used in zip((1, 2, 3), m["banks"]):
        lines.append(f"bank {b}: {used} B used, {BANK_SIZE - used} B free")
        if used > BANK_WARN:
            warns.append(f"bank {b} holds {used} B, over 47 KiB ({BANK_SIZE - used} B free)")
    return m, fails, warns, lines


def main(argv):
    ap = argparse.ArgumentParser(description="the scheduler's resource gate")
    ap.add_argument("builddir")
    ap.add_argument("--baseline")
    ap.add_argument("--update", action="store_true", help="write the baseline from BUILDDIR")
    a = ap.parse_args(argv)
    if a.update:
        if not a.baseline:
            ap.error("--update needs --baseline")
        m = measure(a.builddir)
        out = {k: m[k] for k in ("data", "overlay", "bits", "stack", "banks")}
        out["common_end"] = f"0x{m['common_end']:04x}"
        out["dseg"] = m["dseg"]
        with open(a.baseline, "w") as f:
            json.dump(out, f, indent=1)
            f.write("\n")
        print(f"resource gate: baseline {a.baseline} written from {a.builddir}")
        return 0
    m, fails, warns, lines = gate(a.builddir, a.baseline)
    print(f"resource gate: {a.builddir}" + (f" against {a.baseline}" if a.baseline else " (no baseline)"))
    for l in lines:
        print("  " + l)
    for w in warns:
        print("  WARN " + w)
    for f in fails:
        print("  FAIL " + f)
    print("  " + ("FAIL" if fails else "PASS") + (f" ({len(warns)} warnings)" if warns else ""))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
