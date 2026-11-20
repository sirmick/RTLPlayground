#!/usr/bin/env python3
"""
flash_job_steps.py - the flash job's steps (E3, doc/sched/flash.md) in rtlsim,
on one firmware image, through ovs-rtlpg's old-vs-new bench (tests/oldnew).

  tools/flash_job_steps.py BOOT_IMAGE [--payload APP_IMAGE] [--ovs DIR] [--board FILE]

S0: past the loader's confirm (LDR_CONFIRM_S, 15 s after the boot), whose
    boot-count erase is the flash job's: one FL_ERASE, HOUSE_SEC short.
Then `do sched.clear`, then each of these, each followed by
the longest step per state (`get state.sched.state`, or for the factory reset, which
resets, the scheduler's per-state maxima read from xdata as it ends):
S1: a firmware upload whose CRC is wrong (the payload with its last byte
    flipped), so it is written, read back and refused, without a reset:
    the answer is "checksum failed" and the staged image is the payload's
    bytes (the flipped one too).
S2: a config save (POST /config): 200, the text in the config sector.
S3: `factory-reset` on the console: the default config sector copied to the
    config sector, then the reset.
S4: after a new boot, the payload uploaded as it is: written, read back and
    accepted ("Checksum OK"); the per-state maxima read from xdata then,
    before the client's ACK of the answer lets the switch reset.
S5: after a new boot, a refused upload started at once, so the loader's
    confirm comes during it: the confirm waits (its erase isn't run between
    two segments), the upload ends with its answer and every byte, and the
    boot count is erased after it (one FL_ERASE more than the image's).
S6: a refused upload from a client that keeps to the window and, as Linux's
    persist timer, sends a zero-window probe (empty, one below what was
    ACKed) every 200 ms while the window is shut; the first window update
    after a zero window is lost (the client ignores it). The probe's ACK
    reopens the window: the upload ends with its answer, every byte staged,
    in far less than the 30 s abort.
S7: as S5, but the upload's multipart preamble comes 2 bytes a segment, so
    the confirm's erase ends while it is collected; then the client sends
    to the right edge the switch advertised, as Linux may. That edge never
    moves left (a job's end advertises the stage's room behind the
    preamble, not all of it), nothing is sent again, and the upload ends
    with its answer and every byte staged.
Each passes if no step took over 50 ms (the step limit, decision 25; a
sector erase, ~40 ms, is inside it now), and the flash holds what
it should. The upload's firmware time is printed. Needs a bench whose client
honours the switch's window (the upload closes it after each segment).
Exit status 1 if any fails.
"""
import argparse, os, re, sys

CONFIG_START, CONFIG_LEN = 0x7f000, 0x1000	# layout.h, compact
DEFAULT_CONFIG_START = 0x39000
UPLOAD_START = 0x40000
LDR_BOOTCNT = 0x3a000				# layout.h
LIMIT_US = 50000				# the step limit (decision 25)
FINE_NS = 6144					# sched.h SCHED_FINE_NS


HEALTH = "get state.sched.state"		# P5-2: health tasks went to state.sched
HEALTH_CLEAR = "do sched.clear"


def health(out):
    """state -> (runs, longest µs) from get state.sched.state (max_us, 402640
    and more: saturated), or an older image's `health tasks`"""
    res = {}
    for m in re.finditer(r"^state\.sched\.state\.(\w+)\.runs (\d+)\nstate\.sched\.state\.\1\.max_us (\d+)$", out, re.M):
        res[m[1]] = (int(m[2]), int(m[3]))
    if res:
        return res
    for l in out.split("state\truns\tmax", 1)[-1].splitlines():
        m = re.match(r"(\w+)\t(\d+)\t(>?)(\d+)(us|ms)$", l)
        if m:
            st, runs, over, v, unit = m.groups()
            res[st] = (int(runs), int(v) * (1000 if unit == "ms" else 1) + (1 if over else 0))
    return res


