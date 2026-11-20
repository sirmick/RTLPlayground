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
import json
import os
import re
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import fwupdate  # noqa: E402
import rtlpg_client as rc  # noqa: E402

fails = 0
runs = 0
M = "KEEPLINK_KP_9000_9XHPML_V1_2"
MAC = "02:00:00:00:00:01"
OTHER = "02:00:00:00:00:99"
PCB = "TEST_PCB"
FACTORY = open(os.path.join(HERE, "..", "config.txt")).read().splitlines()


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

    def __init__(self, text, pw="secret9", refuse=(), lost=False, schema=None):
        self.text, self.pw, self.refuse = text, pw, refuse	# text None: GET /config fails
        self.om, self.at, self.uploads, self.lost = False, "old", 0, lost	# lost: unreachable after apply
        self.drops, self.texts = 0, None	# GET /config: connections dropped first; texts to answer in turn
        self.info, self.save_fail, self.pw_after = {}, False, None	# more /information.json; save fails; pw after apply
        self.impostor = None	# a role: another switch (om, OTHER, 1234) there, while the real one isn't
        self.gone, self.back = False, "boot"	# never back after the upload; else the role it is back at
        self.late = 0		# requests the real one leaves unanswered after the upload
        self.imp_log = []
        self.cand, self.run, self.saved = [], [], []
        self.log = []
        self.sch = rc.Schema(schema and [schema], rc.Board(M))
        self.srv = {}
        for role in ("old", "boot", "lease"):	# lease: 127.0.0.2, the old port (a DHCP address)
            addr = ("127.0.0.2", self.srv["old"].server_address[1]) if role == "lease" else ("127.0.0.1", 0)
            s = http.server.ThreadingHTTPServer(addr, self.handler(role))
            threading.Thread(target=s.serve_forever, args=(0.02,), daemon=True).start()
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
                        "state.sys.mac " + MAC, "sys.hostname x"]
            elif w[0] in ("set", "del"):
                if any(r in ln for r in self.refuse):
                    out.append("err %s: refused" % ln)	# echoes the line, a password too
                    continue
                self.cand.append(ln)
            elif ln == "discard":
                self.cand = []
            elif ln.startswith("apply"):
                self.run += self.cand
                self.cand = []
                for x in self.run:
                    if x.startswith("set web.password "):
                        self.pw = x.split()[2]
                if "set mgmt.mode dhcp" in self.run:
                    self.at = "lease"
                elif any(x.startswith("set mgmt.ip ") for x in self.run):
                    self.at = "old"
                if self.lost:
                    self.at = "nowhere"
                if self.pw_after:
                    self.pw = self.pw_after
            elif ln == "save":
                if not self.save_fail:
                    self.saved = list(self.run)
            elif ln == "get state.cfg.apply":
                out.append("state.cfg.apply done")
            elif ln == "get state.cfg.save":
                out.append("state.cfg.save " + ("failed" if self.save_fail else "done"))
            elif ln == "get -s":		# the factory default the firmware booted on, then the lines
                out += rc.bin2txt(self.sch, rc.txt2bin(self.sch, FACTORY + self.saved), secrets=False)
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
                self.other = fake.impostor == role and fake.om and fake.at != role
                if fake.at != role and not self.other:	# not at this address: no answer
                    self.close_connection = True
                    return
                if not self.other and fake.om and fake.late:
                    fake.late -= 1
                    self.close_connection = True
                    return
                super().handle_one_request()

            def do_GET(self):
                if self.other:
                    self.reply(200 if self.path == "/login.html" else 404, b"x")
                    return
                if self.path == "/login.html":
                    self.reply(200, b"login")
                elif self.path == "/information.json" and not fake.om:
                    info = dict({"sw_ver": "v-old", "hw_ver": "8x", "pcb_ver": PCB, "mac_address": MAC}, **fake.info)
                    self.reply(200, json.dumps(info).encode())
                elif self.path == "/config" and not fake.om and fake.text is not None:
                    if fake.drops:
                        fake.drops -= 1
                        self.close_connection = True
                        return
                    self.reply(200, (fake.texts.pop(0) if fake.texts else fake.text).encode())
                elif self.path == "/status.json":
                    self.reply(200, b"[]")
                else:
                    self.reply(404)

            def do_POST(self):
                b = self.body()
                if self.other:		# another switch on the object model, factory password
                    fake.imp_log.append((self.path, b.decode(errors="replace")))
                    if self.path == "/login":
                        ok = b.decode() == "pwd=1234"
                        self.reply(302, headers=[("Location", "/index.html" if ok else "/login.html")] +
                                   ([("Set-Cookie", "session=imp")] if ok else []))
                    elif self.path == "/cli":
                        self.reply(200, ("state.sys.version v-new\nstate.sys.mac %s\nok\n" % OTHER).encode())
                    else:
                        self.reply(404)
                    return
                if self.path == "/login":
                    ok = b.decode() == "pwd=" + fake.pw
                    self.reply(302, headers=[("Location", "/index.html" if ok else "/login.html")] +
                               ([("Set-Cookie", "session=abc")] if ok else []))
                elif self.path == "/cli" and fake.om:
                    self.reply(200, fake.cli(b.decode().split("\n")).encode())
                elif self.path == "/upload":
                    fake.uploads += 1
                    self.reply(200, b"Upload done, rebooting")
                    fake.om, fake.at, fake.pw = True, "nowhere" if fake.gone else fake.back, "1234"
                else:
                    self.reply(404)
        return H


