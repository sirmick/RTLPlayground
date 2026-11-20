#!/usr/bin/env python3
"""test_om_codec.py - tools/rtlpg_client.py's codec against the firmware's.

test_om (om.c on the host) leaves saved sectors in build/ with the text of
the config each holds: om_codec_<name>.bin and .txt (get -d's lines; for
"skip", a hand-made sector, get -s's). The codec must turn the text into
the same bytes and the bytes into the same text, and skip what the load
skipped. Then the codec alone: the real schema on other boards, its errors,
a sector it refuses.
"""
import glob
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import rtlpg_client as rc  # noqa: E402

fails = 0
runs = 0


def check(cond, msg, detail=""):
    global fails, runs
    runs += 1
    print(("  ok:   " if cond else "  FAIL: ") + msg)
    if not cond:
        fails += 1
        if detail:
            print("        " + str(detail).replace("\n", "\n        "))


def main():
    build = os.path.join(HERE, "build")
    sch = rc.Schema([os.path.join(HERE, "..", "schema"), os.path.join(HERE, "om_schema")])
    print("C (test_om) against Python, the same configs")
    names = sorted(os.path.basename(p)[9:-4] for p in glob.glob(os.path.join(build, "om_codec_*.bin")))
    check(len(names) >= 22, "test_om left its samples (%d)" % len(names))
    for n in names:
        b = open(os.path.join(build, "om_codec_%s.bin" % n), "rb").read()
        t = open(os.path.join(build, "om_codec_%s.txt" % n)).read().splitlines()
        if n == "skip":
            skipped = []
            got = rc.bin2txt(sch, b, secrets=False, skipped=skipped)
            check(got == t, "skip: bin2txt prints what get -s printed", "\n".join(got))
            check(len(skipped) == 12, "skip: and skips what get -s's load skipped (12: the 0x0904 storm, port 10, the 5th tvlan)", "\n".join(skipped))
            continue
        got = rc.bin2txt(sch, b)
        check(got == t, "%s: bin2txt is get -d (keyed: its dels and empty entries too)" % n, "\n".join(got))
        mine = rc.txt2bin(sch, t)
        check(mine == b, "%s: txt2bin of get -d is the firmware's save (%d B)" % (n, len(b)),
              "python %s\nC      %s" % (mine.hex(), b.hex()))

    print("the saved config at its largest: om_gen's bound is the codec's bytes")
    worst_case()
    print("the real schema, other boards")
    sw = rc.Schema(board=rc.Board("SWGT024_V2_0_UNMANAGED"))
    b = rc.txt2bin(sw, ["set port.1.isolate 1,6", "port.6.mtu 9000"])
    # vlan's reset and VLAN 1 (its members against an entry's 0), as the firmware saves the defaults:
    # first, vlan applying before port (DESIGN 6.21)
    def vdef(members):
        return bytes([9, 0, 0, 0, 9, 1, 0, 1, 2, 2]) + struct.pack("<H", members)
    # front 1 is logical 4, front 6 logical 3: bits 4 and 3
    check(b[8:] == vdef(0x1f8) + bytes([1, 1, 0, 1, 11, 2, 0x18, 0]) + bytes([1, 6, 0, 1, 5, 2]) +
          struct.pack("<H", 9000), "SWGT024: front-panel numbers to logical bits, the index as typed", b[8:].hex())
    check(rc.bin2txt(sw, b) == ["port.1.isolate 1,6", "port.6.mtu 9000"], "and back", rc.bin2txt(sw, b))
    # keyed (P4-2): a default entry deleted, an entry with nothing off its default
    for t in (["del vlan.1", "vlan.20.name "], ["vlan.1.members 2", "vlan.20.name ", "vlan.30.tagged 1", "vlan.30.members 1"]):
        got = rc.bin2txt(rc.Schema(), rc.txt2bin(rc.Schema(), t))
        want = [ln for ln in t if ln != "vlan.30.tagged 1"] + (["vlan.30.tagged 1"] if "vlan.30.tagged 1" in t else [])
        check(got == want, "keyed round trip: %s" % " / ".join(t), "\n".join(got))
    b8 = rc.txt2bin(rc.Schema(), [])
    check(b8[8:] == vdef(0x1ff), "the defaults: a header and VLAN 1's records", b8.hex())
    for bad, why in (("port.10.mtu 100", "no such port"), ("port.1.mtu 63", "64-16383"),
                     ("port.9.eee on", "not on this port"), ("port.1.nope 1", "no such path"),
                     ('port.1.name a"b', "printable")):
        try:
            rc.txt2bin(rc.Schema(), [bad])
            check(False, "refused: " + bad)
        except rc.Bad as e:
            check(why in str(e), "refused: %s (%s)" % (bad, e))
    for data, why in ((b"\xff" * 8, "blank"), (b"ip 1.2.3.4\n", "bad"),
                      (b8[:6] + b"\1\0", "bad")):
        try:
            rc.bin2txt(rc.Schema(), data)
            check(False, "a sector it refuses: " + why)
        except rc.Bad as e:
            check(why in str(e), "a sector it refuses: %s (%s)" % (why, e))

    convert()
    print("\n%d checks, %d failed" % (runs, fails))
    return 1 if fails else 0


