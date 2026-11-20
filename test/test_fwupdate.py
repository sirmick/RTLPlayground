#!/usr/bin/env python3
"""test_fwupdate.py - tools/fwupdate.py against a fake switch.

The fake (http.server, on two ports of 127.0.0.1: the switch's address and
the new firmware's built-in one) is old firmware until an upload: GET
/config is its command text, /information.json its version. After the
upload it is the object model, answering at the second port with the
default password, POST /cli taking set, apply (which moves it back to the
first port, and its password to web.password), save and get -s (through
rtlpg_client's codec). Checks: the images fwupdate takes, the dry run, the
refusal over legacy lines, --drop-legacy, the whole flow, a line the switch
refuses. The flow on the real firmware is ~/rtlpg-bench/object-model/sim_upgrade.py's.
"""
import contextlib
import http.server
import io
import os
import re
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import fwupdate  # noqa: E402
import rtlpg_client as rc  # noqa: E402

fails = 0
runs = 0
M = "KEEPLINK_KP_9000_9XHPML_V1_2"
PCB = "TEST_PCB"


def check(cond, msg, detail=""):
    global fails, runs
    runs += 1
    print(("  ok:   " if cond else "  FAIL: ") + msg)
    if not cond:
        fails += 1
        if detail:
            print("        " + str(detail).strip().replace("\n", "\n        "))


def crc_fixed(n, pcb=b""):
    """n bytes: the magic, pcb, and a tail that makes the CRC 0xb001."""
    data = bytearray(n)
    data[:3] = fwupdate.IMAGE_MAGIC
    data[16:16 + len(pcb)] = pcb
    c = fwupdate.crc16(bytes(data[:-2]))
    for x in range(65536):
        a, b = x & 0xff, x >> 8
        t = (c >> 8) ^ fwupdate.CRC_TABLE[(c ^ a) & 0xff]
        t = (t >> 8) ^ fwupdate.CRC_TABLE[(t ^ b) & 0xff]
        if t == fwupdate.IMAGE_CRC:
            data[-2:] = bytes([a, b])
            return bytes(data)
    raise AssertionError("no tail")


