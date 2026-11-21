#!/usr/bin/env python3
"""
boot_stuck.py - a poll timeout early in the boot must not spoil the boot's
later table work (reviewer2, E4), in rtlsim, on one firmware image for
dotx_dx_1009n, through ovs-rtlpg's old-vs-new bench.

  tools/boot_stuck.py BOOT_IMAGE [--ovs DIR] [--board FILE] [--busy N]

rtlsim (ovs-rtlpg feat/sched-sim 999584e) makes each table command take
time, as on silicon: `reg busy 5cac 1 N` keeps TBL_CTRL's execute bit set
for N reads, and only then applies the command. A command written while
one is busy is dropped (`checks`: table-busy). A saved config with VLANs
is booted twice from a power cut, both with an RTL8373N and an 8224 faked
(CHIP_ID, MMD30 reg 5), so the boot takes the 8224 SerDes path:
  plain  the 8224's SDS_INDACS_CMD reads ready
  early  it reads busy (phy hold 0 30 3f8 8000): rtl8224_sds_write times
         out twice, before vlan_setup and the config's replay
The VLAN table and `get vlan` must be the same after both boots, and
neither may drop a table command. Exit status 1 if not.
"""
import argparse, os, re, sys

# the saved config: VLANs 10 (2, 3 tagged), 300 (4 tagged), 4000 (2); 20 made and deleted
CFG = ["set vlan.10.name ten", "set vlan.10.members 2,3", "set vlan.10.tagged 3",
       "set vlan.20.name twenty", "set vlan.20.members 3", "set vlan.300.members 4", "set vlan.300.tagged 4",
       "del vlan.20", "set vlan.4000.name top", "set vlan.4000.members 2", "apply", "save"]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--ovs", default=os.path.expanduser("~/src/ovs-rtlpg"))
    ap.add_argument("--board", default=None)
    ap.add_argument("--busy", type=int, default=2)
    a = ap.parse_args(argv)
    board = a.board or os.path.join(a.ovs, "boards", "dotx_dx_1009n.json")
    sys.path.insert(0, os.path.join(a.ovs, "tests", "oldnew"))
    sys.path.insert(0, os.path.join(a.ovs, "tests"))
    from oldnew import Run
    from cli_steps import health_text

    fails = []

    def check(ok, what):
        print(("PASS " if ok else "FAIL ") + what, flush=True)
        if not ok:
            fails.append(what)

    out = {}
    for name in ("plain", "early"):
        r = Run(a.image, board, os.path.join(a.ovs, "build", "rtlsim"), None)
        b, sim = r.b, r.b.sim
        try:
            r.boot()
            for l in CFG:
                r.cli(l, slot=8000)
            r.wait(2000)
            r._now()
            sim.cmd("reg hold 4 7000")		# CHIP_ID: an RTL8373N, its 8224 SerDes setting
            sim.cmd("phy hold 0 30 5 8224")	# MMD30 reg 5: the 8224's ID
            if name == "early":
                sim.cmd("phy hold 0 30 3f8 8000")	# SDS_INDACS_CMD busy: two timeouts early in the boot
            sim.cmd(f"reg busy 5cac 1 {a.busy}")
            n0 = sum(c[1] == "table-busy" for c in sim.checks())
            r.powercut()
            r.boot(slot=60000)
            r.wait(3000)
            r.cli("get state.sys.version")	# the first reply after this boot is link lines only
            text = health_text(lambda l: (r.cli(l, slot=10000),	# P5-2 deleted health tasks
                                          r.results[max(k for k in r.results if k.endswith("cli " + l))])[1])
            m = re.search(r"hw timeouts (\d+)(?: last 0x([0-9a-f]{4}))?", text)
            r.cli("get vlan", slot=10000)
            vl = r.results[max(k for k in r.results if k.endswith("cli get vlan"))]
            r._now()
            table = "\n".join(b.table("vlan"))
            drops = [c[2] for c in sim.checks() if c[1] == "table-busy"][n0:]
            out[name] = (vl, table)
            print(f"{name}: hw timeouts {m.group(1) if m else '?'} last {m.group(2) if m else '-'}; "
                  f"{len(drops)} table commands dropped", flush=True)
            for d in drops[:5]:
                print("  " + d)
            check(not drops, f"{name}: no table command dropped")
            if name == "early":
                check(m and int(m.group(1)) > 0 and m.group(2) == "03f8", "early: the 8224 SDS wait timed out")
            check("vlan.10.members" in vl and "vlan.4000.name top" in vl and "vlan.20." not in vl,
                  f"{name}: get vlan has the config's VLANs")
        finally:
            b.close()
    check(out["plain"][1] == out["early"][1], "the VLAN table the same after both boots")
    check(out["plain"][0] == out["early"][0], "get vlan the same")
    print(f"boot_stuck: {len(fails)} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