OLD = """ip 192.168.10.247\r
gw 192.168.10.1
netmask 255.255.255.0
hostname bench-4+2
vlan 100 lab 1 2t 9t
vlan 200 3 4
vlan 200 d
vlan 1 mgmt
port 3 name uplink "a"
port 4 100m half
port 5 off
mtu 2 9000
pvid 3 100
isolate 3 1 2 0
isolate 4 off
ingress 1t 2u
eee off 6
lag 1 lacp 7 8
lag 2 lacp 8
laghash 1 sip dip
stp on
stp prio 8
stp port 3 cost 20000
stp lag 1 edge on
storm 2 bcast 1000 pps
bw in 3 1f4
sfp 1 10g
mirror 1 2r 3t 4
ntp timezone -3:30
passwd hunter2
mac 02:11:22:33:44:55
a typo here
\0trailing bytes after the NUL are not config
"""
WANT = """mgmt.mode static
mgmt.ip 192.168.10.247
mgmt.gateway 192.168.10.1
mgmt.netmask 255.255.255.0
sys.hostname bench-4+2
vlan.100.name lab
vlan.100.members 1,2,9
vlan.100.tagged 2,9
del vlan.200
mgmt.vlan 1
port.3.name uplink .a.
port.4.enabled on
port.4.speed 100m
port.4.duplex half
port.5.enabled off
port.2.mtu 9000
port.3.pvid 100
port.3.isolate 1,2,cpu
port.4.isolate 1,2,3,4,5,6,7,8,9,cpu
port.1.ingress tagged
port.2.ingress untagged
port.6.eee off
lag.1.mode lacp
lag.2.mode lacp
lag.2.members 8
lag.1.members 7
lag.1.hash dip,sip
stp.enabled on
stp.priority 8
stp.port.3.cost 20000
stp.lag.1.edge on
port.2.storm.bcast.rate 1000
port.2.storm.bcast.unit pps
port.3.bw_in_kbps 496
port.3.bw_in_mode fc
port.9.speed 10g
mirror.enabled on
mirror.port 1
mirror.rx 2,4
mirror.tx 3,4
ntp.timezone -03:30
web.password hunter2
sys.mac 02:11:22:33:44:55""".split("\n")


def worst_case():
    """Every config field of every instance off its default, a Str at its
    full length, every keyed slot taken (the 8x: all 9 ports): the records
    the codec (held to test_om's bytes above) makes are om_gen's saved_worst(), the
    bound the build fails past, less the fields `when` leaves off a port and
    the objects with no config field (om_gen counts their header)."""
    sch = rc.Schema()
    cfg, off = rc.Config(sch), 0
    for o in sch.objs:
        if o.levels and o.levels[0][0] == "KEY":
            n, t = o.levels[0][1], o.levels[0][2]
            for k in range(t.hi, t.lo - 1, -1):        # the slots the table has, the default entries' too
                if len(cfg.keys[o.regname]) == n:
                    break
                cfg._entry(o, (k,))
        for ix in [ix for (name, ix) in cfg.v if name == o.regname]:
            for f in o.cfields:
                d = sch.default(o, f)
                cfg.v[(o.regname, ix)][f.name] = (b"a" * f.typ.arg + b"\0" if f.typ.code in ("STR", "SECRET")
                                                  else bytes([d[0] ^ 1]) + d[1:])
                if not sch.board.when(f.when, sch.lport(o, ix)):
                    off += 2 + (f.typ.arg if f.typ.code in ("STR", "SECRET") else f.typ.size)
    got = rc.HDR + len(cfg.records())     # the bytes as saved (some flipped values aren't valid text)
    off += 4 * sum(1 for o in sch.objs if not o.cfields)    # an object of actions only: no record
    worst = rc.om_gen.saved_worst(sch.objs)
    check(got + off == worst and worst <= rc.CONFIG_LEN,
          "%d B, and the %d B `when` and the action-only objects leave off: om_gen's %d, of the sector's %d"
          % (got, off, worst, rc.CONFIG_LEN), " ".join(o.regname for o in sch.objs))


