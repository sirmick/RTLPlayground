#!/usr/bin/env python3
"""
l2_walk.py - the console `l2` dump must end even if the entry it found first
is deleted while it walks (FL; upstream U11), in rtlsim, on one firmware
image, through ovs-rtlpg's old-vs-new bench.

  tools/l2_walk.py BOOT_IMAGE [--ovs DIR] [--board FILE] [--hosts N]

The walk ends when the table search comes back to the index it found
first (l2_first). On silicon that entry aged out mid-dump (F3 soak) and the
dump printed the same entries for minutes. rtlsim's control mode doesn't
age entries, so the deletion is made where the firmware sees it: l2_first
is poked (from the build's .map and rtl837x_port.sym) to an index the
table doesn't hold, a few entries into the dump. Two runs:
  plain    no poke: the dump ends, each entry once
  deleted  the poke: the dump must still end within 3 s, each entry once
Exit status 1 if not.
"""
import argparse, os, re, sys

MAC = re.compile(r"^([0-9a-f]{2}(?::[0-9a-f]{2}){5})\t", re.M)


def l2_first_addr(image):
    """l2_first is static: its offset in rtl837x_port's XSEG (.sym) from
    the module's _l2_head, whose address the .map has."""
    d = os.path.dirname(os.path.abspath(image))
    sym = open(os.path.join(d, "rtl837x_port.sym")).read()
    off = {n: int(v, 16) for n, v in re.findall(r"B (_l2_head|_l2_first)\s+([0-9A-Fa-f]+)", sym)}
    m = re.search(r"D:\s+([0-9A-Fa-f]+)\s+_l2_head\s", open(os.path.join(d, "rtlplayground.map")).read())
    return int(m.group(1), 16) + off["_l2_first"] - off["_l2_head"]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--ovs", default=os.path.expanduser("~/src/ovs-rtlpg"))
    ap.add_argument("--board", default=None)
    ap.add_argument("--hosts", type=int, default=20)
    a = ap.parse_args(argv)
    board = a.board or os.path.join(a.ovs, "boards", "keeplink_kp_9000_9xhpml_v1_2.json")
    sys.path.insert(0, os.path.join(a.ovs, "tests", "oldnew"))
    sys.path.insert(0, os.path.join(a.ovs, "tests"))
    from oldnew import Run
    from bench import PROMPT

    addr = l2_first_addr(a.image)
    fails = []

    def check(ok, what):
        print(("PASS " if ok else "FAIL ") + what, flush=True)
        if not ok:
            fails.append(what)

    for name in ("plain", "deleted"):
        r = Run(a.image, board, os.path.join(a.ovs, "build", "rtlsim"), None)
        b, sim = r.b, r.b.sim
        try:
            r.boot()
            r.link(r.P[1], True)
            r.wait(1000)
            r.learn(r.P[1], a.hosts)
            r.wait(1000)
            r._now()
            r.cli("version")
            idx = {int(l.split()[0], 16) for l in b.table("l2")}
            n = len(idx)
            b.pending()
            sim.uart_write("l2\r")
            if name == "deleted":
                b.advance(b.now + 2000000, lambda: len(MAC.findall(b.take_uart())) >= 3)
                gone = max(i for i in range(0x1000) if i not in idx)
                sim.poke(f"x:0x{addr:04x}", bytes([gone & 0xff, gone >> 8]))
                print(f"deleted: l2_first (x:0x{addr:04x}) poked to 0x{gone:03x}, not in the table", flush=True)
            done = re.compile(r"\tport\n[\s\S]*?" + PROMPT)	# the prompt after the header
            ended = b.advance(b.now + 3000000, lambda: done.search(b.take_uart()) is not None)
            out = b.pending()
            macs = MAC.findall(out)
            print(f"{name}: {n} entries in the table, {len(macs)} printed, "
                  f"{len(set(macs))} different; prompt {'back' if ended else 'not back in 3 s'}", flush=True)
            check(ended, f"{name}: the dump ends")
            check(len(macs) == n and len(set(macs)) == n, f"{name}: each entry printed once")
        finally:
            b.close()
    print(f"l2_walk: {len(fails)} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
