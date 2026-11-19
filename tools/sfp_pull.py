#!/usr/bin/env python3
"""
sfp_pull.py - an SFP module pulled in the middle of its read (E2 review),
in rtlsim, on one firmware image, through ovs-rtlpg's old-vs-new bench.

  tools/sfp_pull.py BOOT_IMAGE [--ovs DIR] [--board FILE] [--rtlsim PATH]

A module into cage 1; the run stops when the identity cache is complete
(sfp_id_ok[0] = 1, after SFP_READ's fourth transaction) and the module
is pulled there, so a later transaction of the read fails. Then:
/information.json and `sfp` must show no module (no vendor, no PN), the
console must say the read failed, and the slot's caches must be clear;
a module put back is read again and shown. Exit status 1 if any fails.
"""
import argparse, os, re, sys

UPTIME = re.compile(r'"uptime":"?[^,}]*"?')


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--ovs", default=os.path.expanduser("~/src/ovs-rtlpg"))
    ap.add_argument("--board", default=None)
    ap.add_argument("--rtlsim", default=None)
    a = ap.parse_args(argv)
    board = a.board or os.path.join(a.ovs, "boards", "keeplink_kp_9000_9xhpml_v1_2.json")
    sys.path.insert(0, os.path.join(a.ovs, "tests", "oldnew"))
    sys.path.insert(0, os.path.join(a.ovs, "tests"))
    from oldnew import Run
    from scenarios import _sfp

    fails = []
    # rtlsim runs without --map here: the symbols from the build's map
    mp = os.path.join(os.path.dirname(os.path.realpath(a.image)), "rtlplayground.map")
    sym = {m.group(2): int(m.group(1), 16) for m in re.finditer(r"^D:\s+([0-9A-F]+)\s+(_\w+)", open(mp).read(), re.M)}
    loc = lambda n: f"x:0x{sym[n]:x}"

    def check(ok, what):
        print(("PASS " if ok else "FAIL ") + what, flush=True)
        if not ok:
            fails.append(what)

    r = Run(a.image, board, a.rtlsim or os.path.join(a.ovs, "build", "rtlsim"), None)
    b = r.b
    try:
        if 1 not in r.cages:
            print("no SFP cage on this board")
            return 0
        r.boot()
        r.login()
        r.get("/information.json")
        empty = r.results[max(r.results)]
        eeprom = _sfp("10g")
        vendor = eeprom[20:36].decode(errors="replace").strip()
        r.sfp(1, eeprom)
        hit = b.sim.run_until(loc("_sfp_id_ok"), "==", 1, max_ms=10000)
        check(hit is not None, "the read reached sfp_id_ok[0] = 1")
        b.now = b.sim.time()[1]
        b.sfp(1, None)			# out, before the read's next transaction
        r.t = b.now
        r.wait(1000)
        r.get("/information.json")
        info = r.results[max(r.results)]
        r.cli("sfp")
        out = r.results[max(r.results)]
        r._now()
        con = "".join(v for k, v in r.results.items() if k.endswith("console"))
        check("I2C read failed" in con, "the console says the read failed")
        check(vendor not in info, f"/information.json shows no {vendor!r}")
        same = UPTIME.sub("", info) == UPTIME.sub("", empty)
        check(same, "/information.json as before the module (but its uptime)")
        if not same:
            print("  before: " + empty + "\n  after:  " + info)
        check(vendor not in out and "empty" in out, "`sfp`: the slot empty")
        flags = [b.sim.peek(loc(s), 1)[0] for s in ("_sfp_ready", "_sfp_id_ok", "_sfp_diag_ok")]
        check(flags == [0, 0, 0], f"slot 0's ready/id_ok/diag_ok clear: {flags}")
        r.sfp(1, eeprom)		# back in: read again
        r.wait(3000)
        r.get("/information.json")
        check(vendor in r.results[max(r.results)], "back in: /information.json shows it again")
        r.cli("sfp")
        check(vendor in r.results[max(r.results)], "back in: `sfp` shows it")
    finally:
        b.close()
    print(f"sfp_pull: {len(fails)} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
