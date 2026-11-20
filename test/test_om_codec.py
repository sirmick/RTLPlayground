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
            check(len(skipped) == 9, "skip: and skips what the load skipped (9)", "\n".join(skipped))
            continue
        got = rc.bin2txt(sch, b)
        lines = [ln for ln in t if " " in ln and not ln.startswith("del ")]	# not test_om's keyed notes
        check(got == lines, "%s: bin2txt is get -d" % n, "\n".join(got))
        mine = rc.txt2bin(sch, t)
        check(mine == b, "%s: txt2bin of get -d is the firmware's save (%d B)" % (n, len(b)),
              "python %s\nC      %s" % (mine.hex(), b.hex()))

    print("the real schema, other boards")
    sw = rc.Schema(board=rc.Board("SWGT024_V2_0_UNMANAGED"))
    b = rc.txt2bin(sw, ["set port.1.isolate 1,6", "port.6.mtu 9000"])
    # front 1 is logical 4, front 6 logical 3: bits 4 and 3
    check(b[8:] == bytes([1, 1, 0, 1, 11, 2, 0x18, 0]) + bytes([1, 6, 0, 1, 5, 2]) + struct.pack("<H", 9000),
          "SWGT024: front-panel numbers to logical bits, the index as typed", b[8:].hex())
    check(rc.bin2txt(sw, b) == ["port.1.isolate 1,6", "port.6.mtu 9000"], "and back")
    b8 = rc.txt2bin(rc.Schema(), [])
    check(b8 == b"\0RC1" + bytes(4), "the defaults: a header and no records")
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


def p41_schema(board=None):
    """The objects P4-1 left on the model, whatever has moved since: a
    schema of their files alone, so the legacy split below stays put."""
    import shutil
    import tempfile
    d = tempfile.mkdtemp()
    for f in ("port", "sys", "mgmt", "web", "ntp", "dns", "syslog"):
        shutil.copy(os.path.join(HERE, "..", "schema", f + ".py"), d)
    shutil.copy(os.path.join(HERE, "..", "schema", "registry.txt"), d)
    sch = rc.Schema([d], board)
    shutil.rmtree(d)
    return sch


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
    print("convert against the schema: path lines, legacy, refused")
    sch = p41_schema()
    warn = []
    lines, legacy, bad = legacy_map.convert(OLD.encode(), sch.board, warn, sch)
    mine = [ln for ln in WANT if not ln.split()[1 if ln.startswith("del ") else 0].startswith(
        ("vlan.", "lag.", "stp.", "mirror.", "sys.mac", "port.2.storm", "port.3.bw_", "port.9."))]
    check(lines == mine, "the schema's paths are path lines (%d)" % len(lines), "\n".join(lines))
    check(rc.txt2bin(sch, lines)[:4] == b"\0RC1", "and txt2bin takes them all")
    want = [("vlan 100 lab 1 2t 9t", "P4-2"), ("vlan 200 3 4", "P4-2"), ("vlan 200 d", "P4-2"),
            ("lag 1 lacp 7 8", "P4-3"), ("lag 2 lacp 8", "P4-3"), ("laghash 1 sip dip", "P4-3"),
            ("stp on", "P4-4"), ("stp prio 8", "P4-4"), ("stp port 3 cost 20000", "P4-4"),
            ("stp lag 1 edge on", "P4-4"), ("storm 2 bcast 1000 pps", "P4-6"), ("bw in 3 1f4", "P4-6"),
            ("sfp 1 10g", "P4-6"), ("mirror 1 2r 3t 4", "P4-5"), ("mac 02:11:22:33:44:55", "sys.mac (deferred in P4-1)")]
    check(legacy == want, "the rest is legacy, in order, each with the node that migrates it", legacy)
    check(bad == ["a typo here"] and not warn, "the typo is still reported", (bad, warn))
    warn = []
    lines, legacy, bad = legacy_map.convert(b"mtu 2 10\nport 12 off\npvid 3 5000\nport 9 on\nport 9 name up\n"
                                            b"port 9 auto\nsfp 1 10g\nhostname ok\n", sch.board, warn, sch)
    check(lines == ["port.9.name up", "sys.hostname ok"], "values the schema refuses aren't taken", lines)
    check(bad == ["mtu 2 10", "port 12 off", "pvid 3 5000"] and
          warn == ["mtu 2 10: port.2.mtu: 64-16383", "port 12 off: port.12.enabled: no such port",
                   "pvid 3 5000: port.3.pvid: 0-4094"], "they are reported, with set's reason", (bad, warn))
    check(legacy == [("port 9 on", "P4-6"), ("port 9 auto", "P4-6"), ("sfp 1 10g", "P4-6")],
          "an SFP port's copper fields are P4-6's legacy", legacy)
    lines, legacy, _ = legacy_map.convert(b"port 9 auto\nsfp 1 10g\nport 4 off\n", sch.board, None, sch)
    check(lines == ["port.4.enabled off"] and len(legacy) == 2, "a legacy line's paths never reach the output",
          lines)
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
