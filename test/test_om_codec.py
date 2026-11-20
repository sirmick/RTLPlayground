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
    for data, why in ((b"\xff" * 8, "blank"), (b"ip 1.2.3.4\n", "old command text"),
                      (b8[:6] + b"\1\0", "bad")):
        try:
            rc.bin2txt(rc.Schema(), data)
            check(False, "a sector it refuses: " + why)
        except rc.Bad as e:
            check(why in str(e), "a sector it refuses: %s" % e)

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


def convert():
    import legacy_map
    print("convert: old command text to paths")
    lines, bad = legacy_map.convert(OLD.encode(), rc.Board())
    check(lines == WANT, "every old form a config holds", "\n".join(lines))
    check(bad == ["a typo here"], "a line it can't translate is reported", bad)
    dump = "".join("%06x  %s  |\n" % (a, " ".join("%02x" % c for c in b"ip 10.0.0.9\nhostname x\n\0\xff"[a:a + 16]))
                   for a in (0, 16))
    lines, bad = legacy_map.convert(("flash r 7f000 20\n" + dump).encode(), rc.Board())
    check(lines == ["mgmt.mode static", "mgmt.ip 10.0.0.9", "sys.hostname x"] and not bad,
          "a console flash dump of the old sector", lines)
    warn = []
    lines, bad = legacy_map.convert(b"session 30\npasswd a b\npasswd a\"b\npasswd a\\b\n", rc.Board(), warn)
    check(lines == ["web.session_timeout_s 60"] and len(warn) == 1 and "raised to 60" in warn[0],
          "a session under 60 is raised to 60, with a warning", (lines, warn))
    check(bad == ["passwd a b", 'passwd a"b', "passwd a\\b"], "a passwd with a space or quote isn't translated", bad)
    # the corpus: every saved config the bench kept (outside the tree)
    corpus = sorted(glob.glob(os.path.expanduser("~/rtlpg-bench/cfg-*.txt")) +
                    glob.glob(os.path.expanduser("~/rtlpg-bench/spare2-config*.txt")))
    for f in corpus:
        lines, bad = legacy_map.convert(open(f, "rb").read(), rc.Board())
        check(lines and not bad, "corpus %s: %d lines, all translated" % (os.path.basename(f), len(lines)), bad)
    if not corpus:
        print("  (no corpus in ~/rtlpg-bench: skipped)")


if __name__ == "__main__":
    sys.exit(main())
