#!/usr/bin/env python3
"""
webcmd_abort.py - the web command buffer (config_buf) in rtlsim, on one
normal firmware image, through ovs-rtlpg's old-vs-new bench (tests/oldnew).

  tools/webcmd_abort.py BOOT_IMAGE [--ovs DIR] [--board FILE]

Since D3 a /cmd waits in config_buf for the cli task, and its reply goes out
on a later poll (net.md item 2). uIP has one connection, so no other request
can reach config_buf meanwhile, unless the /cmd's own connection ends first.

W1: a config upload (POST /config, staged in config_buf), a /cmd, another
    upload, another /cmd: each /cmd's reply is its commands' output and each
    upload is in flash.
W2: a /cmd ("set sys.hostname aborted", apply) and the client's RST in the same instant,
    then a POST /config (its body "set sys.hostname cfgbody ...") on a new
    connection: the cancelled command runs nothing, the upload's body isn't
    run as commands (hostname unchanged, the console silent), and the upload
    is in flash. The order is forced by the sim: the /cmd segment and the RST
    are in the NIC ring together, and the rx task takes up to RX_BUDGET (4)
    frames a step, so the RST is handled before cli's next step.
W3: a /cmd ("set sys.hostname tail3", apply, "get mgmt.ip") followed, in a segment of its own in the
    same instant, by a CRLF; W4: the same with a second request pipelined
    behind it (GET /favicon.ico): the posted command runs exactly (hostname
    set, the reply its output, no parse error) and the extra data is
    dropped, as today's code drops data while the reply is sent.
W5: a web /cmd "get state.l2\nhostname oldbody" over 300 learned MACs, its connection
    reset mid-dump, and at once a new /cmd "set sys.hostname w5", apply, "get mgmt.ip": the new one
    gets its own reply (mgmt.ip's, no state.l2 lines), the hostname is w5, and the old
    body's second line never runs (E1: the run checks its command's
    generation at every step).
W6: STP off, "stp bpdu filter" (the BPDU entries CPU-only), VLAN 4000
    configured; a web /cmd "stp bpdu flood" (stp_off()'s VLAN walk, VLAN
    4000's entry near its end), its connection reset at the walk's first
    step: the walk still runs to its end, so the L2 table equals the one a
    whole console "stp bpdu flood" leaves (E1: a job with side effects isn't
    dropped). W5 and W6 stop the sim on cli's functions: the image's
    rtlplayground.map, next to it.
Exit status 1 if any fails.
"""
import argparse, os, re, struct, sys

CMD = "get mgmt.ip\nget state.sys.version"
CONFIG_START = 0x7f000


def cmd_ok(resp):
    body = resp.partition(b"\r\n\r\n")[2]
    return resp.startswith(b"HTTP/1.1 200") and b"mgmt.ip " in body and b"state.sys.version " in body


class Conn:
    """One client TCP connection from the bench's Host; several at once.
    acking False holds our ACKs back (the switch's data stays unacknowledged)."""

    def __init__(self, host, sport):
        self.h, self.sport = host, sport
        self.iss = 1000 + (sport - 40000) * 100000
        self.snd, self.rcv, self.una = self.iss + 1, None, self.iss
        self.resp, self.fin, self.rst, self.acking = b"", False, False, True
        self.unacked = False

    def tcp(self, flags, data=b"", mss=False):
        self.h._tcp(self.sport, self.iss if flags & 0x02 else self.snd, self.rcv or 0, flags, data, mss)

    def take(self, s):
        seq, ack, off, fl = struct.unpack("!IIBB", s[4:14])
        data = s[(off >> 4) * 4:]
        if fl & 0x04:
            self.rst = True
        if fl & 0x10:
            self.una = max(self.una, ack)
        if fl & 0x02 and self.rcv is None:
            self.rcv = seq + 1
            return
        if self.rcv is None:
            return
        if seq == self.rcv and data:
            self.resp += data
            self.rcv += len(data)
        if seq + len(data) == self.rcv and fl & 0x01 and not self.fin:
            self.fin = True
            self.rcv += 1
        if data or fl & 0x01:
            self.unacked = True
        self.ack()

    def ack(self):
        if self.unacked and self.acking:
            self.unacked = False
            self.tcp(0x10)


class Client:
    def __init__(self, run):
        self.r, self.b, self.h = run, run.b, run.host
        self.conns = {}

    def pump(self):
        for s in self.h.rx:
            c = self.conns.get(struct.unpack("!H", s[2:4])[0])
            if c:
                c.take(s)
        self.h.rx = []

    def run(self, us, stop=None):
        def st():
            self.pump()
            return stop() if stop else False
        return self.b.advance(self.b.now + us, st)

    def open(self):
        if not self.h.arp(self.b.now + 1000000):
            raise RuntimeError("no ARP reply")
        sport, self.h.next_port = self.h.next_port, self.h.next_port + 1
        c = self.conns[sport] = Conn(self.h, sport)
        c.tcp(0x02, mss=True)
        self.run(1000000, lambda: c.rcv is not None)
        c.tcp(0x10)
        return c

    def send(self, c, req):
        """The request in one segment (all of ours fit one)."""
        c.tcp(0x18, req)
        c.snd += len(req)

    def done(self, *cs):
        return all(c.fin or c.rst for c in cs)

    def close(self, *cs):
        for c in cs:
            if c.fin:
                c.tcp(0x11)
                c.snd += 1
        self.run(200000)