PLAN = os.path.join(tempfile.mkdtemp(), "sw.plan")


def run(fake, image, *extra):
    out, err = io.StringIO(), io.StringIO()
    argv = ["-p", "secret9", "--timeout", "5", "--wait-timeout", "30", "--save-plan", PLAN,
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
    check("Config: 8 path lines, 0 legacy, 0 untranslated, 0 vlan problems, 0 warnings" in out
          and "  set mgmt.ip 127.0.0.1" in out and "at 192.168.10.247 or 192.168.2.2 until" in out, "the plan, and where it comes back", out)
    check("  set web.password *" in out and "secret9" not in out, "the password is shown as *", out)
    f.close()

    print("legacy lines: refused, then --drop-legacy")
    f = Fake(CLEAN + "igmp on\nloop on\nfrobnicate\n")	# P4-5's: still old commands
    st, out, err = run(f, img)
    check(st == 5 and f.uploads == 0, "refused before the upload (5)", err)
    check("legacy (P4-5): igmp on" in out and "legacy (P4-5): loop on" in out and "untranslated: frobnicate" in out
          and "3 config lines would be lost" in err, "each listed, with its node", out + err)
    st, out, err = run(f, img, "--drop-legacy")
    check(st == 0 and f.uploads == 1 and "Dropping 3 config lines" in err, "--drop-legacy updates", err)
    check(f.saved == ["set " + ln for ln in ["mgmt.mode static", "mgmt.ip 127.0.0.1", "mgmt.gateway 192.168.10.1",
                                             "sys.hostname old-one", "web.password secret9", "port.3.mtu 9000",
                                             "port.5.enabled off", "mgmt.netmask 255.255.255.0"]]
          and "Saved: get -s is the plan (5 lines)" in err,
          "the path lines set, applied and saved; get -s compared", (f.saved, err))
    check(f.log.index("apply 120") < f.log.index("confirm") < f.log.index("save") and f.at == "old"
          and f.pw == "secret9", "apply 120, back at its address with its password, confirm, save", f.log)
    text = open(PLAN).read()
    check(os.stat(PLAN).st_mode & 0o777 == 0o600 and text.startswith("# fwupdate plan: 127.0.0.1:")
          and "# dropped, legacy (P4-5): igmp on\n" in text and "# dropped, untranslated: frobnicate\n" in text
          and "\nmgmt.ip 127.0.0.1\n" in text and "\nweb.password *\n" in text and "secret9" not in text
          and text.split("\n")[0].endswith(", mac " + MAC)
          and err.find("Plan written to") < err.find("Uploading"),
          "the plan is written before the upload, owner-only, with what it drops, the password masked", text)
    steps = [err.find("Step %d/4" % n) for n in range(1, 5)]
    check(-1 not in steps and steps == sorted(steps) and err.find("Uploading") > steps[1],
          "read and convert, upload, wait, set: in that order", err)
    f.close()

    print("the switch refuses a line")
    f = Fake(CLEAN, refuse=("port.3.mtu",))
    st, out, err = run(f, img)
    check(st == 5 and "refused lines: err set port.3.mtu 9000: refused" in err and "discard" in f.log and "save" not in f.log,
          "discarded, not applied or saved (5)", err)
    f.close()

    print("GET /config: a lost connection is asked again; two reads must agree")
    f = Fake(CLEAN)
    f.drops = 2
    st, out, err = run(f, img, "--dry-run")
    check(st == 0 and "Config: 8 path lines" in out and f.drops == 0, "two dropped connections, then read", out + err)
    f.texts = [CLEAN, CLEAN[:20]]
    st, out, err = run(f, img)
    check(st == 5 and f.uploads == 0 and "read twice differs" in out, "a read that differs: refused (5)", out + err)
    f.close()

    print("an old config without an ip line: the built-in address, not config.txt's")
    f = Fake("hostname no-ip\nmtu 3 9000\n")
    st, out, err = run(f, img)
    check(st == 0 and "set mgmt.ip 192.168.2.2  (the factory default sets it" in out
          and "set mgmt.ip 192.168.2.2" in f.saved and "Saved: get -s is the plan (2 lines)" in err,
          "config.txt's mgmt.* go back to the built-ins; get -s is the plan", (out, f.saved, err))
    f.close()

    print("not reached after the apply: the confirm window rolls back")
    fwupdate.CONFIRM_S, keep = 1, fwupdate.CONFIRM_S
    f = Fake(CLEAN, lost=True)
    st, out, err = run(f, img)
    fwupdate.CONFIRM_S = keep
    check(st == 5 and "within the confirm window" in err and "confirm" not in f.log and "save" not in f.log
          and "nothing is saved" in err and "--plan %s 127.0.0.1" % PLAN in err,
          "exit 5, no confirm or save, and how to redo step 4", err)
    f.close()

    print("no ip line, the old switch on its own address: pinned to it")
    f = Fake("hostname no-ip\n")
    f.info = {"ip_address": "192.168.2.7", "ip_gateway": "192.168.2.1", "ip_netmask": "255.255.0.0"}
    st, out, err = run(f, img)
    check(st == 0 and "set mgmt.ip 192.168.2.7" in f.saved and "set mgmt.gateway 192.168.2.1" in f.saved
          and "set mgmt.netmask 255.255.0.0" in f.saved and "as the old switch ran" in out,
          "mgmt.* as the old switch's /information.json said", (f.saved, out))
    f.close()

    print("a Secret in a reply isn't printed")
    f = Fake(CLEAN, refuse=("web.password",))
    st, out, err = run(f, img)
    check(st == 5 and "err set web.password *: refused" in err and "secret9" not in out + err,
          "the switch's err line, the password as *", err)
    f.close()

    print("the save fails")
    f = Fake(CLEAN)
    f.save_fail = True
    st, out, err = run(f, img)
    check(st == 5 and "state.cfg.save failed" in err and "nothing is saved" in err and "--plan " + PLAN in err
          and not f.saved, "exit 5: nothing saved, where it is, how to redo it", err)
    f.close()

    print("the login fails after the apply (another password)")
    fwupdate.CONFIRM_S, keep = 1, fwupdate.CONFIRM_S
    f = Fake(CLEAN)
    f.pw_after = "other"
    st, out, err = run(f, img)
    fwupdate.CONFIRM_S = keep
    check(st == 5 and "within the confirm window" in err and "confirm" not in f.log and not f.saved,
          "exit 5, never confirmed: the window rolls it back", err)
    f.close()

    print("another switch at the factory address, factory password: refused")
    plan2 = PLAN + "2"
    fwupdate.SETTLE_S, keep_settle = 0, fwupdate.SETTLE_S
    f = Fake(CLEAN)
    f.impostor, f.back, f.late = "boot", "old", 1	# the real one back at HOST a moment later, another at the factory address
    st, out, err = run(f, img, "--save-plan", plan2)
    check(st == 0 and "Skipping 127.0.0.1:%d: another switch (MAC %s" % (f.port("boot"), OTHER) in err
          and "Saved: get -s is the plan" in err and f.imp_log and not [b for p, b in f.imp_log if "set " in b],
          "the other switch was asked who it is, never set; the real one updated", (f.imp_log, err))
    f.close()
    f = Fake(CLEAN)
    f.impostor, f.gone = "boot", True
    st, out, err = run(f, img, "--save-plan", plan2, "--wait-timeout", "2")
    check(st == 5 and "only another switch answered" in err and not [b for p, b in f.imp_log if "set " in b],
          "only another switch answers: exit 5, nothing set", err)
    f.close()
    fwupdate.SETTLE_S = keep_settle
    f = Fake("")
    f.om, f.at, f.pw, f.impostor = True, "boot", "1234", "old"	# --plan: another switch at HOST
    st, out, err = run(f, None, "--plan", PLAN)
    check(st == 0 and "another switch (MAC %s" % OTHER in err and "Config carried over" in out
          and not [b for p, b in f.imp_log if "set " in b],
          "--plan: the other switch at HOST skipped, the real one at its factory address set", err)
    f.close()

    print("DHCP: the new lease by the old MAC in the neighbour table")
    arp = os.path.join(tempfile.mkdtemp(), "arp")
    open(arp, "w").write("IP address HW type Flags HW address Mask Device\n127.0.0.2 0x1 0x2 %s * lo\n" % MAC)
    fwupdate.ARP, keep_arp = arp, fwupdate.ARP
    f = Fake("ip dhcp\nhostname leased\n")
    st, out, err = run(f, img, "--save-plan", plan2)
    check(st == 0 and "Logged in to 127.0.0.2:%d after the apply" % f.port("old") in err
          and "set mgmt.mode dhcp" in f.saved and not [x for x in f.saved if x.startswith("set mgmt.ip")]
          and "Saved: get -s is the plan" in err, "found at the lease, mgmt.ip not pinned, get -s config.txt + plan",
          (f.saved, err))
    f.close()
    open(arp, "w").write("IP address HW type Flags HW address Mask Device\n")
    fwupdate.CONFIRM_S, keep = 1, fwupdate.CONFIRM_S
    f = Fake("ip dhcp\nhostname leased\n")
    st, out, err = run(f, img, "--save-plan", plan2)
    fwupdate.CONFIRM_S, fwupdate.ARP = keep, keep_arp
    check(st == 5 and "(or a DHCP lease this host saw)" in err and "rolls back" in err and "--plan" in err
          and "save" not in f.log, "no lease seen: exit 5, the rollback, the --plan line", err)
    f.close()

    print("VLANs the new firmware refuses (vlan.V as P4-2's): refused, then --fit-vlans")
    from test_om_codec import vlan_schema_dir
    vd = vlan_schema_dir()
    text = CLEAN + "vlan 10 1 2\npvid 3 99\npvid 1 10\n" + "".join("vlan %d 1\n" % (100 + i) for i in range(64))
    f = Fake(text, schema=vd)
    st, out, err = run(f, img, "--schema", vd, "--save-plan", plan2)
    check(st == 5 and f.uploads == 0 and "3 vlan problems" in out and "vlan problem: port 3: pvid 99" in out
          and "vlan problem: vlan 163: over the 64" in out and "--fit-vlans" in err,
          "each listed, refused before the upload (5)", out + err)
    st, out, err = run(f, img, "--schema", vd, "--save-plan", plan2, "--fit-vlans")
    check(st == 0 and f.uploads == 1 and "set port.3.pvid 0" in f.saved and "set port.1.pvid 10" in f.saved
          and not [x for x in f.saved if x.startswith(("set vlan.162.", "set vlan.163."))]
          and "warning: port 3: pvid 99 parked on 0" in out and "warning: vlan 163: dropped" in out
          and "Saved: get -s is the plan" in err, "--fit-vlans: parked, dropped, warned, saved", (out[-600:], err))
    f.close()

    print("an old `mac` line: refused, unless --expect-mac gives the hardware MAC")
    f = Fake(CLEAN + "mac 02:11:22:33:44:55\n")
    f.info = {"mac_address": "02:11:22:33:44:55"}	# the configured MAC; state.sys.mac will be MAC
    st, out, err = run(f, img, "--save-plan", plan2, "--drop-legacy")
    check(st == 5 and f.uploads == 0 and "`mac 02:11:22:33:44:55`" in err and "--expect-mac" in err,
          "the line named, refused before the upload (5)", err)
    st, out, err = run(f, img, "--save-plan", plan2, "--drop-legacy", "--expect-mac", MAC.upper().replace(":", "-"))
    check(st == 0 and f.uploads == 1 and "Saved: get -s is the plan" in err
          and open(plan2).readline().rstrip().endswith("mac " + MAC), "--expect-mac: found by the hardware MAC", err)
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

    print("--plan: step 4 alone, from the written plan")
    f = Fake("")
    f.om, f.at, f.pw = True, "boot", "1234"		# updated, the config never set
    st, out, err = run(f, None, "--plan", PLAN)
    check(st == 0 and f.uploads == 0 and f.at == "old" and f.pw == "secret9" and "save" in f.log
          and "Saved: get -s is the plan (5 lines)" in err and "Config carried over" in out,
          "set, applied, saved and compared, no upload", (f.log, err))
    st, out, err = run(f, None, "--plan", PLAN)
    check(st == 0 and f.log.count("save") == 2, "and again: it is idempotent", err)
    f.close()
    f = Fake(CLEAN)
    st, out, err = run(f, None, "--plan", PLAN)
    check(st == 5 and "doesn't run the object model" in err and not f.om, "refused on the old firmware (5)", err)
    f.close()
    open(PLAN + "x", "w").write("mgmt.ip 1.2.3.4\n")
    f = Fake("")
    st, out, err = run(f, None, "--plan", PLAN + "x")
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
