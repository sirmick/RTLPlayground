#!/usr/bin/env python3
"""test_place_banks.py - tools/place_banks.py's packing, on .rel files made
up for it (sizes only, no calls between them).

The greedy packings (shared out, then best fit) fail on group sizes that
P3C's 8x build had (a 793 B group left with no bank, ~550 B free in all);
the search packs them. A set the greedy packs keeps the greedy's
placement; one that can't fit fails, in bounded time; the same input packs
the same way twice.
"""
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "tools", "place_banks.py")
fails = 0

# P3C's 8x groups before 395edf2 merged om's handlers into om's group:
# bank 1's pinned group (httpd, html_data ...), bank 3's, then the rest
PINNED = {1: 32865, 3: 736}
GROUPS = [19859, 17673, 7679, 6896, 5735, 5458, 4397, 3267, 3212, 3207, 2685, 2685,
          2496, 2265, 2165, 2088, 1987, 1912, 1757, 1749, 1664, 1651, 1528, 1386,
          1220, 1176, 1090, 1069, 876, 864, 793, 547, 111, 58, 3]


def check(cond, name, detail=""):
    global fails
    print(("PASS  " if cond else "FAIL  ") + name)
    if not cond:
        fails += 1
        if detail:
            print("      " + detail.strip().replace("\n", "\n      "))


def place(groups, pinned=PINNED):
    """(exit status, stderr, banks.txt)"""
    d = tempfile.mkdtemp()
    banked, other = [], []
    for i, size in enumerate(groups):
        rel = os.path.join(d, "m%02d.rel" % i)
        with open(rel, "w") as f:
            f.write("A BANKED size %X flags 20 addr 0\nS _f%02d Def0000\n" % (size, i))
        banked.append(rel)
    for b, size in pinned.items():
        rel = os.path.join(d, "pin%d.rel" % b)
        with open(rel, "w") as f:
            f.write("A BANK%d size %X flags 20 addr 0\nS _p%d Def0000\n" % (b, size, b))
        other.append(rel)
    r = subprocess.run([sys.executable, TOOL, d] + banked + ["--"] + other,
                       capture_output=True, text=True)
    txt = ""
    if os.path.exists(os.path.join(d, "banks.txt")):
        txt = open(os.path.join(d, "banks.txt")).read()
    return r.returncode, r.stderr, txt


def banks(txt):
    return [l for l in txt.split("\n") if l and not l.startswith("#")]


# the greedy fails, the search packs
st, err, txt = place(GROUPS)
check(st == 0 and "packed by a search" in err, "search: packs what the greedy can't", err)
used = {}
for l in banks(txt):
    n, b = l.split()[:2]
    if n.startswith("m"):
        used[int(b)] = used.get(int(b), 0) + GROUPS[int(n[1:])]
for b, size in PINNED.items():
    used[b] = used.get(b, 0) + size
check(all(v <= 0xC000 for v in used.values()) and len(banks(txt)) == len(GROUPS) + len(PINNED),
      "search: every group in a bank, none over 48 KiB", str(used))
st2, _, txt2 = place(GROUPS)
check(st2 == 0 and txt2 == txt, "search: the same input, the same placement")

# what the greedy packs stays the greedy's (the first groups shrink by 100 B)
easy = [GROUPS[0] - 100] + GROUPS[1:]
st, err, txt = place(easy)
check(st == 0 and "search" not in err and "best fit" not in err,
      "greedy: a set it packs doesn't reach the search", err)

# too big: fails, in bounded time
t = time.time()
st, err, _ = place(GROUPS + [700])
check(st == 1 and "no bank has room" in err and time.time() - t < 60,
      "search: a set that can't fit fails (%.1f s)" % (time.time() - t), err)

print("\n%s" % ("ALL PASS" if not fails else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
