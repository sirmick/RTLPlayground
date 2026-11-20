#!/usr/bin/env python3
"""size_report.py - the object model's size against the base (DESIGN.md 17).

Builds two revisions (default: 53ee574, the last firmware before the object
model, and HEAD) for each board, LAYOUT=compact, each in a detached git
worktree of its own under WORK, with tools/docker-make.sh; then reports per
board, base -> head:

- the three banks' free bytes and the common area left (tools/resource_gate.py);
- xdata in use (the map's XSEG, XISEG and PSEG);
- the code per module, from the .rel files (BANKED, CSEG, CONST, HOME areas),
  the modules that changed;
- the web flash: the region used and free (the build's layout lines), and
  app.js and index.html as stored (gzip -9, as fileadder -z does) and raw.

    tools/size_report.py [--base REV] [--head REV] [--work DIR] [--boards M,M] [--keep] > size.md

A worktree that already has a build of its board is reused (--work keeps
them between runs).
"""
import argparse
import glob
import gzip
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BOARDS = ["KEEPLINK_KP_9000_9XHPML_V1_2", "SWGT024_V2_0_UNMANAGED"]
AREAS = ("BANKED", "CSEG", "CONST", "HOME")


def sh(cmd, cwd=None, log=None):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if log:
        open(log, "w").write(p.stdout + p.stderr)
    if p.returncode:
        sys.exit(f"size_report: {' '.join(cmd)} failed ({p.returncode}): {(p.stdout + p.stderr)[-800:]}")
    return p.stdout + p.stderr


def build(work, rev, board):
    """The revision's worktree and its build directory for the board."""
    wt = os.path.join(work, rev)
    if not os.path.isdir(wt):
        sh(["git", "worktree", "add", "--detach", wt, rev], cwd=ROOT)
    bd = os.path.join(wt, "output", board + "-compact")
    log = os.path.join(work, f"build-{rev}-{board}.log")
    if not glob.glob(os.path.join(bd, "rtlplayground-install-*.bin")) or not os.path.exists(log):
        print(f"size_report: building {rev} {board} ...", file=sys.stderr)
        sh(["tools/docker-make.sh", f"MACHINE={board}", "LAYOUT=compact"], cwd=wt, log=log)
    return wt, bd, open(log).read()


def measure(wt, bd, log):
    m = {}
    g = sh([sys.executable, os.path.join(HERE, "resource_gate.py"), bd])
    for n in (1, 2, 3):
        x = re.search(rf"bank {n}: (\d+) B used, (-?\d+) B free", g)
        m[f"bank{n}"] = int(x.group(2)) if x else None
    x = re.search(r"common area: ends \w+ \(gate \w+, (-?\d+) B left\)", g)
    m["common"] = int(x.group(1)) if x else None
    mp = open(os.path.join(bd, "rtlplayground.map"), errors="replace").read()
    m["xdata"] = 0			# each area once (the map repeats it per page)
    for area in ("XSEG", "XISEG", "PSEG"):
        x = re.search(rf"(?m)^{area}\s.*?=\s+(\d+)\. bytes", mp)
        m["xdata"] += int(x.group(1)) if x else 0
    x = re.search(r"layout: web UI 0x([0-9a-f]+)-0x([0-9a-f]+) \(default config at 0x([0-9a-f]+)\)", log)
    if x:
        a, b, c = (int(v, 16) for v in x.groups())
        m["web_used"], m["web_free"] = b - a, c - b
    for f in ("app.js", "index.html"):
        p = os.path.join(bd, "html_min", f)
        if os.path.exists(p):
            raw = open(p, "rb").read()
            m[f + "_raw"], m[f + "_gz"] = len(raw), len(gzip.compress(raw, 9))
    mods = {}
    for rel in glob.glob(os.path.join(bd, "**", "*.rel"), recursive=True):
        if "/placed/" in rel or "/loader/" in rel:
            continue
        n = 0
        for line in open(rel, errors="replace"):
            x = re.match(r"A (\w+) size (\w+)", line)
            if x and x.group(1) in AREAS:
                n += int(x.group(2), 16)
        mods[os.path.relpath(rel, bd)[:-4]] = n
    m["mods"] = mods
    m["code"] = sum(mods.values())
    return m


