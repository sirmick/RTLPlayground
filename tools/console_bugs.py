#!/usr/bin/env python3
"""
console_bugs.py - U2 and U4 (doc/sched/upstream-bugs.md) in rtlsim, on one
normal firmware image, through ovs-rtlpg's old-vs-new bench (tests/oldnew).

  tools/console_bugs.py BOOT_IMAGE [--ovs DIR] [--board FILE]

U2: two commands pasted at once ("ip\\rversion\\r"): both run, each with its
    own reply. Today's editor writes the second line's characters into the
    command buffer while the first is still pending.
U4: "ver" typed on the console, then a web POST /cmd "ip", then "sion\\r":
    the web reply is ip's, and the console line still runs as "version".
    Today the web command runs in the console's buffer.
Exit status 1 if either fails (expected on fork main and on D2, 9948af2).
"""
import argparse, os, re, sys


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--ovs", default=os.path.expanduser("~/src/ovs-rtlpg"))
    ap.add_argument("--board", default=None)
    a = ap.parse_args(argv)
    board = a.board or os.path.join(a.ovs, "boards", "keeplink_kp_9000_9xhpml_v1_2.json")
    sys.path.insert(0, os.path.join(a.ovs, "tests", "oldnew"))
    sys.path.insert(0, os.path.join(a.ovs, "tests"))
    from oldnew import Run

    fails = []
    r = Run(a.image, board, os.path.join(a.ovs, "build", "rtlsim"), None)
    try:
        r.boot()
        b = r.b

        # U2
        b.pending()
        b.sim.uart_write("ip\rversion\r")
        b.advance(b.now + 3000000)
        out = b.pending()
        ok = "Current IP:" in out and "Software version:" in out and "Unknown command" not in out
        print(f"U2 paste 'ip\\rversion\\r': {'PASS' if ok else 'FAIL'}")
        print("   " + out.replace("\n", "\n   ").rstrip())
        if not ok:
            fails.append("U2")

        # U4
        r.login()
        b.advance(b.now + 500000)
        b.pending()
        b.sim.uart_write("ver")
        b.advance(b.now + 200000)
        r.post_cmd("ip")
        web = r.results[max(k for k in r.results if k.endswith("web POST /cmd"))]
        r._now()
        b.sim.uart_write("sion\r")
        b.advance(b.now + 2000000)
        out = b.pending()
        ok_web = "Current IP:" in web
        ok_con = "Software version:" in out and "Unknown command" not in out
        print(f"U4 web /cmd 'ip' while 'ver' is typed: web reply {'PASS' if ok_web else 'FAIL'}, "
              f"console line {'PASS' if ok_con else 'FAIL'}")
        print("   web: " + web.split("\n\n", 1)[-1].replace("\n", "\n        ").rstrip())
        print("   console: " + out.replace("\n", "\n            ").rstrip())
        if not (ok_web and ok_con):
            fails.append("U4")
    finally:
        r.b.close()
    print("console_bugs: " + ("FAIL " + " ".join(fails) if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
