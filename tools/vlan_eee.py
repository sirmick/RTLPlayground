#!/usr/bin/env python3
"""
vlan_eee.py - E4's /vlanlist.json and `eee` on/off, in rtlsim, old image
against new, through ovs-rtlpg's old-vs-new bench (tests/oldnew).

  tools/vlan_eee.py NEW_BOOT_IMAGE --old OLD_BOOT_IMAGE [--ovs DIR] [--board FILE]

One scenario on both images: VLANs created, renamed and deleted from the
console, from a /cmd and from a config loaded at boot; /vlanlist.json and
the VLAN table after each; `eee on|off`, all ports and one, and `eee
status`, from the console and from a /cmd. Every reply, the console
between them, and the tables must be the same on both. On the new image
the same steps run between `health clear` and `health tasks` (not
compared): the longest RX_STEP of each /vlanlist.json must be under
--limit-us (default 5000), and every step of an eee command too. Two
replies are full (a 40-VLAN table with 20-character names; and after a
reset, where rtlsim keeps the VLAN table): their times are printed, not
checked, as the reply's size bounds them. Exit status 1 if any differ or
is over.
"""
import argparse, os, re, sys

FINE = re.compile(r"^([A-Z][A-Z0-9_]*)\t(\d+)\t(\S+)$", re.M)
NVLANS = 40
VERSION = re.compile(r"^(Software version|Build date): .*$", re.M)	# the images' own


def us(mx):
    if mx.endswith("us"):
        return float(mx[:-2])
    if mx.endswith("ms"):
        return float(mx.lstrip(">")[:-2]) * 1000
    return 1e9


def scenario(r, measure):
    """The steps; measure(what, fn) runs fn, between health clear and
    health tasks on the new image."""
    F, P = r.F, r.P
    r.boot()
    for i in (1, 2):
        r.link(P[i], True)
    r.wait(1000)
    r.login()
    measure("vlanlist boot", lambda: r.get("/vlanlist.json"))
    for c in (f"vlan 10 office {F[1]} {F[2]}t", f"vlan 20 lab {F[2]}", f"vlan 4094 top {F[1]}",
              f"vlan 100 {F[3]}", f"vlan 4000 x{'y' * 20} {F[1]}t", "vlan 20 d", "vlan 3000 d",
              f"vlan 10 renamed {F[1]}"):
        r.cli(c)
    r.post_cmd(f"vlan 21 web {F[2]}\nvlan 22 {F[3]}t\nvlan 100 d")
    measure("vlanlist cli", lambda: r.get("/vlanlist.json"))
    r.tables()
    for v in range(200, 200 + NVLANS):	# more than fit: the reply's own limit
        r.cli(f"vlan {v} n{v}-{'z' * 16} {F[1]}")
    measure("vlanlist full", lambda: r.get("/vlanlist.json"), check=False)
    r.tables()
    cfg = "".join(f"vlan {v} c{v} {F[2]}t\n" for v in (5, 6, 7, 1234)) + "vlan 6 d\n"
    r.post_config(cfg)
    r.cli("reset", slot=1000)
    r.boot()
    for i in (1, 2):
        r.link(P[i], True)
    r.wait(1000)
    r.login()
    measure("vlanlist config", lambda: r.get("/vlanlist.json"), check=False)
    r.tables()
    for c in ("eee status", "eee on", "eee status", "eee off", "eee status", f"eee on {F[1]} 1g",
              f"eee on {F[2]}", f"eee off {F[1]}", "eee on 100m", "eee status", "eee off"):
        measure(c, lambda c=c: r.cli(c, slot=8000))
    measure("web eee on", lambda: r.post_cmd("eee on", slot=8000))
    measure("web eee off 2", lambda: r.post_cmd(f"eee off {F[1]}\neee status {F[1]}", slot=8000))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--old", required=True)
    ap.add_argument("--ovs", default=os.path.expanduser("~/src/ovs-rtlpg"))
    ap.add_argument("--board", default=None)
    ap.add_argument("--limit-us", type=float, default=5000)
    a = ap.parse_args(argv)
    board = a.board or os.path.join(a.ovs, "boards", "keeplink_kp_9000_9xhpml_v1_2.json")
    sys.path.insert(0, os.path.join(a.ovs, "tests", "oldnew"))
    sys.path.insert(0, os.path.join(a.ovs, "tests"))
    from oldnew import Run

    runs, bad = {}, []
    for which, img in (("old", a.old), ("new", a.image)):
        r = Run(img, board, os.path.join(a.ovs, "build", "rtlsim"), None)
        seq = []

        def measure(what, fn, check=True, r=r, which=which):
            if which == "new":
                r.cli("health clear")
            fn()
            if which != "new":
                return
            r.cli("health tasks")
            text = r.results[max(k for k in r.results if k.endswith("cli health tasks"))]
            st = {s: us(mx) for s, runs_, mx in FINE.findall(text)}
            if what.startswith("vlanlist"):
                worst = [("RX_STEP", st.get("RX_STEP", 0))]
            else:
                worst = sorted(st.items(), key=lambda x: -x[1])[:3]
            line = f"{what}: " + ", ".join(f"{s} {v:.0f} us" for s, v in worst)
            if not check:
                line += " (a full reply: its size bounds it; not checked)"
            print(line, flush=True)
            if check and worst and worst[0][1] > a.limit_us:
                bad.append(line)

        try:
            scenario(r, measure)
            r._now()
            r._console()
        finally:
            r.b.close()
        # the harness's own health lines aside, in order
        for k in sorted(r.results):
            name = k.split(" ", 1)[1]
            if "health" in name:
                continue
            seq.append((name, VERSION.sub("", r.results[k])))
        runs[which] = seq

    o, n = runs["old"], runs["new"]
    diffs = 0
    for i in range(max(len(o), len(n))):
        x = o[i] if i < len(o) else ("<none>", "")
        y = n[i] if i < len(n) else ("<none>", "")
        if x != y:
            diffs += 1
            print(f"DIFF #{i} old {x[0]!r} / new {y[0]!r}")
            print("  old: " + repr(x[1])[:600])
            print("  new: " + repr(y[1])[:600])
    print(f"vlan_eee: {len(n)} results, {diffs} differ; {len(bad)} over {a.limit_us:.0f} us")
    return 1 if diffs or bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