def p41_schema(board=None):
    """The objects P4-1 left on the model, whatever has moved since: a
    schema of their files alone, so the legacy split below stays put."""
    import shutil
    import tempfile
    d = tempfile.mkdtemp()
    objs = ("port", "sys", "mgmt", "web", "ntp", "dns", "syslog")
    for f in objs:
        shutil.copy(os.path.join(HERE, "..", "schema", f + ".py"), d)
    # their registry lines only: an object moved since (vlan, P4-2) has IDs but no file here
    ids, reg = set(), []
    for ln in open(os.path.join(HERE, "..", "schema", "registry.txt")):
        w = ln.split()
        if len(w) >= 3 and w[0] == "obj":
            if w[2].split(".")[0] not in objs:
                continue
            ids.add(w[1])
        elif len(w) >= 3 and w[0] == "field" and w[1].split(".")[0] not in ids:
            continue
        reg.append(ln)
    open(os.path.join(d, "registry.txt"), "w").write("".join(reg))
    sch = rc.Schema([d], board)
    shutil.rmtree(d)
    return sch


VLAN_PY = """const("MAX_VLANS", 64)
obj("vlan", keyed=U16(1, 4094), max="MAX_VLANS", apply="om_apply_vlan", check="om_check_vlan", order=30, tip="A VLAN")
cfg("name", Str(15), "", tip="A name")
cfg("members", PortMask(), 0, tip="Its ports")
cfg("tagged", PortMask(), 0, tip="The tagged members")
default_entry(1)
"""


def vlan_schema_dir():
    """The tree's schema and a vlan.V as P4-2's (its IDs, 64 entries,
    VLAN 1 by default), until P4-2 brings the real one: a directory."""
    import shutil
    import tempfile
    d = tempfile.mkdtemp()
    for f in os.listdir(os.path.join(HERE, "..", "schema")):
        shutil.copy(os.path.join(HERE, "..", "schema", f), d)
    if not os.path.exists(os.path.join(d, "vlan.py")):
        open(os.path.join(d, "vlan.py"), "w").write(VLAN_PY)
        open(os.path.join(d, "registry.txt"), "a").write(
            "obj 9 vlan\nfield 9.1 vlan.name Str(15)\nfield 9.2 vlan.members PMASK\nfield 9.3 vlan.tagged PMASK\n")
    return d