def fmt(v):
    return "-" if v is None else f"{v:,}"


def delta(a, b):
    if a is None or b is None:
        return "-"
    return f"{b - a:+,}"


def report(base, head, res, boards):
    out = [f"# Size: {base} -> {head} (tools/size_report.py)", ""]
    for bd in boards:
        a, b = res[(base, bd)], res[(head, bd)]
        out += [f"## {bd} (compact)", "", f"| | {base} | {head} | change |", "|---|---|---|---|"]
        free_a = sum(a[f"bank{n}"] or 0 for n in (1, 2, 3))
        free_b = sum(b[f"bank{n}"] or 0 for n in (1, 2, 3))
        rows = [("banks free (1/2/3)", "/".join(fmt(a[f"bank{n}"]) for n in (1, 2, 3)),
                 "/".join(fmt(b[f"bank{n}"]) for n in (1, 2, 3)), ""),
                ("banks free, total", fmt(free_a), fmt(free_b), delta(free_a, free_b)),
                ("common area left", fmt(a["common"]), fmt(b["common"]), delta(a["common"], b["common"])),
                ("code, all modules", fmt(a["code"]), fmt(b["code"]), delta(a["code"], b["code"])),
                ("xdata (XSEG+XISEG+PSEG)", fmt(a["xdata"]), fmt(b["xdata"]), delta(a["xdata"], b["xdata"])),
                ("web region used", fmt(a.get("web_used")), fmt(b.get("web_used")),
                 delta(a.get("web_used"), b.get("web_used"))),
                ("web region free", fmt(a.get("web_free")), fmt(b.get("web_free")),
                 delta(a.get("web_free"), b.get("web_free")))]
        for f in ("app.js", "index.html"):
            rows.append((f"{f} gz (raw)", f"{fmt(a.get(f + '_gz'))} ({fmt(a.get(f + '_raw'))})",
                         f"{fmt(b.get(f + '_gz'))} ({fmt(b.get(f + '_raw'))})",
                         delta(a.get(f + "_gz"), b.get(f + "_gz"))))
        out += [f"| {r[0]} | {r[1]} | {r[2]} | {r[3]} |" for r in rows]
        out += ["", "Per module, the ones that changed (code bytes, BANKED+CSEG+CONST+HOME):", "",
                f"| module | {base} | {head} | change |", "|---|---|---|---|"]
        names = sorted(set(a["mods"]) | set(b["mods"]),
                       key=lambda n: (b["mods"].get(n, 0) - a["mods"].get(n, 0), n))
        for n in names:
            x, y = a["mods"].get(n), b["mods"].get(n)
            if x != y:
                out.append(f"| {n} | {fmt(x)} | {fmt(y)} | {delta(x or 0, y or 0)} |")
        out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--base", default="53ee574")
    ap.add_argument("--head", default="HEAD")
    ap.add_argument("--work", default=os.path.join(ROOT, "output", "size"))
    ap.add_argument("--boards", default=",".join(BOARDS))
    ap.add_argument("--keep", action="store_true", help="keep the worktrees (else removed at the end)")
    a = ap.parse_args()
    head = sh(["git", "rev-parse", "--short", a.head], cwd=ROOT).strip()
    base = sh(["git", "rev-parse", "--short", a.base], cwd=ROOT).strip()
    boards = a.boards.split(",")
    os.makedirs(a.work, exist_ok=True)
    res = {}
    try:
        for rev in (base, head):
            for bd in boards:
                res[(rev, bd)] = measure(*build(a.work, rev, bd))
    finally:
        if not a.keep:
            for rev in (base, head):
                if os.path.isdir(os.path.join(a.work, rev)):
                    subprocess.run(["git", "worktree", "remove", "--force", os.path.join(a.work, rev)], cwd=ROOT)
    print(report(base, head, res, boards))


if __name__ == "__main__":
    main()
