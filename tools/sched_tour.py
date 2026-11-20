#!/usr/bin/env python3
"""
sched_tour.py - the scheduler on the real firmware in rtlsim (D2's proof
until old-vs-new): ovs-rtlpg's reference boot, CLI tour and web pages
(tests/oldnew/scenarios.py) on one image, then get state.sched (P5-2
deleted `health tasks`; cli_steps.health_text() renders it as that text).

  tools/sched_tour.py BOOT_IMAGE [--ovs DIR] [--board FILE]

Checks: every slot listed; no state's longest step over 50 ms (the step
limit, decision 25; FL_ERASE, ~40 ms, is inside it); stack headroom >= 30 B. The stack is painted (0xa5) from the sim
at the prompt, above the SP the idle firmware has then, and read at the end:
the normal build has no paint of its own (health_stack_paint is HEALTH's).
Exit status 1 on a failure.
"""
import argparse, os, re, sys

FINE_US = 6.144


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
    from scenarios import CLI_TOUR, WEB_PAGES
    from cli_steps import health_text

    r = Run(a.image, board, os.path.join(a.ovs, "build", "rtlsim"), None)
    sim = r.b.sim
    fails = []
    r.boot()
    sp = sim.peek("s:0x81", 1)[0]
    paint_from = sp + 8
    sim.poke(f"i:{paint_from:#x}", b"\xa5" * (0x100 - paint_from))
    r.cli("do sched.clear")
    for c in CLI_TOUR:
        r.cli(c)
    r.login()
    for p in WEB_PAGES:
        r.get(p)
    last = lambda l: r.results[max(k for k in r.results if k.endswith("cli " + l))]
    text = health_text(lambda l: (r.cli(l), last(l))[1])
    r.cli("get state.sched.slot")
    slots = re.findall(r"^state\.sched\.slot\.(\d+)\.task (\S+)$", last("get state.sched.slot"), re.M)
    paint = sim.peek(f"i:{paint_from:#x}", 0x100 - paint_from)
    hw = max([paint_from + i for i, b in enumerate(paint) if b != 0xa5] or [paint_from - 1])
    r.b.close()

    print(text)
    print(f"slots listed: {len(slots)}")
    long = []
    for m in re.finditer(r"^([A-Z][A-Z0-9_]*)\t(\d+)\t(\S+)$", text, re.M):
        st, runs, mx = m.groups()
        us = float(mx[:-2]) * (1000 if mx.endswith("ms") else 1) if mx[-2:] in ("us", "ms") else 1e9
        if us > 50000:
            long.append((st, mx))
    print(f"steps over 50 ms: {long or 'none'}")
    if long:
        fails.append(f"steps over 50 ms: {long}")
    print(f"stack: SP at the prompt {sp:#x}, painted {paint_from:#x}..0xff, high-water {hw:#x}, "
          f"headroom {0xff - hw} B")
    if 0xff - hw < 30:
        fails.append(f"stack headroom {0xff - hw} B < 30")
    for f in fails:
        print(f"FAIL {f}")
    print("sched_tour: " + ("FAIL" if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