def convert():
    import legacy_map
    print("convert: old command text to paths")
    lines, _, bad = legacy_map.convert(OLD.encode(), rc.Board())
    check(lines == WANT, "every old form a config holds", "\n".join(lines))
    check(bad == ["a typo here"], "a line it can't translate is reported", bad)
    dump = "".join("%06x  %s  |\n" % (a, " ".join("%02x" % c for c in b"ip 10.0.0.9\nhostname x\n\0\xff"[a:a + 16]))
                   for a in (0, 16))
    lines, _, bad = legacy_map.convert(("flash r 7f000 20\n" + dump).encode(), rc.Board())
    check(lines == ["mgmt.mode static", "mgmt.ip 10.0.0.9", "sys.hostname x"] and not bad,
          "a console flash dump of the old sector", lines)
    warn = []
    lines, _, bad = legacy_map.convert(b"session 30\npasswd a b\npasswd a\"b\npasswd a\\b\n", rc.Board(), warn)
    check(lines == ["web.session_timeout_s 60"] and len(warn) == 1 and "raised to 60" in warn[0],
          "a session under 60 is raised to 60, with a warning", (lines, warn))
    check(bad == ["passwd a b", 'passwd a"b', "passwd a\\b"], "a passwd with a space or quote isn't translated", bad)
    warn = []
    lines, _, _ = legacy_map.convert(b"mirror 1 1 2r 3t\n", rc.Board(), warn)
    check(lines == ["mirror.enabled on", "mirror.port 1", "mirror.rx 2", "mirror.tx 3"] and
          warn == ["mirror 1 1 2r 3t: the mirror port 1 left out of its own sources"],
          "the mirror port isn't one of its sources, with a warning", (lines, warn))
    print("pvid on a LAG member: spread over the LAG, as the old port_pvid_set did")
    for text, want, w in ((b"lag 1 2 4\npvid 2 7\n", ["port.2.pvid 7", "port.4.pvid 7"], 0),
                          (b"lag 1 lacp 2 4\npvid 4 7\n", ["port.2.pvid 7", "port.4.pvid 7"], 0),
                          (b"lag 1 2 4\nlag 1 d\npvid 2 7\n", ["port.2.pvid 7"], 0),
                          (b"pvid 2 7\nlag 1 2 4\n", ["port.2.pvid 7"], 1),
                          (b"lag 1 2 4\npvid 2 7\nlag 1 2 4 6\n", ["port.2.pvid 7", "port.4.pvid 7"], 1)):
        warn = []
        lines, _, _ = legacy_map.convert(text, rc.Board(), warn)
        got = [ln for ln in lines if ".pvid " in ln]
        check(got == want and len(warn) == w and all("lag.1: its members' pvids differ" in x for x in warn),
              "%r: %s%s" % (text.decode(), " ".join(want), ", warned" if w else ""), (got, warn))
    print("VLANs the new firmware refuses: more than 64, a pvid on a missing VLAN")
    text = b"vlan 10 1 2\npvid 3 99\npvid 1 10\npvid 2 0\n" + b"".join(b"vlan %d 1\n" % (100 + i) for i in range(64))
    vs = rc.Schema([vlan_schema_dir()])
    for sch in (None, vs):
        issues, warn = [], []
        lines, _, _ = legacy_map.convert(text, rc.Board(), warn, sch, issues)
        check(issues == ["vlan 162: over the 64 VLANs the new firmware holds",
                         "vlan 163: over the 64 VLANs the new firmware holds",
                         "port 3: pvid 99, a VLAN the config doesn't have"] and "port.3.pvid 99" in lines,
              "listed (VLAN 1 by default, 10 and 100-163: 66), %s" % ("the rows" if sch is None else "vlan.V's schema"),
              issues)
        issues, warn = [], []
        lines, _, _ = legacy_map.convert(text, rc.Board(), warn, sch, issues, fit=True)
        check(not issues and "port.3.pvid 0" in lines and "port.1.pvid 10" in lines
              and not [x for x in lines if x.startswith(("vlan.162.", "vlan.163."))] and "vlan.161.members 1" in lines
              and warn[-3:] == ["vlan 162: dropped, the new firmware holds 64 VLANs",
                                "vlan 163: dropped, the new firmware holds 64 VLANs",
                                "port 3: pvid 99 parked on 0, VLAN 99 isn't in the config"],
              "fit: the highest VIDs dropped, the pvid parked on 0, each warned", (warn, [x for x in lines if "pvid" in x]))
    issues = []
    legacy_map.convert(b"vlan 1 d\npvid 2 1\n", rc.Board(), None, vs, issues)
    check(issues == ["port 2: pvid 1, a VLAN the config doesn't have"], "VLAN 1 deleted: its pvid is one", issues)
    issues = []
    legacy_map.convert(text, rc.Board(), None, p41_schema(), issues)
    check(not issues, "no vlan.V in the schema (before P4-2): no check, the VLAN lines are legacy", issues)
    print("convert against the schema: path lines, legacy, refused")
    sch = p41_schema()
    warn = []
    lines, legacy, bad = legacy_map.convert(OLD.encode(), sch.board, warn, sch)
    mine = [ln for ln in WANT if not ln.split()[1 if ln.startswith("del ") else 0].startswith(
        ("vlan.", "lag.", "stp.", "mirror.", "sys.mac"))]
    check(lines == mine, "the schema's paths are path lines (%d)" % len(lines), "\n".join(lines))
    check(rc.txt2bin(sch, lines)[:4] == b"\0RC1", "and txt2bin takes them all")
    want = [("vlan 100 lab 1 2t 9t", "P4-2"), ("vlan 200 3 4", "P4-2"), ("vlan 200 d", "P4-2"),
            ("lag 1 lacp 7 8", "P4-3"), ("lag 2 lacp 8", "P4-3"), ("laghash 1 sip dip", "P4-3"),
            ("stp on", "P4-4"), ("stp prio 8", "P4-4"), ("stp port 3 cost 20000", "P4-4"),
            ("stp lag 1 edge on", "P4-4"), ("mirror 1 2r 3t 4", "P4-5"), ("mac 02:11:22:33:44:55", "sys.mac (deferred in P4-1)")]
    check(legacy == want, "the rest is legacy, in order, each with the node that migrates it", legacy)
    check(bad == ["a typo here"] and warn == ["bw in 3 1f4: 500 kbit/s rounded down to 496, a multiple of 16"],
          "the typo is still reported; bw 1f4 rounded down, warned", (bad, warn))
    warn = []
    lines, legacy, bad = legacy_map.convert(b"mtu 2 10\nport 12 off\npvid 3 5000\nport 9 on\nport 9 name up\n"
                                            b"port 9 auto\nsfp 1 10g\nhostname ok\n", sch.board, warn, sch)
    check(lines == ["port.9.name up", "port.9.speed 10g", "sys.hostname ok"], "values the schema refuses aren't taken", lines)
    check(bad == ["mtu 2 10", "port 12 off", "pvid 3 5000"] and
          warn == ["mtu 2 10: port.2.mtu: 64-16383", "port 12 off: port.12.enabled: no such port",
                   "pvid 3 5000: port.3.pvid: 0-4094"], "they are reported, with set's reason", (bad, warn))
    check(legacy == [("port 9 on", "P4-6"), ("port 9 auto", "P4-6")],
          "an SFP port's copper fields are legacy; its speed isn't", legacy)
    lines, legacy, _ = legacy_map.convert(b"port 9 auto\nsfp 1 10g\nport 4 off\n", sch.board, None, sch)
    check(lines == ["port.9.speed 10g", "port.4.enabled off"] and len(legacy) == 1,
          "a legacy line's paths never reach the output",
          lines)
    warn = []
    lines, legacy, bad = legacy_map.convert(b"eee on 1g\neee on 3 100m\neee on 4\neee off 5 2g5\n", sch.board, warn, sch)
    check(lines[:2] == ["port.1.eee on", "port.2.eee on"] and "port.1.eee_speeds 100m,1g" in lines and
          "port.3.eee_speeds 100m" in lines and "port.4.eee_speeds 100m,1g,2g5,5g,10g" in lines and
          "port.5.eee off" in lines and "port.5.eee_speeds 100m,1g" in lines and
          not any(ln.startswith("port.9.eee") for ln in lines) and not legacy and not bad and not warn,
          "eee on [N] [speed]: eee_speeds every speed up to it, none every speed; off leaves them", lines)
    b = rc.Board("SWGT024_V2_0_UNMANAGED")
    lines, legacy, bad = legacy_map.convert(b"port 1 on\neee on\nstp on\n", b, None, p41_schema(b))
    check(lines[:3] == ["port.1.enabled on", "port.1.speed auto", "port.1.duplex auto"] and
          all(not ln.startswith("port.%d.eee" % n) for ln in lines for n in b.fronts() if b.sfp[b.log_of(n)])
          and legacy == [("stp on", "P4-4")] and not bad, "the SWGT024's ports", (lines, legacy, bad))
    # the corpus: every saved config the bench kept (outside the tree)
    corpus = sorted(glob.glob(os.path.expanduser("~/rtlpg-bench/cfg-*.txt")) +
                    glob.glob(os.path.expanduser("~/rtlpg-bench/spare2-config*.txt")))
    for f in corpus:
        lines, legacy, bad = legacy_map.convert(open(f, "rb").read(), sch.board, None, sch)
        check(lines and not bad and not legacy, "corpus %s: %d lines, all path lines" % (
            os.path.basename(f), len(lines)), (bad, legacy))
    if not corpus:
        print("  (no corpus in ~/rtlpg-bench: skipped)")


if __name__ == "__main__":
    sys.exit(main())