class Fake:
    """The switch: old firmware with `text` saved, password `pw`."""

    def __init__(self, text, pw="secret9", refuse=()):
        self.text, self.pw, self.refuse = text, pw, refuse	# text None: GET /config fails
        self.om, self.at, self.uploads = False, "old", 0
        self.cand, self.run, self.saved = [], [], []
        self.log = []
        self.sch = rc.Schema(board=rc.Board(M))
        self.srv = {}
        for role in ("old", "boot"):
            s = http.server.ThreadingHTTPServer(("127.0.0.1", 0), self.handler(role))
            threading.Thread(target=s.serve_forever, daemon=True).start()
            self.srv[role] = s

    def port(self, role):
        return self.srv[role].server_address[1]

    def close(self):
        for s in self.srv.values():
            s.shutdown()
            s.server_close()

    def cli(self, lines):
        out = []
        for ln in lines:
            self.log.append(ln)
            w = ln.split()
            if ln.startswith("get state.sys"):
                out += ["state.sys.version v-new", "state.sys.machine " + M, "state.sys.pcb " + PCB,
                        "sys.hostname x"]
            elif w[0] in ("set", "del"):
                if any(r in ln for r in self.refuse):
                    out.append("err set: refused")
                    continue
                self.cand.append(ln)
            elif ln == "discard":
                self.cand = []
            elif ln == "apply":
                self.run += self.cand
                self.cand = []
                for x in self.run:
                    if x.startswith("set web.password "):
                        self.pw = x.split()[2]
                if any(x.startswith("set mgmt.ip ") for x in self.run):
                    self.at = "old"
            elif ln == "save":
                self.saved = list(self.run)
            elif ln == "get state.cfg.apply":
                out.append("state.cfg.apply done")
            elif ln == "get state.cfg.save":
                out.append("state.cfg.save done")
            elif ln == "get -s":
                out += rc.bin2txt(self.sch, rc.txt2bin(self.sch, self.saved), secrets=False)
            out.append("ok")
        return "\n".join(out) + "\n"

    def handler(self, role):
        fake = self

        class H(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def reply(self, status, body=b"", headers=()):
                self.send_response(status)
                for k, v in headers:
                    self.send_header(k, v)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def body(self):
                return self.rfile.read(int(self.headers.get("Content-Length", 0)))

            def handle_one_request(self):
                if fake.at != role:		# not at this address: no answer
                    self.close_connection = True
                    return
                super().handle_one_request()

            def do_GET(self):
                if self.path == "/login.html":
                    self.reply(200, b"login")
                elif self.path == "/information.json" and not fake.om:
                    self.reply(200, ('{"sw_ver":"v-old","hw_ver":"8x","pcb_ver":"%s"}' % PCB).encode())
                elif self.path == "/config" and not fake.om and fake.text is not None:
                    self.reply(200, fake.text.encode())
                elif self.path == "/status.json":
                    self.reply(200, b"[]")
                else:
                    self.reply(404)

            def do_POST(self):
                b = self.body()
                if self.path == "/login":
                    ok = b.decode() == "pwd=" + fake.pw
                    self.reply(302, headers=[("Location", "/index.html" if ok else "/login.html")] +
                               ([("Set-Cookie", "session=abc")] if ok else []))
                elif self.path == "/cli" and fake.om:
                    self.reply(200, fake.cli(b.decode().split("\n")).encode())
                elif self.path == "/upload":
                    fake.uploads += 1
                    self.reply(200, b"Upload done, rebooting")
                    fake.om, fake.at, fake.pw = True, "boot", "1234"
                else:
                    self.reply(404)
        return H


PLAN = os.path.join(tempfile.mkdtemp(), "sw.plan")


def run(fake, image, *extra):
    out, err = io.StringIO(), io.StringIO()
    argv = ["-p", "secret9", "--timeout", "5", "--wait-timeout", "30", "--plan", PLAN,
            "--boot-host", "127.0.0.1:%d" % fake.port("boot"), *extra]
    argv += [image, "127.0.0.1:%d" % fake.port("old")] if image else ["127.0.0.1:%d" % fake.port("old")]
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        st = fwupdate.main(argv)
    return st, out.getvalue(), err.getvalue()


CLEAN = "ip 127.0.0.1\ngw 192.168.10.1\nhostname old-one\npasswd secret9\nmtu 3 9000\nport 5 off\n"


def main():
    fwupdate.time.sleep = lambda s: None	# the fake answers at once
    d = tempfile.mkdtemp()
    print("the images fwupdate takes")
    images = {}
    for name, data in (("standard", crc_fixed(fwupdate.IMAGE_SIZE, PCB.encode() + b"\0")),
                       ("app", crc_fixed(fwupdate.COMPACT_APP, PCB.encode() + b"\0"))):
        images[name] = data
    images["install"] = images["app"] + crc_fixed(fwupdate.LOADER_LEN)
    images["bad-loader"] = images["app"] + bytes(fwupdate.LOADER_LEN)
    images["short"] = images["app"][:-1]
    paths = {}
    for name, data in images.items():
        paths[name] = os.path.join(d, "rtlplayground-%s-v0-test-%s-compact.bin" % (name, M))
        open(paths[name], "wb").write(data)
    for name in ("standard", "app", "install"):
        check(fwupdate.load_image(paths[name]) == images[name], "taken: " + name)
    for name, why in (("bad-loader", "magic at 0x3a000"), ("short", "size is")):
        try:
            fwupdate.load_image(paths[name])
            check(False, "refused: " + name)
        except fwupdate.Failure as e:
            check(why in str(e), "refused: %s (%s)" % (name, e))
    img = paths["install"]

    print("the board from the image's name")
    a = fwupdate.parse_args([img])
    check(fwupdate.machine_of(a) == M, "machine " + M)
    a = fwupdate.parse_args([os.path.join(d, "fw.bin")])
    try:
        fwupdate.machine_of(a)
        check(False, "a name without a board is refused")
    except fwupdate.Failure as e:
        check("--machine" in str(e), "a name without a board asks for --machine")

    print("dry run")
    f = Fake(CLEAN)
    st, out, err = run(f, img, "--dry-run")
    check(st == 0 and f.uploads == 0 and not f.om, "nothing uploaded", err)
    check("Config: 7 path lines, 0 legacy, 0 untranslated, 0 warnings" in out
          and "  set mgmt.ip 127.0.0.1" in out and "at 192.168.10.247 or 192.168.2.2 until" in out, "the plan, and where it comes back", out)
    f.close()

    print("legacy lines: refused, then --drop-legacy")
    f = Fake(CLEAN + "lag 1 7 8\nstp on\nfrobnicate\n")
    st, out, err = run(f, img)
    check(st == 5 and f.uploads == 0, "refused before the upload (5)", err)
    check("legacy (P4-3): lag 1 7 8" in out and "legacy (P4-4): stp on" in out and "untranslated: frobnicate" in out
          and "3 config lines would be lost" in err, "each listed, with its node", out + err)
    st, out, err = run(f, img, "--drop-legacy")
    check(st == 0 and f.uploads == 1 and "Dropping 3 config lines" in err, "--drop-legacy updates", err)
    check(f.saved == ["set " + ln for ln in ["mgmt.mode static", "mgmt.ip 127.0.0.1", "mgmt.gateway 192.168.10.1",
                                             "sys.hostname old-one", "web.password secret9", "port.3.mtu 9000",
                                             "port.5.enabled off"]] and "Saved: get -s is the plan (5 lines)" in err,
          "the path lines set, applied and saved; get -s compared", (f.saved, err))
    check(f.log.index("apply") < f.log.index("save") and f.at == "old" and f.pw == "secret9",
          "back at its address with its password after the apply", f.log)
    text = open(PLAN).read()
    check(os.stat(PLAN).st_mode & 0o777 == 0o600 and text.startswith("# fwupdate plan: 127.0.0.1:")
          and "# dropped, legacy (P4-3): lag 1 7 8\n" in text and "# dropped, untranslated: frobnicate\n" in text
          and "\nmgmt.ip 127.0.0.1\n" in text and err.find("Plan written to") < err.find("Uploading"),
          "the plan is written before the upload, owner-only, with what it drops", text)
    steps = [err.find("Step %d/4" % n) for n in range(1, 5)]
    check(-1 not in steps and steps == sorted(steps) and err.find("Uploading") > steps[1],
          "read and convert, upload, wait, set: in that order", err)
    f.close()

    print("the switch refuses a line")
    f = Fake(CLEAN, refuse=("port.3.mtu",))
    st, out, err = run(f, img)
    check(st == 5 and "refused lines: err set: refused" in err and "discard" in f.log and "save" not in f.log,
          "discarded, not applied or saved (5)", err)
    f.close()

    print("--no-wait can't carry a config")
    f = Fake(CLEAN)
    st, out, err = run(f, img, "--no-wait")
    check(st == 1 and f.uploads == 0 and "--no-wait" in err, "refused before the upload", err)
    f.close()

    print("no readable config: refused, then --boot-defaults")
    for text, why in (("", "the saved config is blank"), (None, "GET /config returned HTTP 404")):
        f = Fake(text)
        st, out, err = run(f, img)
        check(st == 5 and f.uploads == 0 and why in out and "boot its factory default, at 192.168.10.247" in out,
              "%s: refused before the upload (5)" % why, out + err)
        st, out, err = run(f, img, "--boot-defaults")
        check(st == 0 and f.uploads == 1 and not [ln for ln in f.log if ln.split()[0] in ("set", "apply", "save")],
              "%s: --boot-defaults updates, sets nothing" % why, (f.log, err))
        f.close()

    print("--resume: step 4 again from the written plan")
    f = Fake("")
    f.om, f.at, f.pw = True, "boot", "1234"		# updated, the config never set
    st, out, err = run(f, None, "--resume", PLAN)
    check(st == 0 and f.uploads == 0 and f.at == "old" and f.pw == "secret9" and "save" in f.log
          and "Saved: get -s is the plan (5 lines)" in err and "Config carried over" in out,
          "set, applied, saved and compared, no upload", (f.log, err))
    st, out, err = run(f, None, "--resume", PLAN)
    check(st == 0 and f.log.count("save") == 2, "and again: it is idempotent", err)
    f.close()
    f = Fake(CLEAN)
    st, out, err = run(f, None, "--resume", PLAN)
    check(st == 5 and "doesn't run the object model" in err and not f.om, "refused on the old firmware (5)", err)
    f.close()
    open(PLAN + "x", "w").write("mgmt.ip 1.2.3.4\n")
    f = Fake("")
    st, out, err = run(f, None, "--resume", PLAN + "x")
    check(st == 1 and "not a plan fwupdate wrote" in err, "refused: a file fwupdate didn't write", err)
    f.close()

    print("a switch on the object model already: its config stays")
    f = Fake("")
    f.om, f.pw = True, "secret9"
    st, out, err = run(f, img, "--dry-run")
    check(st == 0 and f.uploads == 0 and "its config stays as it is" in out, "dry run: nothing to convert", out)
    st, out, err = run(f, img)
    check(st == 0 and f.uploads == 1 and not [ln for ln in f.log if ln.split()[0] in ("set", "apply", "save")]
          and "Firmware update complete" in out, "updated, no set, apply or save", (f.log, err))
    f.close()

    print("\n%d checks, %d failed" % (runs, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
