#!/usr/bin/env python3
"""ui_sim.py - the switch's web UI from rtlsim, for a browser on localhost.

Boots a firmware image in rtlsim (ovs-rtlpg's bench.py), cables bench.py's
Host to front port 1, and serves http://127.0.0.1:PORT: each request is
replayed to the switch over the Host's TCP (one at a time, as the switch has
one connection) and its reply passed back as it came. Between requests the
simulation runs on, about as fast as the wall clock.

    ui_sim.py BUILDDIR [--port 8899] [--ovs ~/src/ovs-rtlpg] [--board boards/x.json]

BUILDDIR is a firmware build (output/<MACHINE>-compact); --ovs an
ovs-rtlpg checkout with build/rtlsim built; --board one of its boards/
(default the 8x's). The UI is the image's own (html/ as built into it), so
rebuild the firmware to see a change to html/.

Prints "ready http://127.0.0.1:PORT" once the switch has booted, and each
request on stderr (firmware time it started, the path (and a /cli's first line), firmware ms it took, reply bytes; -1: no reply in 30 s, -2: the
switch reset the connection). GET /_proxy/slow_apply?s=N&busy=M holds the
next apply's reply N s (the wall clock; the switch runs on), as a long walk,
and answers every /cli "err: applying" for M s from apply (not from the
reply), as om_cmd does while the walk runs. GET /_proxy/pin: from then on a
request reaches the switch only when the address it was for (127.0.0.1 is
the boot address; a route's X-Sim-Host header names another) is the
switch's, so a page at an old address loses it. GET /_proxy/sfp?slot=S&speed=10g[&serial=X]:
a module (ovs-rtlpg's mksfp.py) into cage S, its link up; ?slot=S&out=1 pulls it.
GET /_proxy/bpdu?n=N: an RST BPDU in on copper port N.
GET /_proxy/hosts?n=N: N hosts 02:00:00:01:xx:xx learned on the second copper port.

rtlsim's nic inject hands the Host's frames straight to the CPU, past the
fabric, so the proxy applies the chip's port isolation itself: while the
Host's port may not send to the CPU (PORT_ISOLATION bit 9 clear) a request
is lost, as a browser's would be: the switch runs on 10 s (the UI's
timeout) and no reply comes. The PHYs aren't modelled: a port switched off
keeps its link.
"""
import argparse, glob, json, os, re, subprocess, sys, tempfile, threading, time
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

def ovs_arg():
    for i, a in enumerate(sys.argv):
        if a == "--ovs" and i + 1 < len(sys.argv):
            return sys.argv[i + 1]
        if a.startswith("--ovs="):
            return a[6:]
    return "~/src/ovs-rtlpg"


OVS = os.path.expanduser(ovs_arg())
sys.path.insert(0, os.path.join(OVS, "tests"))
sys.path.insert(0, os.path.join(OVS, "tests", "oldnew"))
from bench import Bench, Host, ip_bytes  # noqa: E402

KEEP = ("cookie", "content-type", "content-length")
PROMPT = r"\n(\[confirm \d+\] |\*)?> "	# bench.py's, and the prompt in a window or with edits staged
lock = threading.Lock()
B = H = BOARD = None
SLOW = BUSY = 0			# /_proxy/slow_apply?s=N&busy=M: the next apply's reply N s late,
BUSY_UNTIL = 0			# and every /cli for M s after it answered "err: applying"
BUSY_R = b"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 14\r\n\r\nerr: applying\n"
ISO = None			# the Host's port's PORT_ISOLATION register, as reg dump names it
PIN = False			# /_proxy/pin: a request reaches the switch only at its address
ADDR = None			# the address 127.0.0.1 stands for (a route's X-Sim-Host: another)
CUR = None			# pinned: the switch's address, asked again when a request is for another


def sw_ip():
    """the switch's address from its console: state.mgmt.ip (P5-3), or
    mgmt.ip (P4-1, static), else the old bare `ip`"""
    m = None
    for p in ("state.mgmt.ip", "mgmt.ip"):
        _, r = B.console("get " + p, B.now + 3_000_000, PROMPT)
        m = m or re.search(p.replace(".", r"\.") + r" ([\d.]+)", r or "")
    if not m:
        _, r = B.console("ip", B.now + 3_000_000, PROMPT)
        m = re.search(r"Current IP: ([\d.]+)", r or "")
    return m and m.group(1)


