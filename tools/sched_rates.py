#!/usr/bin/env python3
"""
sched_rates.py - state.sched's rates and load (decision 26) on the real
firmware in rtlsim: the periodic states run as often as their periods say,
and the load is low when the switch idles and high while `get state.l2`
dumps 1000 learned hosts.

  tools/sched_rates.py BOOT_IMAGE [--ovs DIR] [--board FILE]

From the scheduler's xdata (sched_st_rate[], sched_load; the symbols from the
build's .map, as rtlsim runs without --map here), sampled each second of an
idle 5 s after the boot:
  - HOUSE_SEC and LOOP_POLL 1/s; NET_TICK, STP_TICK, LACP_TICK, LLDP_TICK
    once a tick, 200/s (+-1: a second rolls at a pass end, late by up to a
    pass, on a fixed 200-tick grid);
  - the load at most IDLE_MAX %.
`get state.sched.state state.sched.load` prints the same: each state's
.rate as the xdata has it, after its .runs and .max_us, and the load.
Then 1000 hosts are learned behind a front port and `get state.l2` is
typed: sampled every 250 ms until its prompt, the load reaches at least
BUSY_MIN %.

The load against rtlsim's own count: rtlsim run alone (--stats, no control
socket) to 20 s and to 30 s of firmware time gives the CPU's busy machine
cycles over 20-30 s; the firmware's load each second of the same 10 s, in
a bare bench (nothing attached, nothing typed: the same run, the sim being
deterministic), averages within TRUTH_TOL points of it. The firmware's
idle is not 0: two passes a tick over the run sequence and the tick tasks
(~22 % in the sim at fork main's 72 positions).
Exit status 1 on a failure.
"""
import argparse, os, re, subprocess, sys

IDLE_MAX = 30
TRUTH_TOL = 1.5
BUSY_MIN = 50
PER_SEC = {"HOUSE_SEC": (1, 1), "LOOP_POLL": (1, 1)}
PER_TICK = ("NET_TICK", "STP_TICK", "LACP_TICK", "LLDP_TICK")
L2_HOSTS = 1000


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
    from bench import PROMPT

    build = os.path.dirname(os.path.abspath(a.image))
    gen = open(os.path.join(build, "sched", "sched_gen.h")).read()
    names = re.search(r"enum\s*\{\s*S_NONE\s*,(.*?)S_COUNT", gen, re.S).group(1)
    names = ["NONE"] + [n.strip()[2:] for n in names.split(",") if n.strip()]
    xsym = {}
    for l in open(os.path.join(build, "rtlplayground.map")):
        m = re.match(r"\w*:?\s+([0-9A-F]{8})\s+_(sched_st_rate|sched_load)\s", l)
        if m:
            xsym[m.group(2)] = int(m.group(1), 16)

    rtlsim = os.path.join(a.ovs, "build", "rtlsim")
    fails = []

    def check(ok, what):
        print(f"{'ok  ' if ok else 'FAIL'} {what}")
        if not ok:
            fails.append(what)

    from bench import Bench
    busy = []
    for s in (20, 30):
        p = subprocess.run([rtlsim, "--image", a.image, "--board", board, "--cycles", str(s * 31_250_000),
                            "--no-realtime", "--stats", "--quiet"], stdin=subprocess.DEVNULL,
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        m = re.search(r"(\d+) machine cycles .*?, (\d+) busy, ", p.stderr)
        busy.append((int(m.group(1)), int(m.group(2))))
    truth = 100 * (busy[1][1] - busy[0][1]) / (busy[1][0] - busy[0][0])
    b = Bench(a.image, board=board, rtlsim=rtlsim)
    b.advance(20_000_000)
    bare = []
    for _ in range(10):
        b.advance(b.now + 1_000_000)
        bare.append(b.sim.peek(f"x:{xsym['sched_load']:#x}", 1)[0])
    b.close()
    mean = sum(bare) / len(bare)
    check(abs(mean - truth) <= TRUTH_TOL,
          f"20-30 s idle: the firmware's load {bare} %, mean {mean:.1f}; rtlsim's busy cycles {truth:.1f} %")

    r = Run(a.image, board, rtlsim, None)
    sim = r.b.sim

    def sample():
        rn = sim.peek(f"x:{xsym['sched_st_rate']:#x}", 2 * len(names))
        rates = {n: rn[2 * i] | rn[2 * i + 1] << 8 for i, n in enumerate(names)}
        return rates, sim.peek(f"x:{xsym['sched_load']:#x}", 1)[0]

    r.boot()
    r.wait(3000)
    r._now()
    idle = []
    for _ in range(5):
        r.wait(1000)
        r._now()
        idle.append(sample())
    for want, lo_hi in list(PER_SEC.items()) + [(n, (199, 201)) for n in PER_TICK]:
        seen = [s[0][want] for s in idle]
        check(all(lo_hi[0] <= v <= lo_hi[1] for v in seen), f"idle: {want} {seen}/s, want {lo_hi[0]}..{lo_hi[1]}")
    loads = [s[1] for s in idle]
    check(max(loads) <= IDLE_MAX, f"idle: load {loads} %, want <= {IDLE_MAX}")
    print("idle, other states running: " + ", ".join(
        f"{n} {v}" for n, v in sorted(idle[-1][0].items()) if v and n not in PER_SEC and n not in PER_TICK))

    get = "get state.sched.state state.sched.load"
    r.cli(get)
    text = r.results[max(k for k in r.results if k.endswith("cli " + get))]
    lines = dict(re.findall(r"^state\.sched\.state\.(\w+)\.max_us \d+\nstate\.sched\.state\.\1\.rate (\d+)$", text, re.M))
    check(lines.get("HOUSE_SEC") == "1" and len(lines) == len(names) - 1,
          f"get: a .rate per state, after its max_us ({len(lines)} of {len(names) - 1}), HOUSE_SEC {lines.get('HOUSE_SEC')}/s")
    m = re.search(r"^state\.sched\.load (\d+)$", text, re.M)
    check(m is not None and int(m.group(1)) <= 100, f"get: state.sched.load ({m and m.group(1)} %)")

    P = r.P
    r.link(P[1], True)
    r.wait(1000)
    r.learn(P[1], L2_HOSTS)
    r.wait(1000)
    r._now()
    r.b.pending()
    sim.uart_write("get state.l2\r")
    t0, busy, done = r.b.now, [], False
    while r.b.now < t0 + 60_000_000:
        r.b.advance(r.b.now + 250_000)
        rates, load = sample()
        busy.append((load, rates["CLI_MORE"]))
        if re.search(PROMPT, r.b.take_uart()):
            done = True
            break
    r.t = r.b.now
    out = r.b.pending()
    took = (r.b.now - t0) / 1e6
    print(f"get state.l2: {out.count(chr(10))} lines in {took:.2f} s; every 250 ms (load %, CLI_MORE/s): {busy}")
    check(done, "get state.l2: the prompt came back")
    check(max(l for l, _ in busy) >= BUSY_MIN, f"get state.l2: the load reached {max(l for l, _ in busy)} %, want >= {BUSY_MIN}")
    r.wait(2500)
    r._now()
    rates, load = sample()
    check(load <= IDLE_MAX, f"after get state.l2: load {load} %, idle again")
    r.b.close()

    for f in fails:
        print(f"FAIL {f}")
    print("sched_rates: " + ("FAIL" if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