def judge(name, steps):
    long = {s: v for s, v in steps.items() if v[1] > LIMIT_US}
    fl = " ".join(f"{s} {v[0]}x max {v[1] / 1000:.1f} ms" for s, v in sorted(steps.items()) if s.startswith("FL_"))
    top = max(((v[1], s) for s, v in steps.items() if s != "FL_ERASE"), default=(0, "-"))
    hs = steps.get("HOUSE_SEC")
    print(f"   {name}: {fl}; longest other {top[1]} {top[0] / 1000:.1f} ms"
          + (f"; HOUSE_SEC {hs[0]}x max {hs[1] / 1000:.1f} ms" if hs else ""))
    for s, v in long.items():
        print(f"   {name}: LONG {s} {v[1] / 1000:.1f} ms ({v[0]} runs)")
    return not long and any(s.startswith("FL_") for s in steps)


def probe_upload(r, req, until, probe_us=200000):
    """req on a new connection from r.host, keeping to the window; while it is
    shut, an empty probe one below what was ACKed every probe_us; the first
    window update after a zero window ignored. -> (response, probes sent,
    zero windows seen, updates dropped)"""
    import struct
    h, b = r.host, r.b
    if not h.arp(until):
        return None, 0, 0, 0
    sport, h.next_port = h.next_port, h.next_port + 1
    iss = 1000 + (sport - 40000) * 100000
    st = {"snd": iss + 1, "una": iss, "rcv": None, "wnd": 1, "resp": b"", "mss": 536,
          "zero": 0, "dropped": 0, "closed": False}
    h.rx = []

    def segs():
        for sg in h.rx:
            if struct.unpack("!H", sg[2:4])[0] != sport:
                continue
            seq, ack, off, fl, wnd = struct.unpack("!IIBBH", sg[4:16])
            data = sg[(off >> 4) * 4:]
            if fl & 0x02 and st["rcv"] is None:
                st["rcv"] = seq + 1
                o = sg[20:(off >> 4) * 4]
                if len(o) >= 4 and o[0] == 2:
                    st["mss"] = min(h.MSS, struct.unpack("!H", o[2:4])[0])
                st["una"] = ack
                st["wnd"] = wnd
                continue
            if fl & 0x10:
                if wnd == 0 and not st["closed"]:
                    st["zero"] += 1
                    st["closed"] = True
                elif wnd and st["closed"] and not data and ack == st["una"] and not st["dropped"]:
                    st["dropped"] += 1		# the first window update: lost
                    continue
                if wnd:
                    st["closed"] = False
                st["una"] = max(st["una"], ack)
                st["wnd"] = wnd
            if st["rcv"] is not None and seq == st["rcv"] and data:
                st["resp"] += data
                st["rcv"] += len(data)
                h._tcp(sport, st["snd"], st["rcv"], 0x10)
        h.rx = []

    def complete():
        head, sep, body = st["resp"].partition(b"\r\n\r\n")
        m = re.search(rb"(?im)^content-length: *(\d+)", head) if sep else None
        return m is not None and len(body) >= int(m.group(1))

    def run(cond, us):
        return b.advance(min(until, b.now + us), lambda: (segs(), cond())[1])

    h._tcp(sport, iss, 0, 0x02, mss=True)
    run(lambda: st["rcv"] is not None, 1000000)
    h._tcp(sport, st["snd"], st["rcv"], 0x10)
    off, probes = 0, 0
    while not complete() and b.now < until:
        if off < len(req) and st["una"] >= st["snd"] and st["wnd"] > 0:
            chunk = req[off:off + min(st["mss"], st["wnd"])]
            h._tcp(sport, st["snd"], st["rcv"], 0x18, chunk)
            st["snd"] += len(chunk)
            off += len(chunk)
            run(lambda: st["una"] >= st["snd"] or complete(), 1000000)
        elif st["wnd"] == 0:
            if not run(lambda: st["wnd"] > 0 or complete(), probe_us):
                h._tcp(sport, st["snd"] - 1, st["rcv"], 0x10)	# the probe
                probes += 1
        else:
            run(complete, 1000000)
    return (st["resp"] if complete() else None), probes, st["zero"], st["dropped"]