def readdress():
    """after a lost request: the switch's address again (a mgmt.ip change,
    or a save before P4-1 that drops the IP: the boot then has the built-in
    one); true if it moved"""
    ip = sw_ip()
    if ip and ip_bytes(ip) != H.sw_ip:
        print("readdress %s" % ip, file=sys.stderr, flush=True)
        H.sw_ip, H.ip, H.sw_mac = ip_bytes(ip), ip_bytes(ip.rsplit(".", 1)[0] + ".1"), None
        return True


def at(ip):
    """pinned: the switch is at ip (as last seen); the Host follows it there"""
    if CUR != ip:
        return False
    if ip_bytes(ip) != H.sw_ip:
        H.sw_ip, H.ip, H.sw_mac = ip_bytes(ip), ip_bytes(ip.rsplit(".", 1)[0] + ".1"), None
    return True


def cage_phys(slot):
    return [p for p in BOARD["ports"] if p.get("sfp", {}).get("slot") == slot][0]["phys"]


def sfp_out(slot):
    B.link(cage_phys(slot), False)
    B.sfp(slot, None)


def sfp_in(slot, speed, serial):
    f = os.path.join(tempfile.mkdtemp(), "sfp.bin")
    subprocess.run([sys.executable, os.path.join(OVS, "tools", "mksfp.py"), "--speed", speed,
                    "--serial", serial, f],
                   check=True, stdout=subprocess.DEVNULL)
    B.sfp(slot, open(f, "rb").read())
    B.link(cage_phys(slot), True, speed)


def cut():
    """the Host's port may not send to the CPU"""
    for l in B.reg_dump():
        if l.startswith(ISO):
            return not int(l.split()[2], 16) >> 9 & 1
    return False


class Handler(BaseHTTPRequestHandler):
    def _any(self):
        global SLOW, BUSY, BUSY_UNTIL, PIN, CUR
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        if self.path == "/_proxy/pin":
            PIN, CUR = True, ADDR
            self.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n")
            self.close_connection = True
            return
        if self.path.startswith("/_proxy/sfp?"):	# a module (mksfp.py) into a cage, with light
            q = dict(kv.split("=") for kv in self.path.split("?", 1)[1].split("&"))
            with lock:
                if "out" in q:
                    sfp_out(int(q["slot"]))
                else:
                    sfp_in(int(q["slot"]), q.get("speed", "10g"), q.get("serial", "SIM0001"))
            self.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n")
            self.close_connection = True
            return
        if self.path.startswith("/_proxy/hosts?"):	# as sim_smoke's 300 hosts
            n = int(self.path.split("n=", 1)[1])
            p1 = [p for p in BOARD["ports"] if "sfp" not in p][1]["phys"]
            with lock:
                B.link(p1, True)
                B.advance(B.now + 500_000)
                for i in range(n):
                    B.advance(B.now + 2000)
                    B.sim.nic_inject(p1, bytes.fromhex("02000000ffff") + bytes([2, 0, 0, 1, i >> 8, i & 255]) +
                                     b"\x88\xb5" + bytes(46))
                B.advance(B.now + 200_000)
            self.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n")
            self.close_connection = True
            return
        if self.path.startswith("/_proxy/bpdu?"):	# an RST BPDU in on copper port n (1-based)
            n = int(self.path.split("n=", 1)[1])
            p1 = [p for p in BOARD["ports"] if "sfp" not in p][n - 1]["phys"]
            bpdu = bytes.fromhex("0000" "02" "02" "3c" "1000020000000001" "00000000" "1000020000000001"
                                 "8001" "0000" "1400" "0200" "0f00" "00")
            with lock:
                B.link(p1, True)
                B.advance(B.now + 200_000)
                B.sim.nic_inject(p1, bytes.fromhex("0180c2000000" "020000000001") + len(bpdu + b"BBB").to_bytes(2, "big")
                                 + b"\x42\x42\x03" + bpdu + bytes(8))
                B.advance(B.now + 200_000)
            self.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n")
            self.close_connection = True
            return
        if self.path.startswith("/_proxy/slow_apply?"):
            q = dict(kv.split("=") for kv in self.path.split("?", 1)[1].split("&"))
            SLOW, BUSY = float(q.get("s", 0)), float(q.get("busy", 0))
            self.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n")
            self.close_connection = True
            return
        req = "%s %s HTTP/1.1\r\n" % (self.command, self.path)
        for k in KEEP:
            v = self.headers.get(k)
            if v is not None:
                req += "%s: %s\r\n" % (k.title(), v)
        req = req.encode() + b"\r\n" + body
        if self.path == "/cli" and time.time() < BUSY_UNTIL:	# a walk the switch is still in
            print("busy: err: applying to %s" % body.split(b"\n")[0].decode(errors="replace"), file=sys.stderr, flush=True)
            self.wfile.write(BUSY_R)
            self.close_connection = True
            return
        with lock:			# an address not the cached one: ask the switch first
            lost = PIN and not at(self.headers.get("X-Sim-Host") or ADDR)
            if lost:
                CUR = sw_ip()
                lost = not at(self.headers.get("X-Sim-Host") or ADDR)
        if lost:			# an address nobody answers at: the browser waits, others go on
            print("%9.3f s %-4s %-40s  lost (pinned elsewhere: for %s, the switch at %s)" % (
                B.now / 1e6, self.command, self.path[:40], self.headers.get("X-Sim-Host") or ADDR, CUR),
                  file=sys.stderr, flush=True)
            time.sleep(12)
            self.wfile.write(b"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n")
            self.close_connection = True
            return
        with lock:
            t = B.now
            if cut():
                B.advance(B.now + 10_000_000)
                B.pending()
                r = None
            else:
                r = H.http(req, B.now + 30_000_000)
                if r is None and PIN:
                    CUR = sw_ip()
                elif r is None and readdress():	# the browser asked the new address
                    r = H.http(req, B.now + 30_000_000)
            what = self.path + (" " + body.split(b"\n")[0].decode(errors="replace") if self.path == "/cli" else "")
            print("%9.3f s %-4s %-40s %7.1f ms %6d B" % (t / 1e6, self.command, what[:40], (B.now - t) / 1000,
                                                len(r) if isinstance(r, bytes) else -2 if r == "reset" else -1),
                  file=sys.stderr, flush=True)
        if not isinstance(r, bytes):
            r = b"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n"
        if SLOW and body.startswith(b"apply"):	# the switch walks on meanwhile
            t, SLOW = SLOW, 0
            BUSY_UNTIL, BUSY = time.time() + BUSY, 0
            print("slow apply: its reply %g s late" % t, file=sys.stderr, flush=True)
            time.sleep(t)
        try:
            self.wfile.write(r)
        except OSError:
            pass				# the browser gave up
        self.close_connection = True

    do_GET = do_POST = _any

    def log_message(self, *a):
        pass


