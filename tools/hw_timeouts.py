#!/usr/bin/env python3
"""
hw_timeouts.py - E4's bounded hardware polls, each forced to time out in
rtlsim, on one firmware image, through ovs-rtlpg's old-vs-new bench.

  tools/hw_timeouts.py BOOT_IMAGE [--ovs DIR] [--board FILE] [--limit-us N] [--cases C...]

rtlsim holds a status bit busy (ovs-rtlpg feat/sched-sim 0514263: `reg
hold`, `phy hold`, `flash wp`). For each poll: the hold, a command or page
that polls it, get state.sched (health tasks before P5-2). The command
must finish (its prompt or reply), `hw timeouts` must count it with the poll's register, no step may
be over --limit-us (default 20000: stricter than the scheduler's 50 ms step
limit, decision 25, on purpose: a step waits out one timeout, 5-10 ms, at
most, so a step near 20 ms here means a poll waited more than once), and
after the release the same thing must work
without a timeout:

  table    reg 5cac (TBL_CTRL execute): `vlan 30`, get state.l2 (a poll an entry)
  stat     reg 0f60 (STAT_GET): get state.port.1.mib (55 counters)
  flush    reg 53d4 (L2 flush): `do l2.flush`
  i2c      reg 0418 (I2C_CTRL): an SFP module's read fails, said once
  wel      flash wp (WEL never set): a web config save
  boot     reg 5cac held over a power cut: the boot must reach its prompt
           within 2 s of a normal one (vlan_setup's 4093-entry table scan ends
           at its first timeout; waiting out each would be 20-40 s)
  sds      phy 0.30.3f8 (RTL8224 SDS_INDACS_CMD), with a faked 8224 ID: the boot;
           only a board without 10G ports writes it (dotx_dx_1009n)

--cases picks some (default: all but sds).
Exit status 1 if any check fails.
"""
import argparse, os, re, sys

FINE = re.compile(r"^([A-Z][A-Z0-9_]*)\t(\d+)\t(\S+)$", re.M)
TO = re.compile(r"hw timeouts (\d+)(?: last 0x([0-9a-f]{4}))?")



