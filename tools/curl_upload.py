#!/usr/bin/env python3
"""
curl_upload.py - uploads from Linux's curl to a simulated switch (E3, open
question O3: does a real client's TCP cope with the closed window the flash
job's upload uses?). ovs-rtlpg's tools/rtlswitch.py Lab: the switch on a
private OVS bridge, a Linux host on front port 1; needs sudo.

  tools/curl_upload.py BOOT_IMAGE [--payload APP_IMAGE] [--ovs DIR] [--board FILE]
                       [--name e3] [--ip 192.168.2.2]

C1: a config save (curl -F, POST /config): 200, the console quiet.
C2: a firmware upload whose CRC is wrong (the payload, its last byte
    flipped): written, read back, refused (400), no reset.
C3: the payload as it is: accepted (200), then the reset.
For each: curl's time and upload speed, and from a capture on the host the
segments the switch sent with a zero window, the window updates after them,
the host's zero-window probes and retransmissions, and the longest silence
on the connection. Exit status 1 if an answer isn't the expected one.
"""
import argparse, os, re, subprocess, sys, tempfile, time


def sh(ns, *cmd, **kw):
    return subprocess.run(["sudo", "ip", "netns", "exec", ns, *cmd], capture_output=True, text=True, **kw)


def analyse(pcap, sw_ip):
    """What the capture says about the window: (zero-window segments from the
    switch, window updates after them, host retransmissions, host zero-window
    probes, longest gap between segments, s)"""
    out = subprocess.run(["tshark", "-r", pcap, "-T", "fields", "-e", "frame.time_relative", "-e", "ip.src",
                          "-e", "tcp.window_size_value", "-e", "tcp.len", "-e", "tcp.analysis.retransmission",
                          "-e", "tcp.analysis.zero_window_probe", "-e", "tcp.analysis.window_update",
                          "-Y", "tcp.port == 80"], capture_output=True, text=True).stdout
    zero = upd = rexmit = probe = 0
    last, gap, closed = None, 0.0, False
    for l in out.splitlines():
        f = l.split("\t")
        if len(f) < 7:
            continue
        t, src, win, ln = float(f[0]), f[1], int(f[2] or 0), int(f[3] or 0)
        if last is not None:
            gap = max(gap, t - last)
        last = t
        if src == sw_ip:
            if win == 0:
                zero += 1
                closed = True
            elif closed:
                upd += 1
                closed = False
        else:
            rexmit += bool(f[4])
            probe += bool(f[5])
    return zero, upd, rexmit, probe, gap


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--payload", default=None)
    ap.add_argument("--ovs", default=os.path.expanduser("~/src/ovs-rtlpg"))
    ap.add_argument("--board", default=None)
    ap.add_argument("--name", default="e3")
    ap.add_argument("--ip", default="192.168.2.2", help="the switch's address (its default config's)")
    a = ap.parse_args(argv)
    board = a.board or os.path.join(a.ovs, "boards", "keeplink_kp_9000_9xhpml_v1_2.json")
    payload = a.payload or a.image.replace("rtlplayground-boot-", "rtlplayground-")
    sys.path.insert(0, os.path.join(a.ovs, "tools"))
    from rtlswitch import Lab, PROMPT

    tmp = tempfile.mkdtemp(prefix="curl_upload-")
    bad = os.path.join(tmp, "bad.bin")
    data = bytearray(open(payload, "rb").read())
    data[-1] ^= 0xff
    open(bad, "wb").write(data)
    cfg = os.path.join(tmp, "config.txt")
    open(cfg, "w").write("set sys.hostname curlcfg\napply\n")
    host = a.ip.rsplit(".", 1)[0] + ".11/24"
    lab = Lab(a.image, board, a.name, {"1": host}, rtlsim=os.path.join(a.ovs, "build", "rtlsim"), console=True)
    ns = lab.host_ns("1")
    fails = []
    try:
        lab.start(attach=False)
        if not lab.console.expect(PROMPT, 120):
            raise SystemExit("curl_upload: no prompt")
        lab.attach()
        time.sleep(3)
        url = f"http://{a.ip}"
        r = sh(ns, "curl", "-s", "-i", "--max-time", "20", "-d", "pwd=1234", f"{url}/login")
        m = re.search(r"session=(\w+)", r.stdout)
        if not m:
            raise SystemExit(f"curl_upload: no login: {r.stdout[:200]!r} {r.stderr[:200]!r}")
        cookie = f"session={m.group(1)}"

        def upload(name, path, field, file, want):
            pcap = os.path.join(tmp, f"{name}.pcap")
            cap = subprocess.Popen(["sudo", "ip", "netns", "exec", ns, "tcpdump", "-q", "-i", "eth0", "-w", pcap,
                                    "tcp", "port", "80"], stderr=subprocess.DEVNULL)
            time.sleep(1)
            n0 = len(lab.console.log)
            r = sh(ns, "curl", "-s", "-o", "/dev/stderr", "-w", "%{http_code} %{time_total} %{speed_upload}",
                   "--max-time", "600", "-H", f"Cookie: {cookie}", "-F", f"{field}=@{file}", f"{url}{path}")
            time.sleep(1)
            subprocess.run(["sudo", "kill", "-INT", str(cap.pid)])
            cap.wait()
            subprocess.run(["sudo", "chmod", "a+r", pcap])
            code, t, speed = (r.stdout.split() + ["?", "0", "0"])[:3]
            lab.console.expect(b"\xff", 1)	# take what the console said
            con = lab.console.log[n0:].decode(errors="replace")
            zero, upd, rexmit, probe, gap = analyse(pcap, a.ip)
            ok = code == want
            print(f"{name}: {'PASS' if ok else 'FAIL'} HTTP {code} (want {want}), {float(t):.2f} s, "
                  f"{float(speed) / 1024:.1f} KiB/s; switch zero-window segments {zero}, window updates {upd}, "
                  f"host retransmissions {rexmit}, zero-window probes {probe}, longest silence {gap:.3f} s")
            print("   console: " + " ".join(con.split())[:200])
            if not ok:
                fails.append(name)

        upload("C1 config", "/config", "configuration", cfg, "200")
        upload("C2 bad image", "/upload", "uploadedfile", bad, "400")
        upload("C3 image", "/upload", "uploadedfile", payload, "200")
    finally:
        lab.stop()
    print("curl_upload:", "FAIL " + " ".join(fails) if fails else "PASS", f"(captures in {tmp})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