def run_on():
    """the switch's clock with the wall clock, as far as rtlsim keeps up:
    what a request ran is not run again"""
    w0 = time.time()
    with lock:
        s0 = B.now
    while True:
        time.sleep(0.05)
        with lock:
            due = s0 + int((time.time() - w0) * 1e6)
            if due > B.now:
                B.advance(min(due, B.now + 500_000))
            else:		# a request ran ahead (a lost one's timeout): from here
                w0, s0 = time.time(), B.now
            B.pending()


def main():
    global B, H, ISO, ADDR, BOARD
    ap = argparse.ArgumentParser()
    ap.add_argument("builddir")
    ap.add_argument("--port", type=int, default=8899)
    ap.add_argument("--ovs", default="~/src/ovs-rtlpg", help="an ovs-rtlpg checkout (read before argparse)")
    ap.add_argument("--board", default=os.path.join(OVS, "boards", "keeplink_kp_9000_9xhpml_v1_2.json"))
    a = ap.parse_args()
    img = max(glob.glob(os.path.join(a.builddir, "rtlplayground-boot-*.bin")), key=os.path.getmtime)
    B = Bench(img, board=a.board)
    BOARD = json.load(open(a.board))
    p0 = [p for p in json.load(open(a.board))["ports"] if "sfp" not in p][0]["phys"]
    B.link(p0, True)
    ISO = "r %04x " % (0x50c0 + 4 * p0)
    B.wait_uart(r"\n> ", 30_000_000)
    B.advance(B.now + 10000)
    sw = ADDR = sw_ip()
    H = Host(B, p0, ip=sw.rsplit(".", 1)[0] + ".1", switch_ip=sw)
    threading.Thread(target=run_on, daemon=True).start()
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), Handler)
    print("ready http://127.0.0.1:%d" % a.port, flush=True)
    try:
        srv.serve_forever()
    finally:
        B.close()


if __name__ == "__main__":
    main()
