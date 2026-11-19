#!/usr/bin/env python3
"""
cli_steps.py - each console command's longest step, in rtlsim (E1): the
CLI tour of ovs-rtlpg's old-vs-new scenarios, and the dumps E1 chunks, each
between `health clear` and `health tasks`, on one firmware image.

  tools/cli_steps.py BOOT_IMAGE [--ovs DIR] [--board FILE] [--limit-us N] [--macs N] [--web]

--macs N: N source MACs injected on front port 1 first (rtlsim learns them),
for an l2 table of that size. --web: the same commands as web /cmd too.
Prints each command's longest step per state over the limit (default
5000 us); exit status 1 if any cli state is over it.
"""
import argparse, os, re, struct, sys

FINE = re.compile(r"^([A-Z][A-Z0-9_]*)\t(\d+)\t(\S+)$", re.M)


def us(mx):
    if mx.endswith("us"):
        return float(mx[:-2])
    if mx.endswith("ms"):
        return float(mx.lstrip(">")[:-2]) * 1000
    return 1e9


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--ovs", default=os.path.expanduser("~/src/ovs-rtlpg"))
    ap.add_argument("--board", default=None)
    ap.add_argument("--limit-us", type=float, default=5000)
    ap.add_argument("--macs", type=int, default=0)
    ap.add_argument("--web", action="store_true")
    ap.add_argument("--only", nargs="*")
    a = ap.parse_args(argv)
    board = a.board or os.path.join(a.ovs, "boards", "keeplink_kp_9000_9xhpml_v1_2.json")
    sys.path.insert(0, os.path.join(a.ovs, "tests", "oldnew"))
    sys.path.insert(0, os.path.join(a.ovs, "tests"))
    from oldnew import Run
    from scenarios import CLI_TOUR

    r = Run(a.image, board, os.path.join(a.ovs, "build", "rtlsim"), None)
    b = r.b
    F, P = r.F, r.P
    r.boot()
    for i in (1, 2):
        r.link(P[i], True)
    r.wait(1000)
    setup = ["stp on", f"vlan 10 office {F[1]} {F[2]}t", f"vlan 20 lab {F[2]}", "lacp show"]
    for c in setup:
        r.cli(c)
    if a.macs:
        for n in range(a.macs):
            src = bytes([0x02, 0x00, 0x00, 0x10, n >> 8, n & 0xff])
            f = b"\xff" * 6 + src + struct.pack("!H", 0x88b5) + b"\0" * 46
            b.sim.nic_inject(P[1], f)
            if n % 32 == 31:
                b.advance(b.now + 20000)
        r.wait(2000)
    cmds = list(CLI_TOUR) + [f"port {F[1]}", f"port {F[2]}", "stp status", "health tasks", "history"]
    if a.only:
        cmds = a.only
    worst = {}
    bad = []
    for c in cmds:
        for how in (["cli", "web"] if a.web else ["cli"]):
            r.cli("health clear")
            if how == "cli":
                r.cli(c, slot=60000)
            else:
                r.login() if not r.cookie else None
                r.post_cmd(c, slot=60000)
            r.cli("health tasks")
            text = r.results[max(k for k in r.results if k.endswith("cli health tasks"))]
            over = [(st, mx) for st, runs, mx in FINE.findall(text) if us(mx) > a.limit_us]
            line = f"{how} {c!r}: " + (", ".join(f"{st} {mx}" for st, mx in over) or "-")
            print(line, flush=True)
            for st, mx in over:
                if st.startswith("CLI") or st.endswith("_MORE"):
                    bad.append((how, c, st, mx))
    b.close()
    print(f"cli states over {a.limit_us:.0f} us: {len(bad)}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
