#!/usr/bin/env python3
"""
lldp_overrun.py - LLDP's frames at their longest, in rtlsim, on one normal
firmware image, through ovs-rtlpg's old-vs-new bench (tests/oldnew).

  tools/lldp_overrun.py BOOT_IMAGE [--ovs DIR] [--board FILE]

Since D3 LLDP builds its frames in ctl_buf (net.md), not uip_buf. With the
longest hostname (23), a 31-character port name and named VLANs, lldp.enabled
on: every LLDP frame the switch sends parses to its End TLV and carries the
hostname, the port name and the version; and the VLAN names, the hostname
and the port name read the same after the sends as before (ctl_buf is
followed by ctl_len, tx_buf, tx_len; the names are in the object model's
config copies since P4-2, read with get).
Exit status 1 if any fails (expected at f142690: ctl_buf was 164 B).
"""
import argparse, os, re, struct, sys

HOSTNAME = "lldp-overrun-hostname-x"	# 23: hostname[24]
PORTNAME = "lldp-overrun-port-name-31-chars"	# 31: PORT_NAME_SIZE 32


class Tap:
    """Keeps the LLDP frames the switch sends."""

    def __init__(self):
        self.frames = []

    def take(self, port, f):
        if len(f) >= 14 and struct.unpack("!H", f[12:14])[0] == 0x88cc:
            self.frames.append((port, f))

    def tick(self, now):
        pass


def tlvs(f):
    """The LLDPDU's TLVs as (type, value), and whether it ended with End."""
    p, out = f[14:], []
    while len(p) >= 2:
        h = struct.unpack("!H", p[:2])[0]
        t, n = h >> 9, h & 0x1ff
        if t == 0:
            return out, True
        if len(p) < 2 + n:
            break
        out.append((t, p[2:2 + n]))
        p = p[2 + n:]
    return out, False


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
    r = Run(a.image, board, os.path.join(a.ovs, "build", "rtlsim"), None, om=("state.mgmt", "vlan", "lldp"))	# its address: get state.mgmt.ip
    b = r.b
    try:
        r.boot()
        r._now()
        F = r.F

        def cli(line, ms=5000):
            before, out = b.console(line, b.now + ms * 1000)
            return out or ""

        for line in (f"set sys.hostname {HOSTNAME}", "set vlan.10.name tenname", f"set vlan.10.members {F[1]},{F[2]}",
                     f"set vlan.10.tagged {F[2]}", "set vlan.20.name twentyname", f"set vlan.20.members {F[1]},{F[2]}",
                     f"set vlan.20.tagged {F[1]}", f"set port.{F[0]}.name {PORTNAME}", "apply"):
            cli(line)

        state = lambda: [cli(c) for c in ("get vlan.*.name", "get sys.hostname", f"get port.{F[0]}.name")]
        before = state()
        tap = Tap()
        b.devices.append(tap)
        cli("set lldp.enabled on")
        cli("apply")
        b.advance(b.now + 70 * 1000000)
        after = state()
        version = re.search(r"state\.sys\.version (\S+)", cli("get state.sys.version"))
        version = version.group(1) if version else "?"

        bad = []
        for port, f in tap.frames:
            ts, ended = tlvs(f)
            d = dict(ts)
            why = []
            if not ended:
                why.append("no End TLV")
            if d.get(5) != HOSTNAME.encode():
                why.append(f"sysname {d.get(5)!r}")
            if not d.get(6, b"").endswith(b" " + version.encode()):
                why.append(f"sysdesc {d.get(6)!r}")
            if d.get(4, PORTNAME.encode()) not in (PORTNAME.encode(),):
                why.append(f"portdesc {d.get(4)!r}")
            if why:
                bad.append((port, len(f), why))
        longest = max((len(f) for _, f in tap.frames), default=0)
        with_name = sum(1 for _, f in tap.frames if dict(tlvs(f)[0]).get(4) == PORTNAME.encode())
        ok_frames = tap.frames and not bad and with_name
        print(f"L1 LLDP frames: {len(tap.frames)} sent, longest {longest} B on the wire, "
              f"{with_name} with the port name: {'PASS' if ok_frames else 'FAIL'}")
        for x in bad[:5]:
            print(f"   port {x[0]}, {x[1]} B: {', '.join(x[2])}")
        if not ok_frames:
            fails.append("L1")

        ok_state = before == after and "tenname" in before[0] and "twentyname" in before[0]
        print(f"L2 VLAN names, hostname, port name after the sends: {'PASS' if ok_state else 'FAIL'}")
        for x, y in zip(before, after):
            if x != y:
                print("   before: " + x.replace("\n", "\n           ").rstrip())
                print("   after:  " + y.replace("\n", "\n           ").rstrip())
        if not ok_state:
            fails.append("L2")
    finally:
        b.close()
    print("lldp_overrun: " + ("FAIL " + " ".join(fails) if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