def health_text(cli):
    """get state.sched.state and state.health as health tasks' text (P5-2
    deleted it): "STATE\\truns\\tNus" lines, "hw timeouts N last 0xREG"."""
    o = cli("get state.sched.state state.health") or ""
    v = dict(re.findall(r"^(state\.\S+) (\S+)$", o, re.M))
    out = ["%s\t%s\t%sus" % (m[1], x, v.get("state.sched.state.%s.max_us" % m[1], "0"))
           for p, x in v.items() for m in [re.fullmatch(r"state\.sched\.state\.(\w+)\.runs", p)] if m]
    reg = int(v.get("state.health.hw_timeout_reg", "0"))
    out.append("hw timeouts %s" % v.get("state.health.hw_timeouts", "?") + (" last 0x%04x" % reg if reg else ""))
    return "\n".join(out) + "\n"

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
    ap.add_argument("--limit-us", type=float, default=20000)
    ap.add_argument("--cases", nargs="*", default=["table", "stat", "flush", "i2c", "wel", "boot"])
    a = ap.parse_args(argv)
    board = a.board or os.path.join(a.ovs, "boards", "keeplink_kp_9000_9xhpml_v1_2.json")
    sys.path.insert(0, os.path.join(a.ovs, "tests", "oldnew"))
    sys.path.insert(0, os.path.join(a.ovs, "tests"))
    from oldnew import Run
    from scenarios import _sfp

    fails = []

    def check(ok, what):
        print(("PASS " if ok else "FAIL ") + what, flush=True)
        if not ok:
            fails.append(what)

    r = Run(a.image, board, os.path.join(a.ovs, "build", "rtlsim"), None, om=("cfg",))	# P3B: a save by POST /cli
    b = r.b
    sim = b.sim
    F, P = r.F, r.P

    def last():
        return r.results[max(r.results)]

    def health():
        h = health_text(lambda l: (r.cli(l, slot=10000), r.results[max(k for k in r.results if k.endswith("cli " + l))])[1])
        m = TO.search(h)
        if not m:
            print("  health tasks gave: " + repr(h[-400:]))
        n, reg = (int(m.group(1)), m.group(2)) if m else (None, None)
        worst = max(((s, us(mx)) for s, _, mx in FINE.findall(h) if s != "FL_ERASE"),
                    key=lambda x: x[1], default=("-", 0))
        return n, reg, worst

    def case(name, reg, hold, release, trigger, ok_reply, after=None):
        """hold; trigger(); its replies must pass ok_reply; counted with reg;
        steps within the limit; release; trigger again (or after()), no timeout"""
        r.cli("do sched.clear")
        n0, _, _ = health()
        r.cli("do sched.clear")
        r._now()
        sim.cmd(hold)
        replies = trigger()
        r._now()
        sim.cmd(release)
        n1, reg1, worst = health()
        check(all(ok_reply(x) for x in replies), f"{name}: finished: {[x[-60:] for x in replies]!r}"[:300])
        check(n1 is not None and n1 > n0 and reg1 == reg,
              f"{name}: counted: hw timeouts {n0} -> {n1}, last {reg1} (want {reg})")
        check(worst[1] <= a.limit_us, f"{name}: longest step {worst[0]} {worst[1]:.0f} us")
        replies = (after or trigger)()
        n2, _, _ = health()
        check(n2 == n1 and all(ok_reply(x) for x in replies), f"{name}: released: no new timeout ({n1} -> {n2})")

    def cli(*lines):
        def f():
            out = []
            for c in lines:
                r.cli(c, slot=8000)
                out.append(last())
            return out
        return f

    def web(*paths):
        def f():
            out = []
            for p in paths:
                r.get(p, slot=8000)
                out.append(last())
            return out
        return f

    prompt = lambda x: x != "<timeout>"
    http = lambda x: isinstance(x, str) and "200 OK" in x
    try:
        t0 = sim.time()[1]
        r.boot()
        normal = sim.time()[1] - t0
        r.link(P[1], True)
        r.wait(1000)
        r.login()
        C = a.cases
        if "table" in C:
          case("table", "5cac", "reg hold 5cac 1", "reg release 5cac",
               lambda: cli(f"vlan 30 {F[1]}", "get state.l2")(), lambda x: prompt(x) and "Error" not in x,
               after=lambda: cli(f"vlan 31 {F[1]}", "get state.l2")() + web("/vlanlist.json")())
          vl = [v for k, v in r.results.items() if k.endswith("GET /vlanlist.json")][-1]
          check('"id":30' in vl and '"id":31' in vl, "table: VLANs 30 and 31 listed after the release")
        if "stat" in C:
          case("stat", "0f60", "reg hold f60 1", "reg release f60",
               cli("get state.port.1.mib"), prompt)
        if "flush" in C:
          case("flush", "53d4", "reg hold 53d4 1ffff", "reg release 53d4", cli("do l2.flush"), prompt)

        if "i2c" in C and 1 in r.cages:
            eeprom = _sfp("10g")
            vendor = eeprom[20:36].decode(errors="replace").strip()

            # not through case(): while held, the read fails and starts over
            # at every poll, and its console lines would split the replies
            r.cli("do sched.clear")
            n0, _, _ = health()
            r._now()
            sim.cmd("reg hold 418 1")
            r.sfp(1, eeprom)
            r.wait(3000)
            r._now()
            held = b.pending()
            sim.cmd("reg release 418")
            r.wait(4000)
            r._now()
            freed = b.pending()
            n1, reg1, worst = health()
            check("I2C read failed" in held and vendor not in held, "i2c: held, the read fails, said")
            check(n1 is not None and n1 > n0 and reg1 == "0418",
                  f"i2c: counted: hw timeouts {n0} -> {n1}, last {reg1} (want 0418)")
            check(worst[1] <= a.limit_us, f"i2c: longest step {worst[0]} {worst[1]:.0f} us")
            check(vendor in freed, "i2c: released, the module read at the next try")
            r.cli("get state.sfp.1")
            check(vendor in last(), "i2c: get state.sfp shows it")
            n2, _, _ = health()
            check(n2 == n1, f"i2c: released: no new timeout ({n1} -> {n2})")
            r.sfp(1, None)
            r.wait(1000)

        def save():
            r.post_config(f"vlan 40 {F[1]}\n", slot=8000)
            return [last()]
        if "wel" in C:
          case("wel", "ff01", "flash wp on", "flash wp off", save, lambda x: x != "<timeout>" and "HTTP" in x)

        # the 8224's SerDes writes are boot work: hold before a power cut
        if "boot" in C:
          r._now()
          sim.cmd("reg hold 5cac 1")
          r.powercut()
          t0 = sim.time()[1]
          r.boot(slot=60000)
          took = sim.time()[1] - t0
          boot = r.results[max(k for k in r.results if k.endswith(" boot"))]
          check(boot != "<timeout>" and took < normal + 2e6,
                f"boot: with the table stuck, the prompt after {took / 1e6:.2f} s (a normal boot {normal / 1e6:.2f} s)")
          sim.cmd("reg release 5cac")
          r.wait(1000)
          r.cli("version")
          n, reg, _ = health()
          check(n is not None and n > 0, f"boot: hw timeouts {n}, last {reg}")
        if "sds" in C:
          r.cli("do sched.clear")
          sim.cmd("reg hold 4 7000")		# CHIP_ID: an RTL8373N (byte 1 0x70): its 8224 SerDes setting
          sim.cmd("phy hold 0 30 5 8224")		# MMD30 reg 5 reads the 8224's ID
          sim.cmd("phy hold 0 30 3f8 8000")	# SDS_INDACS_CMD busy
          r.powercut()
          r.boot(slot=60000)
          boot = r.results[max(k for k in r.results if k.endswith(" boot"))]
          check(boot != "<timeout>", "sds: the boot reached the prompt")
          r.wait(3000)
          r.cli("version")	# the first reply after this boot comes back as link lines only
          n, reg, worst = health()
          check(n is not None and n > 0 and reg == "03f8", f"sds: counted: hw timeouts {n}, last {reg}")
          sim.cmd("phy release all")
          sim.cmd("reg release all")
    finally:
        b.close()
    print(f"hw_timeouts: {len(fails)} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