def until_step(r, fn):
    """Run to the next call of fn, and past that step (its end: the next
    return to the scheduler), keeping the bench's clock."""
    r.b.sim.run_until_pc(fn, max_ms=5000)
    r.b.sim.run_until_pc("_sched_pass", max_ms=100)
    r.b.now = r.b.sim.time()[1]


def post(cookie, path, ctype, body):
    return (f"POST {path} HTTP/1.1\r\nCookie: session=".encode() + cookie +
            f"\r\nContent-Type: {ctype}\r\nContent-Length: {len(body)}\r\n\r\n".encode() + body)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--ovs", default=os.path.expanduser("~/src/ovs-rtlpg"))
    ap.add_argument("--board", default=None)
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args(argv)
    board = a.board or os.path.join(a.ovs, "boards", "keeplink_kp_9000_9xhpml_v1_2.json")
    sys.path.insert(0, os.path.join(a.ovs, "tests", "oldnew"))
    sys.path.insert(0, os.path.join(a.ovs, "tests"))
    from oldnew import Run
    from bench import Bench

    fails = []
    r = Run(a.image, board, os.path.join(a.ovs, "build", "rtlsim"), None, om=("state.mgmt",))	# its address: get state.mgmt.ip
    # the image's map (next to it), for W5 and W6's run-until on cli's steps
    mp = os.path.join(os.path.dirname(os.path.abspath(a.image)), "rtlplayground.map")
    r.b.close()
    r.b = Bench(a.image, board=board, rtlsim=os.path.join(a.ovs, "build", "rtlsim"), args=["--map", mp])
    r.b.link(r.P[0], True)
    try:
        r.boot()
        r.login()
        r._now()
        k = Client(r)
        # W6's VLAN, while the console is quiet (a line typed while a link
        # change prints is lost)
        k.b.console(f"vlan 4000 {r.F[1]} {r.F[2]}t", k.b.now + 3000000)

        def upload(text):
            bnd = "----wcmd0123456789"
            body = (f"--{bnd}\r\nContent-Disposition: form-data; name=\"configuration\"; filename=\"config.txt\"\r\n"
                    f"Content-Type: application/octet-stream\r\n\r\n").encode() + text.encode() + f"\r\n--{bnd}--\r\n".encode()
            c = k.open()
            k.send(c, post(r.cookie, "/config", f"multipart/form-data; boundary={bnd}", body))
            k.run(5000000, lambda: k.done(c))
            k.close(c)
            saved = r.b.flash(CONFIG_START, 512).split(b"\0")[0].split(b"\xff")[0].decode("latin-1")
            return c.resp.startswith(b"HTTP/1.1 200") and saved.startswith(text.rstrip("\n")), saved

        def webcmd(text):
            c = k.open()
            k.send(c, post(r.cookie, "/cmd", "text/plain;charset=UTF-8", text.encode()))
            k.run(3000000, lambda: k.done(c))
            k.close(c)
            return c.resp

        def hostname():
            k.b.pending()
            before, out = k.b.console("get sys.hostname", k.b.now + 3000000)	# sys.hostname: P4-1
            m = re.search(r"sys\.hostname (\S*)", out or "")
            return m.group(1) if m else ""

        F = r.F
        cfg = lambda name: f"set sys.hostname {name}\napply\nvlan 10 {F[1]} {F[2]}t\npvid {F[1]} 10\n"

        # W1
        res = []
        for i in (1, 2):
            ok_up, saved = upload(cfg(f"up{i}"))
            resp = webcmd(CMD)
            res.append((ok_up, cmd_ok(resp)))
            print(f"   upload {i}: {'PASS' if ok_up else 'FAIL'} (flash {saved[:40]!r}); "
                  f"/cmd: {'PASS' if cmd_ok(resp) else 'FAIL'} {resp.partition(b'\r\n\r\n')[2][:60]!r}")
        ok = all(a and b for a, b in res)
        print(f"W1 /cmd with a config upload before and after: {'PASS' if ok else 'FAIL'}")
        if not ok:
            fails.append("W1")

        # W2
        name0 = hostname()
        k.b.pending()
        c = k.open()
        k.send(c, post(r.cookie, "/cmd", "text/plain;charset=UTF-8", b"set sys.hostname aborted\napply"))
        c.tcp(0x04)				# RST, in the same instant
        k.run(1000000)
        ok_up, saved = upload(cfg("cfgbody"))
        k.run(1000000)
        con = k.b.pending()
        name1 = hostname()
        ok_run = name1 == name0
        ok_quiet = "Unknown command" not in con and "Error" not in con and "aborted" not in con
        print(f"W2 /cmd aborted before cli runs, then POST /config: hostname {name0!r} -> {name1!r} "
              f"{'PASS' if ok_run else 'FAIL'}, console {'PASS' if ok_quiet else 'FAIL'}, "
              f"upload {'PASS' if ok_up else 'FAIL'}")
        if con.strip():
            print("   console: " + con.replace("\n", "\n            ").rstrip())
        if not (ok_run and ok_quiet and ok_up):
            fails.append("W2")

        # W3, W4
        for name, extra in (("W3", b"\r\n"),
                            ("W4", b"GET /favicon.ico HTTP/1.1\r\nCookie: session=" + r.cookie + b"\r\n\r\n")):
            want = f"tail{name[1]}"
            k.b.pending()
            c = k.open()
            k.send(c, post(r.cookie, "/cmd", "text/plain;charset=UTF-8", f"set sys.hostname {want}\napply\nget mgmt.ip".encode()))
            k.send(c, extra)			# in the same instant: before cli runs
            k.run(3000000, lambda: k.done(c))
            k.close(c)
            body = c.resp.partition(b"\r\n\r\n")[2]
            con = k.b.pending()
            got = hostname()
            ok = (c.resp.startswith(b"HTTP/1.1 200") and b"mgmt.ip " in body and b"rror" not in body
                  and b"Unknown" not in body and got == want and "Unknown command" not in con)
            print(f"{name} /cmd then {'a CRLF' if name == 'W3' else 'a pipelined GET'}: hostname {got!r}, "
                  f"reply {body[:50]!r}: {'PASS' if ok else 'FAIL'}")
            if not ok:
                print(f"   reply: {c.resp[:160]!r}\n   console: {con.strip()[:200]!r}")
                fails.append(name)
        # W6: stp off's VLAN walk survives its connection. "stp bpdu filter"
        # leaves the BPDU entries CPU-only (STP off: at once, a whole walk);
        # a web "stp bpdu flood" rewrites them, VLAN 4000's near the walk's end.
        k.b.console("stp bpdu filter", k.b.now + 3000000)
        k.run(1000000)
        k.b.pending()
        c = k.open()
        k.send(c, post(r.cookie, "/cmd", "text/plain;charset=UTF-8", b"stp bpdu flood"))
        until_step(r, "_stp_fdb_line")	# the walk's first step
        c.tcp(0x04)				# while the walk runs
        k.run(2000000)
        cut = r.b.table("l2")
        k.b.console("stp bpdu filter", k.b.now + 3000000)
        k.run(1000000)
        k.b.console("stp bpdu flood", k.b.now + 3000000)
        k.run(1000000)
        whole = r.b.table("l2")
        ok = cut == whole and any("fa00180 " in l for l in whole)	# VLAN 4000's entry is there
        print(f"W6 web stp bpdu flood reset mid-walk: L2 table {'the same as' if ok else 'NOT the same as'} "
              f"a whole one's: {'PASS' if ok else 'FAIL'}")
        if a.verbose:
            print("   whole's BPDU entries: " + " | ".join(l for l in whole if "c2000000" in l)[:300])
        if not ok or a.verbose:
            print("   cut:   " + " | ".join(l for l in cut if l not in whole)[:300])
            print("   whole: " + " | ".join(l for l in whole if l not in cut)[:300])
            fails.append("W6")
        # W5: a new /cmd while an old one's dump is cut off
        P = r.P
        r.link(P[1], True)
        for n in range(300):
            src = bytes([0x02, 0, 0, 0x10, n >> 8, n & 0xff])
            r.b.sim.nic_inject(P[1], b"\xff" * 6 + src + struct.pack("!H", 0x88b5) + b"\0" * 46)
            if n % 32 == 31:
                k.run(20000)
        k.run(2000000)
        k.b.pending()
        c1 = k.open()
        k.send(c1, post(r.cookie, "/cmd", "text/plain;charset=UTF-8", b"get state.l2\nset sys.hostname oldbody\napply"))
        k.run(5000000, lambda: k.b.take_uart().count("\n") > 100)	# mid-dump, the TX ring full
        until_step(r, "_cli_more")		# a line printed: cli waits ~4 ms for room for the next
        c1.tcp(0x04)
        c2 = k.open()
        k.send(c2, post(r.cookie, "/cmd", "text/plain;charset=UTF-8", b"set sys.hostname w5\napply\nget mgmt.ip"))
        k.run(8000000, lambda: k.done(c2))
        k.close(c2)
        k.run(1000000)
        body = c2.resp.partition(b"\r\n\r\n")[2]
        got = hostname()
        ok = (c2.resp.startswith(b"HTTP/1.1 200") and b"mgmt.ip " in body and b"02:00:00:10" not in body
              and got == "w5")
        print(f"W5 a new /cmd after one reset mid-get state.l2: hostname {got!r}, reply {body[:60]!r}: {'PASS' if ok else 'FAIL'}")
        if not ok:
            print(f"   reply: {c2.resp[:200]!r}")
            fails.append("W5")

    finally:
        r.b.close()
    print("webcmd_abort: " + ("FAIL " + " ".join(fails) if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