def edge_upload(r, req, head_len, until, dribble=2, pace_us=250000):
    """req on a new connection: its first head_len bytes in one segment, then
    `dribble` bytes a segment, one every pace_us, until a window over an MSS
    is seen (a job's end opened it); then the rest sent up to the right edge
    the switch advertised (ACK + window), without waiting for ACKs. Whatever
    isn't ACKed in 1 s is sent again (go-back-N); a zero window is probed.
    -> (response, the byte the window opened at, times the right edge
    moved left, the most it moved left by, go-backs)"""
    import struct
    h, b = r.host, r.b
    if not h.arp(until):
        return None, None, 0, 0, 0
    sport, h.next_port = h.next_port, h.next_port + 1
    iss = 1000 + (sport - 40000) * 100000
    st = {"snd": iss + 1, "una": iss, "rcv": None, "wnd": 1, "resp": b"", "mss": 536,
          "edge": 0, "left": 0, "short": 0}
    h.rx = []

    def segs():
        for sg in h.rx:
            if struct.unpack("!H", sg[2:4])[0] != sport:
                continue
            seq, ack, off, fl, wnd = struct.unpack("!IIBBH", sg[4:16])
            data = sg[(off >> 4) * 4:]
            if fl & 0x02 and st["rcv"] is None:
                st["rcv"] = seq + 1
                o = sg[20:(off >> 4) * 4]
                if len(o) >= 4 and o[0] == 2:
                    st["mss"] = min(h.MSS, struct.unpack("!H", o[2:4])[0])
                st["una"], st["wnd"], st["edge"] = ack, wnd, ack + wnd
                continue
            if fl & 0x10:
                if ack + wnd < st["edge"]:
                    st["left"] += 1
                    st["short"] = max(st["short"], st["edge"] - ack - wnd)
                st["edge"] = max(st["edge"], ack + wnd)
                if ack >= st["una"]:
                    st["una"], st["wnd"] = ack, wnd
            if st["rcv"] is not None and seq == st["rcv"] and data:
                st["resp"] += data
                st["rcv"] += len(data)
                h._tcp(sport, st["snd"], st["rcv"], 0x10)
        h.rx = []

    def complete():
        head, sep, body = st["resp"].partition(b"\r\n\r\n")
        m = re.search(rb"(?im)^content-length: *(\d+)", head) if sep else None
        return m is not None and len(body) >= int(m.group(1))

    def run(cond, us):
        return b.advance(min(until, b.now + us), lambda: (segs(), cond())[1])

    def send(n):
        off = st["snd"] - iss - 1
        h._tcp(sport, st["snd"], st["rcv"], 0x18, req[off:off + n])
        st["snd"] += len(req[off:off + n])

    h._tcp(sport, iss, 0, 0x02, mss=True)
    run(lambda: st["rcv"] is not None, 1000000)
    h._tcp(sport, st["snd"], st["rcv"], 0x10)
    send(head_len)
    run(lambda: st["una"] >= st["snd"], 1000000)
    opened = None
    while opened is None and st["snd"] - iss - 1 < len(req) and b.now < until:
        send(dribble)
        run(lambda: st["una"] >= st["snd"], pace_us)
        run(lambda: False, pace_us)
        if st["wnd"] > st["mss"]:
            opened = st["snd"] - iss - 1
    resends, last = 0, (st["una"], b.now)
    while not complete() and b.now < until:
        n = min(st["mss"], st["una"] + st["wnd"] - st["snd"], len(req) - (st["snd"] - iss - 1))
        if n > 0:
            send(n)
            continue
        if st["wnd"] == 0:
            if not run(lambda: st["wnd"] > 0 or complete(), 200000):
                h._tcp(sport, st["una"] - 1, st["rcv"], 0x10)	# the probe
            continue
        u = st["una"]
        run(lambda: st["una"] != u or complete(), 200000)
        if st["una"] != last[0]:
            last = (st["una"], b.now)
        elif st["una"] < st["snd"] and b.now - last[1] > 1000000:
            st["snd"] = st["una"]		# go back: send it again
            resends += 1
            last = (st["una"], b.now)
    resp = st["resp"] if complete() else None
    return resp, opened, st["left"], st["short"], resends


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--payload", default=None, help="the image to upload (default: the application next to IMAGE)")
    ap.add_argument("--ovs", default=os.path.expanduser("~/src/ovs-rtlpg"))
    ap.add_argument("--board", default=None)
    a = ap.parse_args(argv)
    board = a.board or os.path.join(a.ovs, "boards", "keeplink_kp_9000_9xhpml_v1_2.json")
    payload = a.payload or a.image.replace("rtlplayground-boot-", "rtlplayground-")
    build = os.path.dirname(os.path.abspath(a.image))
    sys.path.insert(0, os.path.join(a.ovs, "tests", "oldnew"))
    sys.path.insert(0, os.path.join(a.ovs, "tests"))
    from oldnew import Run

    gen = open(os.path.join(build, "sched", "sched_gen.h")).read()
    names = re.search(r"enum\s*\{\s*S_NONE\s*,(.*?)S_COUNT", gen, re.S).group(1)
    names = ["NONE"] + [n.strip()[2:] for n in names.split(",") if n.strip()]
    xsym = {}
    for l in open(os.path.join(build, "rtlplayground.map")):
        m = re.match(r"\w*:?\s+([0-9A-F]{8})\s+(_sched_st_max|_sched_st_runs)\s", l)
        if m:
            xsym[m.group(2)] = int(m.group(1), 16)

    def xsteps(r):
        """state -> (runs, longest µs), from the scheduler's xdata"""
        mx = r.b.sim.peek(f"x:{xsym['_sched_st_max']:#x}", 2 * len(names))
        rn = r.b.sim.peek(f"x:{xsym['_sched_st_runs']:#x}", 2 * len(names))
        steps = {}
        for i, n in enumerate(names):
            runs = rn[2 * i] | rn[2 * i + 1] << 8
            if runs:
                steps[n] = (runs, (mx[2 * i] | mx[2 * i + 1] << 8) * FINE_NS // 1000)
        return steps

    def upload_req(r, data, name):
        bnd = "----flashjob0123456789"
        body = (f"--{bnd}\r\nContent-Disposition: form-data; name=\"uploadedfile\"; filename=\"{name}\"\r\n"
                f"Content-Type: application/octet-stream\r\n\r\n").encode() + data + f"\r\n--{bnd}--\r\n".encode()
        return (b"POST /upload HTTP/1.1\r\nCookie: session=" + r.cookie +
                f"\r\nContent-Type: multipart/form-data; boundary={bnd}\r\nContent-Length: {len(body)}\r\n\r\n".encode() + body)

    fails = []
    r = Run(a.image, board, os.path.join(a.ovs, "build", "rtlsim"), payload, om=("state.mgmt",))
    try:
        r.boot()
        r.login()
        r.wait(8000)		# past the loader's confirm, LDR_CONFIRM_S after the boot
        r._now()

        def ask(line, ms=8000):
            r.b.pending()
            _, out = r.b.console(line, r.b.now + ms * 1000)
            return out or ""

        # S0
        steps = health(ask(HEALTH))
        hs = steps.get("HOUSE_SEC", (0, 0))
        ok = judge("S0", steps) and hs[1] < 5000 and steps.get("FL_ERASE", (0, 0))[0] == 1
        print(f"S0 the loader's confirm, {r.b.now / 1e6:.1f} s after power-on: {'PASS' if ok else 'FAIL'}")
        if not ok:
            fails.append("S0")

        # S1
        ask(HEALTH_CLEAR)
        bad = bytearray(r.payload)
        bad[-1] ^= 0xff
        req = upload_req(r, bytes(bad), "bad.bin")
        bnd = "----flashjob0123456789"
        t0 = r.b.now
        resp = r.host.http(req, r.b.now + 120 * 1000000)
        t1 = r.b.now
        staged = r.b.flash(UPLOAD_START, len(bad))
        ok = isinstance(resp, bytes) and b"checksum failed" in resp and staged == bytes(bad)
        print(f"   S1 upload of {len(bad)} B: {(t1 - t0) / 1e6:.3f} s firmware time; answer "
              f"{resp.split(b'\r\n')[0] if isinstance(resp, bytes) else resp!r}; staged image "
              f"{'= the payload' if staged == bytes(bad) else 'DIFFERS'}")
        ok = judge("S1", health(ask(HEALTH))) and ok
        print(f"S1 upload (refused by CRC): {'PASS' if ok else 'FAIL'}")
        if not ok:
            fails.append("S1")

        # S2: uIP has one connection, and the bench doesn't ACK the switch's
        # FIN: S1's connection holds it in LAST_ACK for a while
        r.b.advance(r.b.now + 20 * 1000000)
        ask(HEALTH_CLEAR)
        text = "set sys.hostname flashjob\napply\nvlan 10 " + " ".join(f"{f}" for f in r.F[1:3]) + "\n"
        body = (f"--{bnd}\r\nContent-Disposition: form-data; name=\"configuration\"; filename=\"config.txt\"\r\n"
                f"Content-Type: application/octet-stream\r\n\r\n").encode() + text.encode() + f"\r\n--{bnd}--\r\n".encode()
        req = (b"POST /config HTTP/1.1\r\nCookie: session=" + r.cookie +
               f"\r\nContent-Type: multipart/form-data; boundary={bnd}\r\nContent-Length: {len(body)}\r\n\r\n".encode() + body)
        resp = r.host.http(req, r.b.now + 10 * 1000000)
        saved = r.b.flash(CONFIG_START, len(text) + 1)
        ok = isinstance(resp, bytes) and resp.startswith(b"HTTP/1.1 200") and saved == text.encode() + b"\0"
        print(f"   S2 answer {resp.split(b'\r\n')[0] if isinstance(resp, bytes) else resp!r}; config sector "
              f"{'= the text' if saved == text.encode() + b'\0' else repr(saved[:40])}")
        ok = judge("S2", health(ask(HEALTH))) and ok
        print(f"S2 config save: {'PASS' if ok else 'FAIL'}")
        if not ok:
            fails.append("S2")

        # S3
        ask(HEALTH_CLEAR)
        r.b.pending()
        r.b.sim.uart_write("factory-reset\r")
        r.b.sim.run_until_uart("Written default config to flash", 5000)
        steps = xsteps(r)
        r.b.sim.run(3000)
        default = r.b.flash(DEFAULT_CONFIG_START, CONFIG_LEN)
        cfg = r.b.flash(CONFIG_START, CONFIG_LEN)
        out = r.b.pending() + r.b.sim.uart_text()
        ok = cfg == default
        print(f"   S3 config sector {'= the default config sector' if ok else 'DIFFERS from the default'}")
        ok = judge("S3", steps) and ok
        print(f"S3 factory reset: {'PASS' if ok else 'FAIL'}")
        if not ok:
            fails.append("S3")
    finally:
        r.b.close()

    r = Run(a.image, board, os.path.join(a.ovs, "build", "rtlsim"), payload, om=("state.mgmt",))
    try:
        r.boot()
        r.login()
        r.wait(8000)
        r._now()
        r.b.pending()
        r.b.console(HEALTH_CLEAR, r.b.now + 8000000)
        adv = r.b.advance
        r.b.advance = lambda until, stop=None: adv(until, lambda: (stop() if stop else False)
                                                    or "Checksum OK" in r.b.take_uart())
        t0 = r.b.now
        r.host.http(upload_req(r, r.payload, "good.bin"), r.b.now + 120 * 1000000)
        t1 = r.b.now
        r.b.advance = adv
        r.b.sim.run(20)		# the verdict's step ends (its print); the answer waits for our ACK
        steps = xsteps(r)
        staged = r.b.flash(UPLOAD_START, len(r.payload))
        ok = "Checksum OK" in r.b.uart and staged == r.payload
        print(f"   S4 upload of {len(r.payload)} B: {(t1 - t0) / 1e6:.3f} s firmware time to the verdict; "
              f"{'Checksum OK' if 'Checksum OK' in r.b.uart else 'no Checksum OK'}; staged image "
              f"{'= the payload' if staged == r.payload else 'DIFFERS'}")
        ok = judge("S4", steps) and ok
        print(f"S4 upload (accepted): {'PASS' if ok else 'FAIL'}")
        if not ok:
            fails.append("S4")
    finally:
        r.b.close()
    r = Run(a.image, board, os.path.join(a.ovs, "build", "rtlsim"), payload, om=("state.mgmt",))
    try:
        r.boot(slot=12000)	# the prompt at ~11 s, the confirm at ~17.6 s (sim)
        r.login(slot=1000)
        r._now()
        bc0 = r.b.flash(LDR_BOOTCNT, 2)
        bad = bytearray(r.payload)
        bad[-1] ^= 0xff
        t0 = r.b.now
        resp = r.host.http(upload_req(r, bytes(bad), "bad.bin"), r.b.now + 120 * 1000000)
        t1 = r.b.now
        mid = health(r.b.console(HEALTH, r.b.now + 8000000)[1] or "")
        r.b.advance(r.b.now + 3000000)
        after = health(r.b.console(HEALTH, r.b.now + 8000000)[1] or "")
        staged = r.b.flash(UPLOAD_START, len(bad))
        bc1 = r.b.flash(LDR_BOOTCNT, 2)
        sectors = (len(bad) + 0xfff) // 0x1000
        e_mid, e_after = mid.get("FL_ERASE", (0, 0))[0], after.get("FL_ERASE", (0, 0))[0]
        ok = (isinstance(resp, bytes) and b"checksum failed" in resp and staged == bytes(bad)
              and bc0 == b"BC" and bc1 == b"\xff\xff" and e_after == sectors + 1)
        print(f"   S5 upload {t0 / 1e6:.1f}..{t1 / 1e6:.1f} s after power-on; answer "
              f"{resp.split(b'\r\n')[0] if isinstance(resp, bytes) else resp!r}; staged image "
              f"{'= the payload' if staged == bytes(bad) else 'DIFFERS'}; boot count {bc0!r} -> {bc1!r}; "
              f"FL_ERASE {e_mid} at the answer, {e_after} 3 s later ({sectors} sectors)")
        # the boot's own steps are in these numbers (no health clear before
        # the upload, to keep it before the confirm): judged are the job's
        # and house's
        ok = judge("S5", {k: v for k, v in after.items() if k.startswith("FL_") or k == "HOUSE_SEC"}) and ok
        ok = ok and after.get("HOUSE_SEC", (0, 0))[1] < 5000
        print(f"S5 upload over the loader's confirm: {'PASS' if ok else 'FAIL'}")
        if not ok:
            fails.append("S5")
    finally:
        r.b.close()
    r = Run(a.image, board, os.path.join(a.ovs, "build", "rtlsim"), payload, om=("state.mgmt",))
    try:
        r.boot()
        r.login()
        r.wait(8000)
        r._now()
        bad = bytearray(r.payload)
        bad[-1] ^= 0xff
        t0 = r.b.now
        resp, probes, zero, dropped = probe_upload(r, upload_req(r, bytes(bad), "bad.bin"), r.b.now + 120 * 1000000)
        t1 = r.b.now
        staged = r.b.flash(UPLOAD_START, len(bad))
        ok = (isinstance(resp, bytes) and b"checksum failed" in resp and staged == bytes(bad)
              and dropped == 1 and probes >= 1 and t1 - t0 < 30 * 1000000)
        print(f"   S6 upload {(t1 - t0) / 1e6:.3f} s; answer {resp.split(b'\r\n')[0] if resp else None!r}; staged image "
              f"{'= the payload' if staged == bytes(bad) else 'DIFFERS'}; zero windows {zero}, window updates "
              f"dropped {dropped}, probes sent {probes}")
        print(f"S6 a lost window update, repaired by a probe: {'PASS' if ok else 'FAIL'}")
        if not ok:
            fails.append("S6")
    finally:
        r.b.close()
    r = Run(a.image, board, os.path.join(a.ovs, "build", "rtlsim"), payload, om=("state.mgmt",))
    try:
        r.boot(slot=12000)	# as S5: the confirm at ~17.6 s, during the preamble
        r.login(slot=1000)
        r._now()
        bad = bytearray(r.payload)
        bad[-1] ^= 0xff
        req = upload_req(r, bytes(bad), "bad.bin")
        t0 = r.b.now
        resp, opened, left, short, resends = edge_upload(r, req, req.index(b"\r\n\r\n") + 4,
                                                         r.b.now + 120 * 1000000)
        t1 = r.b.now
        staged = r.b.flash(UPLOAD_START, len(bad))
        pre = req.index(b"\r\n\r\n", req.index(b"\r\n\r\n") + 4) + 4
        ok = (isinstance(resp, bytes) and b"checksum failed" in resp and staged == bytes(bad)
              and opened is not None and opened < pre and not left and not resends)
        print(f"   S7 upload {t0 / 1e6:.1f}..{t1 / 1e6:.1f} s after power-on; window opened at byte "
              f"{opened} (the preamble ends at {pre}); answer "
              f"{resp.split(b'\r\n')[0] if isinstance(resp, bytes) else resp!r}; staged image "
              f"{'= the payload' if staged == bytes(bad) else 'DIFFERS'}; right edge moved left {left}x "
              f"(by up to {short} B), go-backs {resends}")
        print(f"S7 a job's end during a slow preamble: {'PASS' if ok else 'FAIL'}")
        if not ok:
            fails.append("S7")
    finally:
        r.b.close()
    print("flash_job_steps:", "FAIL " + " ".join(fails) if fails else "PASS")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
