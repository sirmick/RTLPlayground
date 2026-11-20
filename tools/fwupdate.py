#!/usr/bin/env python3
"""Update an RTLPlayground switch over its web API.

The web UI logs in with a form POST to /login (field ``pwd``), keeps the
``session`` cookie from the reply, and posts the image as the ``uploadedfile``
part of a multipart/form-data request to /upload.  This tool performs the same
exchange, checks the image locally before sending it, prints the switch's
verdict, and waits for the switch to reboot and answer again.

The image is an RTLPlayground build, not the OEM upgrade image: the 512 KiB
standard image (output/rtlplayground_*.bin), or for LAYOUT=compact the
install image (rtlplayground-install-*.bin: the application, then the
loader) or the application alone.  The switch only accepts an image whose
size, header and CRC16 are valid; checking them here avoids an upload just
to be rejected.

Firmware before the object model keeps its config as command text, which
the new firmware doesn't read: it boots its factory default (DESIGN.md
15.2).  This tool is the only way such a switch keeps its config, in this
order:
  1. before the upload, read the saved config (GET /config) and convert it
     (tools/legacy_map.py, against the schema of this tree); print the
     plan.  --dry-run stops here.  A line it can't translate, or one of an
     object not on the object model yet (legacy), would be lost: it
     refuses unless --drop-legacy.  More VLANs than the new firmware
     holds, or a pvid on a VLAN the config lacks (the old firmware took
     both, the new refuses them): it refuses unless --fit-vlans.  No
     readable config (GET /config fails, or the sector is blank): it
     refuses unless --boot-defaults, and the switch then keeps the
     factory default;
  2. upload the image;
  3. find the new firmware: at HOST, then at the factory default's
     mgmt.ip (config.txt), then the schema's (or --boot-host);
  4. set the converted lines, apply them (the switch moves back to its
     address), save, and compare get -s with the plan.
Step 4 applies with a confirm window (apply 120): if the switch can't be
reached at its address after it, it rolls back to its factory default.
Every address is checked to be the same switch (state.sys.mac against the
old switch's MAC, kept in the plan) before set and before confirm: another
switch answering there is skipped.  With DHCP the new lease is looked for
by the MAC in this host's neighbour table only; if it isn't found, the
window rolls back, and `--plan HOST.plan <the lease's address>` redoes
step 4 once you know it.
Step 1 writes the plan to HOST.plan (--save-plan): if the update stops
after 2, `fwupdate.py --plan HOST.plan HOST` redoes step 4 from it.  The
web password is shown and written as * (step 4 sets the -p password); the
file is readable by its owner only.

Usage:
    fwupdate.py [options] IMAGE [HOST]

    fwupdate.py -p secret output/rtlplayground_....bin 192.168.10.247
    fwupdate.py -p secret --health output/rtlplayground_....bin 192.168.10.247
    fwupdate.py -p secret --check 192.168.10.247
    fwupdate.py -p secret --dry-run output/.../rtlplayground-install-....bin 192.168.10.247
    fwupdate.py -p secret --plan 192.168.10.247.plan 192.168.10.247
    RTLP_PASSWORD=secret fwupdate.py output/rtlplayground_....bin --no-wait

Exit status:
    0  image accepted (and, unless --no-wait, the switch came back)
    1  the local image is not a valid RTLPlayground image
    2  login failed
    3  the switch rejected the upload
    4  transfer, reboot or health read failed
    5  the config can't be carried over (refused before the upload, or a
       line not taken after it)
"""

import argparse
import getpass
import http.client
import json
import os
import re
import secrets
import sys
import time
import urllib.parse

IMAGE_SIZE = 512 * 1024
COMPACT_APP = 0x3a000		# layout.h IMAGE_SIZE, LAYOUT_COMPACT
LOADER_LEN = 0x4000		# LDR_LOADER_LEN: the install image's tail
IMAGE_MAGIC = b"\x00\x40\x02"
IMAGE_CRC = 0xB001
DEFAULT_HOST = "192.168.10.247"
DEFAULT_WAIT_TIMEOUT = 180
HERE = os.path.dirname(os.path.abspath(__file__))
LINK_SPEEDS = ("down", "10M", "100M", "1000M", "500M", "10G", "2.5G", "5G")


class Failure(Exception):
    """An error that ends the run with a specific exit status."""

    def __init__(self, message, status=4):
        super().__init__(message)
        self.status = status


def _crc_table():
    """Reflected CRC-16 table for poly 0xA001 (the switch's crc16_bank1)."""
    table = []
    for value in range(256):
        crc = value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
        table.append(crc)
    return table


CRC_TABLE = _crc_table()


def crc16(data):
    crc = 0
    for byte in data:
        crc = (crc >> 8) ^ CRC_TABLE[(crc ^ byte) & 0xFF]
    return crc


def load_image(path):
    try:
        with open(path, "rb") as image_file:
            data = image_file.read()
    except OSError as error:
        raise Failure(f"cannot read {path}: {error}", 1)

    # the parts that must each come to the CRC: standard, compact app, or
    # compact app + loader
    parts = {IMAGE_SIZE: [IMAGE_SIZE], COMPACT_APP: [COMPACT_APP],
             COMPACT_APP + LOADER_LEN: [COMPACT_APP, LOADER_LEN]}.get(len(data))
    if parts is None:
        raise Failure(
            f"{path}: size is {len(data)}, expected {IMAGE_SIZE} "
            f"({IMAGE_SIZE // 1024} KiB), or a compact image of "
            f"{COMPACT_APP} or {COMPACT_APP + LOADER_LEN}", 1)
    at = 0
    for n in parts:
        if data[at:at + len(IMAGE_MAGIC)] != IMAGE_MAGIC:
            raise Failure(
                f"{path}: missing bank header / LJMP magic at {at:#x}: not "
                "an RTLPlayground firmware image", 1)
        crc = crc16(data[at:at + n])
        if crc != IMAGE_CRC:
            raise Failure(
                f"{path}: CRC16 at {at:#x} is 0x{crc:04x}, expected "
                f"0x{IMAGE_CRC:04x}; the image is corrupt", 1)
        at += n
    return data


def parse_target(target, port_override):
    """Return (host, port) for a host, host:port or http://host URL."""
    if "://" in target:
        try:
            url = urllib.parse.urlsplit(target)
            port = url.port
        except ValueError as error:
            raise Failure(f"invalid target {target!r}: {error}", 1)
        if url.scheme.lower() != "http":
            raise Failure("only the switch's plain HTTP API is supported", 1)
        if url.path not in ("", "/"):
            raise Failure("the target must not include a path", 1)
        host = url.hostname
    else:
        host, port = target, None
        if target.count(":") == 1:
            host, possible_port = target.split(":", 1)
            if possible_port.isdigit():
                port = int(possible_port)

    if not host:
        raise Failure("no switch address given", 1)
    if port_override is not None:
        port = port_override
    if port is None:
        port = 80
    if not 0 < port < 65536:
        raise Failure(f"invalid port {port}", 1)
    return host, port


def http_request(host, port, timeout, method, path, body=None, cookie=None,
                 content_type=None):
    """Make one request; the switch closes the connection after every reply."""
    connection = http.client.HTTPConnection(host, port, timeout=timeout)
    headers = {"Connection": "close"}
    if cookie:
        headers["Cookie"] = f"session={cookie}"
    if content_type:
        headers["Content-Type"] = content_type
    try:
        connection.request(method, path, body=body, headers=headers)
        response = connection.getresponse()
        data = response.read()
        return (response.status, response.getheader("Location"),
                response.getheader("Set-Cookie"), data)
    except (OSError, http.client.HTTPException) as error:
        raise Failure(f"{method} {path}: {error}", 4)
    finally:
        connection.close()


def login(host, port, timeout, password):
    body = ("pwd=" + urllib.parse.quote_plus(password)).encode("ascii")
    status, location, set_cookie, _ = http_request(
        host, port, timeout, "POST", "/login", body=body,
        content_type="application/x-www-form-urlencoded")
    if status != 302:
        raise Failure(f"login returned HTTP {status}", 2)

    if "index.html" not in (location or ""):
        raise Failure("login failed: wrong password", 2)
    match = re.search(r"(?:^|;\s*)session=([^;]*)", set_cookie or "")
    if not match or not match.group(1):
        raise Failure("login succeeded but the reply had no session cookie", 2)
    return match.group(1)


# /information.json's keys (firmware before the object model) for the
# state.sys paths (the object model's POST /cli `get state.sys`)
INFO_KEYS = {"sw_ver": "version", "build_date": "build_date", "hw_ver": "machine",
             "pcb_ver": "pcb", "flash_size": "flash.size", "chip_temp": "temp_c",
             "hostname": "hostname", "ip_address": "mgmt.ip", "ip_gateway": "mgmt.gateway",
             "ip_netmask": "mgmt.netmask", "mac_address": "mac"}


def fetch_info(host, port, timeout, cookie):
    """The switch's state.sys as a dict ("version", "machine", ...): from
    POST /cli on the object model's firmware, else from /information.json."""
    info = {}
    try:
        status, _, _, body = http_request(
            host, port, timeout, "POST", "/cli", body=b"get state.sys sys.hostname",
            cookie=cookie, content_type="text/plain")
        if status == 200:
            for line in body.decode("utf-8", "replace").splitlines():
                path, _, value = line.partition(" ")
                if path.startswith("state.sys."):
                    info[path[len("state.sys."):]] = value
                elif path == "sys.hostname":	# config since P4-1
                    info["hostname"] = value
        if info:
            info["om"] = True
            return info
        status, _, _, body = http_request(
            host, port, timeout, "GET", "/information.json", cookie=cookie)
    except Failure:
        return None
    if status != 200:
        return None
    try:
        old = json.loads(body.decode("utf-8", "replace"))
    except ValueError:
        return None
    return {INFO_KEYS[k]: str(v) for k, v in old.items() if k in INFO_KEYS}


def describe(info):
    if not info:
        return "unknown version"
    return (f"{info.get('version', '?')} "
            f"(built {info.get('build_date', '?')}, "
            f"hardware {info.get('machine', '?')})")


def fetch_status(host, port, timeout, cookie):
    """Return the port list from /status.json."""
    status, _, _, body = http_request(
        host, port, timeout, "GET", "/status.json", cookie=cookie)
    if status != 200:
        raise Failure(f"/status.json returned HTTP {status}", 4)
    try:
        ports = json.loads(body.decode("utf-8", "replace"))
    except ValueError:
        raise Failure("/status.json is not valid JSON", 4)
    if not isinstance(ports, list):
        raise Failure("/status.json did not return a port list", 4)
    return ports


def print_health(host, port, timeout, password, cookie,
                 label="Post-update health"):
    """Print the port health from /status.json (best effort).

    Returns True when the summary was printed, False when the read failed.
    """
    if cookie is None:
        try:
            cookie = login(host, port, timeout, password)
        except Failure as error:
            print(f"warning: cannot read health data: {error}", file=sys.stderr)
            return False
    try:
        ports = fetch_status(host, port, timeout, cookie)
    except Failure as error:
        print(f"warning: cannot read health data: {error}", file=sys.stderr)
        return False

    def number(entry, key, base=10):
        try:
            return int(str(entry.get(key, "0")), base)
        except (TypeError, ValueError):
            return 0

    rows = []
    up = tx_errors = rx_errors = learned = 0
    for entry in ports:
        link = number(entry, "link")
        speed = (LINK_SPEEDS[link] if 0 <= link < len(LINK_SPEEDS)
                 else f"link {link}")
        if link:
            up += 1
        enabled = entry.get("enabled")
        enabled = ("?" if enabled is None
                   else ("yes" if number(entry, "enabled") else "no"))
        tx_good = number(entry, "txG", 16)
        tx_bad = number(entry, "txB", 16)
        rx_good = number(entry, "rxG", 16)
        rx_bad = number(entry, "rxB", 16)
        macs = number(entry, "macs", 16)
        tx_errors += tx_bad
        rx_errors += rx_bad
        learned += macs
        rows.append((entry.get("portNum", "?"), entry.get("name", ""), speed,
                     enabled, tx_good, tx_bad, rx_good, rx_bad, macs))

    print()
    print(f"{label} (/status.json): {len(ports)} ports, {up} up, "
          f"{learned} MACs learned, TX bad pkts {tx_errors}, "
          f"RX bad pkts {rx_errors}")
    print(f"{'Port':>4} {'Name':<12} {'Link':>6} {'En':>3} "
          f"{'TX pkts':>10} {'TX bad':>7} {'RX pkts':>10} {'RX bad':>7} "
          f"{'MACs':>6}")
    for port_num, name, speed, enabled, tx_good, tx_bad, rx_good, rx_bad, macs in rows:
        print(f"{str(port_num):>4} {str(name)[:12]:<12} {speed:>6} {enabled:>3} "
              f"{tx_good:>10} {tx_bad:>7} {rx_good:>10} {rx_bad:>7} "
              f"{macs:>6}")
    return True


def image_matches_pcb(image, pcb):
    """True if the image carries the switch's NUL-terminated PCB name.

    Firmware older than pcb_ver reports only its machine name as hw_ver, so
    the caller passes that instead and the check still runs.
    """
    if not pcb:
        return True
    return pcb.encode("ascii", "replace") + b"\0" in image


def build_multipart(image, path):
    boundary = "----RTLPlayground" + secrets.token_hex(12)
    marker = b"\r\n--" + boundary.encode("ascii")
    while marker in image:
        boundary = "----RTLPlayground" + secrets.token_hex(12)
        marker = b"\r\n--" + boundary.encode("ascii")

    filename = os.path.basename(path) or "firmware.bin"
    filename = (filename.replace('"', "_").replace("\r", "_")
                .replace("\n", "_"))
    filename = filename.encode("ascii", "replace").decode()
    head = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="uploadedfile"; '
        f'filename="{filename}"\r\n'
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
    ).encode("ascii")
    return boundary, head + image + f"\r\n--{boundary}--\r\n".encode("ascii")


def upload(host, port, timeout, cookie, image, path):
    """Send the image; return (accepted, verdict).

    accepted is None when the switch closed the connection without a verdict
    (older firmware did that).  The caller then falls back to watching the
    reboot.
    """
    boundary, body = build_multipart(image, path)
    connection = http.client.HTTPConnection(host, port, timeout=timeout)

    print(f"Uploading {len(image)} bytes to http://{host}:{port}/upload ...",
          file=sys.stderr)
    try:
        connection.putrequest("POST", "/upload")
        connection.putheader(
            "Content-Type", f"multipart/form-data; boundary={boundary}")
        connection.putheader("Content-Length", str(len(body)))
        connection.putheader("Cookie", f"session={cookie}")
        connection.putheader("Connection", "close")
        connection.endheaders()

        sent = 0
        last_percent = -1
        for offset in range(0, len(body), 8192):
            chunk = body[offset:offset + 8192]
            connection.send(chunk)
            sent += len(chunk)
            if sys.stderr.isatty():
                percent = sent * 100 // len(body)
                if percent != last_percent:
                    last_percent = percent
                    print(f"\rUploading {percent:3d}% "
                          f"({sent}/{len(body)} bytes)", end="",
                          file=sys.stderr)
        if sys.stderr.isatty():
            print(file=sys.stderr)

        try:
            response = connection.getresponse()
        except (http.client.HTTPException, ConnectionResetError,
                BrokenPipeError):
            return None, "connection closed without a verdict"

        verdict = response.read().decode("utf-8", "replace").strip()
        if response.status == 200:
            return True, verdict
        if response.status == 400:
            return False, verdict or "HTTP 400 Bad Request"
        if response.status == 401:
            raise Failure("the switch rejected the session cookie", 2)
        raise Failure(
            f"upload returned HTTP {response.status}"
            + (f": {verdict}" if verdict else ""), 3)
    except (BrokenPipeError, ConnectionResetError) as error:
        raise Failure(f"connection lost during upload: {error}", 4)
    except OSError as error:
        raise Failure(f"upload failed: {error}", 4)
    finally:
        connection.close()


def probe(host, port, timeout):
    """Return the HTTP status, or None when the switch is not answering."""
    try:
        status, _, _, _ = http_request(
            host, port, timeout, "GET", "/login.html")
        return status
    except Failure:
        return None


SETTLE_S = 9		# answering this long without going down: back (or never left)


def wait_for_switch(host, port, timeout, wait_timeout, password, before,
                    require_down=False, also=(), passwords=(), mac=None):
    """Wait until the switch has rebooted and answers again.

    require_down is set when the switch closed the upload connection without
    a verdict: then only an observed reboot proves that the image was taken.
    also: more (host, port) the switch may come back at (a new firmware on
    its factory default), asked in turn; passwords: more to try there.
    mac: the old switch's; an answer whose state.sys.mac differs is another
    switch, skipped. Returns (info, cookie, (host, port)) from the fresh
    login after the reboot; info and cookie are None when that login or
    the state.sys read failed (only without mac).
    """
    start = time.monotonic()
    seen_down = False
    others = set()
    targets = [(host, port)] + [t for t in also if t != (host, port)]
    print("Waiting for the switch to reboot ...", file=sys.stderr)
    while time.monotonic() - start < wait_timeout:
        up = []
        for at in targets:
            status = probe(at[0], at[1], min(timeout, 5))
            if status is not None:
                up.append((at, status))
        elapsed = time.monotonic() - start
        if not any(at == targets[0] for at, _ in up) and not seen_down:
            print("Switch is down (rebooting) ...", file=sys.stderr)
            seen_down = True
        for at, status in up:
            if (at == targets[0] and status in (200, 401) and not seen_down
                    and elapsed >= SETTLE_S and require_down):
                raise Failure(
                    "switch never rebooted and gave no verdict; the image was "
                    "probably rejected", 3)
            # Stay reachable for a few seconds before believing the boot is
            # complete; otherwise a fast reply may just be the old session.
            # Not where it was: it has rebooted, if it is the same switch.
            if status not in (200, 401) or not (seen_down or elapsed >= SETTLE_S or at != targets[0]):
                continue
            after = None
            cookie = None
            for pw in [password] + [p for p in passwords if p != password]:
                try:
                    cookie = login(at[0], at[1], timeout, pw)
                    after = fetch_info(at[0], at[1], timeout, cookie)
                    break
                except Failure:
                    cookie = None
            if mac:
                got = (after or {}).get("mac", "").lower()
                if not (after or {}).get("om") or got != mac:
                    if got and got != mac and at not in others:
                        print(f"Skipping {at[0]}:{at[1]}: another switch (MAC {got}, "
                              f"not {mac})", file=sys.stderr)
                        others.add(at)
                    continue
            print(f"Switch is back at {at[0]}:{at[1]}.", file=sys.stderr)
            if after:
                print(f"Now running {describe(after)}", file=sys.stderr)
                if before and (after.get("version") == before.get("version")
                               and after.get("build_date")
                               == before.get("build_date")):
                    print("Note: the switch reports the same version; this "
                          "may be the image that was already installed.",
                          file=sys.stderr)
            return after, cookie, at
        time.sleep(2)
    if others:
        raise Failure(f"only another switch answered ({', '.join(f'{h}:{p}' for h, p in others)}); "
                      f"the one with MAC {mac} didn't within {wait_timeout} seconds", 5)
    raise Failure(f"switch did not answer within {wait_timeout} seconds", 4)


# ---- the config across the update (DESIGN.md 15.2) ---------------------------

def cli(host, port, timeout, cookie, lines):
    """POST /cli: the reply's text."""
    status, _, _, body = http_request(
        host, port, timeout, "POST", "/cli", body="\n".join(lines).encode(),
        cookie=cookie, content_type="text/plain")
    if status != 200:
        raise Failure(f"POST /cli returned HTTP {status}", 4)
    return body.decode("utf-8", "replace")


def machine_of(args):
    """The board for the port numbers: --machine, else the machine the
    image's file name carries (the build names it rtlplayground-*-<M>-*)."""
    if args.machine:
        return args.machine
    name = os.path.basename(args.image or "")
    ms = [m[:-2] for m in os.listdir(os.path.join(HERE, "..", "machine"))
          if m.endswith(".c") and not m.endswith("_init.c") and "-" + m[:-2] + "-" in "-" + name]
    if len(ms) != 1:
        raise Failure(f"{name}: can't tell the board from the image's name; "
                      "give --machine", 1)
    return ms[0]


class Plan:
    """An old config, converted: lines (path lines), legacy ((line, node)),
    bad, warn; the new firmware's factory default address and password."""

    def __init__(self, args, text, machine=None, lines=None, running=None, mac=None):
        sys.path.insert(0, HERE)
        import legacy_map
        import rtlpg_client
        self.rc = rtlpg_client
        self.machine = machine or machine_of(args)
        self.sch = rtlpg_client.Schema(args.schema, rtlpg_client.Board(self.machine))
        self.warn, self.filled = [], []
        self.mac = (mac or "").lower() or None	# the old switch's: the identity checks
        # the factory default (config.txt, DESIGN.md 15.1): what the new
        # firmware boots on, and where it answers then
        cfg, factory, self.factory = self.rc.Config(self.sch), [], []
        try:
            for ln in open(os.path.join(HERE, "..", "config.txt")):
                cfg.line(ln)
                w = ln.split()
                if w and not w[0].startswith("#"):
                    factory.append(w[1] if w[0] == "set" else w[0])
                    self.factory.append(" ".join(w[1:] if w[0] == "set" else w))
        except (OSError, self.rc.Bad) as error:
            print(f"warning: config.txt: {error}", file=sys.stderr)
        self.boot_ips = list(dict.fromkeys([self.value(cfg, "mgmt.ip"),
                                            self.value(self.rc.Config(self.sch), "mgmt.ip")]))
        self.boot_password = self.value(cfg, "web.password")
        if lines is not None:		# a written plan (--plan)
            self.lines, self.legacy, self.bad, self.vlans = lines, [], [], []
            self.empty = not lines
            return
        self.vlans = []		# VLAN problems the new firmware would refuse
        self.lines, self.legacy, self.bad = legacy_map.convert(
            text, self.sch.board, self.warn, self.sch, self.vlans, getattr(args, "fit_vlans", False))
        self.empty = not (self.lines or self.legacy or self.bad)
        # a field the factory default sets and the old config didn't (no ip
        # line: the old switch ran its built-in address) is pinned to what
        # the old switch ran (its /information.json), else the built-in: the
        # switch stays where it was, and get -s is config.txt + the plan
        have = {ln.split()[0] for ln in self.lines}
        built = self.rc.Config(self.sch)
        dhcp = "mgmt.mode dhcp" in self.lines	# the lease decides: not pinned
        for path in dict.fromkeys(factory):
            if dhcp and path in ("mgmt.ip", "mgmt.netmask", "mgmt.gateway"):
                continue
            if path not in have and not self.empty:
                v = (running or {}).get(path) or self.value(built, path)
                try:
                    self.rc.Config(self.sch).line(f"{path} {v}")
                except self.rc.Bad:
                    v = self.value(built, path)
                self.filled.append(f"{path} {v}")
        self.lines += self.filled

    def secret(self, path):
        try:
            o, ix, f = self.sch.resolve(path)
        except self.rc.Bad:
            return False
        return f is not None and f.typ.code == "SECRET"

    def scrub(self, text):
        """text with every Secret's value as *: for anything printed."""
        for ln in self.lines:
            path, _, v = ln.partition(" ")
            if v and self.secret(path):
                text = text.replace(v, "*")
        return text

    def masked(self, line):
        """A line as shown or written: a Secret's value as *."""
        path = line.split()[0]
        return f"{path} *" if self.secret(path) and " " in line else line

    def write(self, path, host, port):
        """The plan for --plan: the path lines, a Secret as * (the -p
        password is put back), the old lines it drops as comments."""
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(f"# fwupdate plan: {host}:{port}, machine {self.machine}, mac {self.mac}\n")
            for line, node in self.legacy:
                f.write(f"# dropped, legacy ({node}): {line}\n")
            for line in self.bad:
                f.write(f"# dropped, untranslated: {line}\n")
            for line in self.lines:
                f.write(self.masked(line) + "\n")

    @classmethod
    def read(cls, args, path, password):
        """A written plan; web.password * is the -p password (the old
        switch's, the one it is logged in with)."""
        try:
            text = open(path).read()
        except OSError as error:
            raise Failure(f"cannot read {path}: {error}", 1)
        m = re.match(r"# fwupdate plan: \S+, machine (\S+), mac ([0-9a-f:]{17})\n", text)
        if not m:
            raise Failure(f"{path}: not a plan fwupdate wrote (or without the switch's MAC)", 1)
        lines = [ln.strip() for ln in text.splitlines() if ln.strip() and not ln.startswith("#")]
        lines = [f"web.password {password}" if ln == "web.password *" else ln for ln in lines]
        try:
            plan = cls(args, None, args.machine or m.group(1), lines, mac=m.group(2))
        except OSError as error:
            raise Failure(f"the schema or the board: {error}", 1)
        for ln in lines:
            if ln.endswith(" *") and plan.secret(ln.split()[0]):
                raise Failure(f"{path}: {ln.split()[0]} is masked; set it by hand", 1)
        return plan

    def value(self, cfg, path):
        o, ix, f = self.sch.resolve(path)
        return self.sch.fmt(o, f, cfg.v[(o.regname, ix)][f.name])

    def mgmt_ip(self):
        """The address the lines give the switch, or None (DHCP)."""
        v = dict(ln.split(" ", 1) for ln in self.lines if " " in ln and not ln.startswith("del "))
        return None if v.get("mgmt.mode") == "dhcp" else v.get("mgmt.ip")

    def saved(self):
        """What get -s will print once the lines are saved: the factory
        default (what the new firmware booted on) and the lines over it."""
        return self.rc.bin2txt(self.sch, self.rc.txt2bin(self.sch, self.factory + self.lines), secrets=False)

    def lost(self):
        return len(self.legacy) + len(self.bad)

    def show(self):
        print(f"Config: {len(self.lines)} path lines, {len(self.legacy)} "
              f"legacy, {len(self.bad)} untranslated, {len(self.vlans)} vlan "
              f"problems, {len(self.warn)} warnings")
        for line in self.lines:
            line = self.masked(line)
            note = ("  (the factory default sets it, the old config didn't: as the old switch ran)"
                    if line in self.filled else "")
            print(f"  set {line}{note}" if not line.startswith("del ") else f"  {line}")
        for line, node in self.legacy:
            print(f"  legacy ({node}): {line}")
        for line in self.bad:
            print(f"  untranslated: {line}")
        for line in self.vlans:
            print(f"  vlan problem: {line}")
        for line in self.warn:
            print(f"  warning: {line}")
        print("After the reboot: the factory default config, at "
              f"{' or '.join(self.boot_ips)} until the lines are applied",
              flush=True)


def read_plan(args, host, port, cookie, before=None):
    """The plan; and why the config isn't readable, or None."""
    why, body = None, b""
    try:
        # no Content-Length (the old reply ends at its close): read it
        # twice, so a transfer cut short doesn't convert as a shorter config
        for n in range(2):
            for attempt in range(3):	# a connection lost: ask again
                try:
                    status, _, _, got = http_request(
                        host, port, args.timeout, "GET", "/config", cookie=cookie)
                    break
                except Failure:
                    if attempt == 2:
                        raise
                    time.sleep(1)
            if status != 200:
                why, body = f"GET /config returned HTTP {status}", b""
                break
            if n and got != body:
                why, body = "GET /config read twice differs (a cut transfer?)", b""
                break
            body = got
    except Failure as error:
        why, body = str(error), b""
    try:
        plan = Plan(args, body, running=before, mac=(before or {}).get("mac"))
    except OSError as error:
        raise Failure(f"the schema or the board: {error}", 1)
    if why is None and plan.empty:
        why = "the saved config is blank"
    return plan, why


def state(args, host, port, cookie, what, busy):
    """state.cfg.<what> once it is no longer one of busy."""
    start = time.monotonic()
    while True:
        out = cli(host, port, args.timeout, cookie, [f"get state.cfg.{what}"])
        m = re.search(rf"state\.cfg\.{what} (\S+)", out)
        st = m.group(1) if m else out.strip()
        if st not in busy or time.monotonic() - start > args.timeout:
            return st
        time.sleep(0.5)


CONFIRM_S = 120		# apply's confirm window: a wrong address rolls back


ARP = "/proc/net/arp"


def arp_ip(mac):
    """The address this host's neighbour table has for mac (Linux), or
    None: where a switch in DHCP answers now, if it is on our segment."""
    try:
        for row in open(ARP).read().splitlines()[1:]:
            w = row.split()
            if len(w) >= 4 and w[3].lower() == (mac or "").lower() and w[2] != "0x0":
                return w[0]
    except OSError:
        pass
    return None


def same_switch(args, plan, at, cookie):
    """at runs the object model and its state.sys.mac is the old switch's;
    else says so."""
    info = fetch_info(at[0], at[1], args.timeout, cookie) or {}
    got = info.get("mac", "").lower()
    if info.get("om") and got == plan.mac:
        return True
    print(f"Skipping {at[0]}:{at[1]}: " + (f"another switch (MAC {got}, not {plan.mac})" if got
                                          else "not the object model, or no MAC"), file=sys.stderr)
    return False


def carry_over(args, plan, host, port, password, at, cookie):
    """Step 4: set the plan's lines on the new firmware at `at`, apply them
    with a confirm window, find the switch at its address (HOST, or the
    plan's mgmt.ip, or still `at`) with its password, confirm, save, and
    compare get -s with the plan. Not reached in the window, the switch
    rolls back to its factory default."""
    if cookie is None:
        raise Failure(f"can't log in at {at[0]}:{at[1]} after the update", 5)
    if not same_switch(args, plan, at, cookie):
        raise Failure(f"{at[0]}:{at[1]} isn't the switch with MAC {plan.mac}", 5)
    out = cli(at[0], at[1], args.timeout, cookie,
              [f"set {ln}" if not ln.startswith("del ") else ln for ln in plan.lines])
    errs = [ln for ln in out.splitlines() if ln.startswith("err")]
    if errs:
        cli(at[0], at[1], args.timeout, cookie, ["discard"])
        raise Failure(plan.scrub("the switch refused lines: " + "; ".join(errs[:5])), 5)
    print(f"Set {len(plan.lines)} lines; apply {CONFIRM_S} ...", file=sys.stderr)
    applied = time.monotonic()
    deadline = applied + CONFIRM_S - max(5, CONFIRM_S // 10)	# before the rollback
    try:
        out = cli(at[0], at[1], min(args.timeout, 10), cookie, [f"apply {CONFIRM_S}"])
        if "err" in out:
            raise Failure(plan.scrub(f"apply: {out.strip()}"), 5)
    except Failure as error:
        if error.status == 5:
            raise
        pass		# the address moved under the reply
    targets = [(host, port)]
    ip = plan.mgmt_ip()
    if ip:
        targets.append((ip, port))
    targets.append(at)
    targets = list(dict.fromkeys(targets))
    cookie = None
    lport = port
    while cookie is None:
        lease = None if ip else arp_ip(plan.mac)	# DHCP: by the old MAC
        for host, port in targets + ([(lease, lport)] if lease and (lease, lport) not in targets else []):
            for pw in dict.fromkeys([password, plan.boot_password]):
                if time.monotonic() > deadline:
                    break
                try:
                    cookie = login(host, port, min(args.timeout, 5), pw)
                except Failure:
                    continue
                if same_switch(args, plan, (host, port), cookie):
                    break
                cookie = None
                break		# another switch: not this address
            if cookie:
                break
        if cookie is None:
            if time.monotonic() > deadline:
                left = max(0, int(applied + CONFIRM_S - time.monotonic()))
                raise Failure("the switch didn't answer at "
                              + ", ".join(f"{h}:{p}" for h, p in targets)
                              + (" (or a DHCP lease this host saw)" if not ip else "")
                              + f" within the confirm window: in {left} s it rolls "
                              "back to its factory default", 5)
            time.sleep(2)
    print(f"Logged in to {host}:{port} after the apply; confirming",
          file=sys.stderr)
    if not same_switch(args, plan, (host, port), cookie):
        raise Failure(f"{host}:{port} isn't the switch with MAC {plan.mac}; not confirmed", 5)
    out = cli(host, port, args.timeout, cookie, ["confirm"])
    if not out.rstrip().endswith("ok"):
        raise Failure(plan.scrub(f"confirm: {out.strip()}"), 5)
    st = state(args, host, port, cookie, "apply", ("running", "undo", "confirm"))
    if st not in ("idle", "done"):
        raise Failure(f"apply: state.cfg.apply {st}", 5)
    out = cli(host, port, args.timeout, cookie, ["save"])
    st = state(args, host, port, cookie, "save", ("erasing", "writing"))
    if not out.rstrip().endswith("ok") or st != "done":
        raise Failure(plan.scrub(f"save: {out.strip()} (state.cfg.save {st})"), 5)
    plan.saved_ok = True
    got = [ln for ln in cli(host, port, args.timeout, cookie, ["get -s"]).splitlines()
           if ln and ln != "ok"]
    want = plan.saved()
    if got != want:
        diff = [f"-{ln}" for ln in want if ln not in got] + [f"+{ln}" for ln in got if ln not in want]
        raise Failure("get -s differs from the plan: " + " ".join(diff[:10]), 5)
    print(f"Saved: get -s is the plan ({len(got)} lines)", file=sys.stderr)


def step4(args, plan, plan_file, host, port, password, at, cookie):
    """carry_over(), and on a failure what state the switch is in and how
    to redo it."""
    plan.saved_ok = False
    try:
        carry_over(args, plan, host, port, password, at, cookie)
    except Failure as error:
        where = " or ".join(plan.boot_ips)
        state_ = ("the config was saved, but get -s doesn't match the plan" if plan.saved_ok else
                  f"nothing is saved: the switch runs (or comes back to) its factory default, at {where}")
        raise Failure(f"{error}\nStep 4 didn't finish: {state_}. Redo it with "
                      f"`fwupdate.py -p ... --plan {plan_file} {host}`", error.status)


def plan_only(args):
    """Step 4 alone, from a written plan, on a switch that runs the new
    firmware: at HOST, else where it boots (--boot-host or the plan's)."""
    if args.host is not None:
        raise Failure("--plan takes only the host; do not pass an image", 1)
    host, port = parse_target(args.image or DEFAULT_HOST, args.port)
    password = resolve_password(args, host)
    plan = Plan.read(args, args.plan, password)
    print(f"{args.plan}: {len(plan.lines)} path lines", file=sys.stderr)
    targets = [(host, port)] + ([parse_target(args.boot_host, args.port)] if args.boot_host
                                else [(ip, 80) for ip in plan.boot_ips])
    for at in dict.fromkeys(targets):
        for pw in dict.fromkeys([password, plan.boot_password]):
            try:
                cookie = login(at[0], at[1], args.timeout, pw)
            except Failure:
                continue
            info = fetch_info(at[0], at[1], args.timeout, cookie) or {}
            if info.get("mac", "").lower() != plan.mac:
                print(f"Skipping {at[0]}:{at[1]}: another switch (MAC "
                      f"{info.get('mac', '?')}, not {plan.mac})", file=sys.stderr)
                break
            if not info.get("om"):
                raise Failure(f"{at[0]}:{at[1]} doesn't run the object model; "
                              "update it first", 5)
            print(f"Logged in to {at[0]}:{at[1]}; step 4/4: setting, "
                  "applying and saving the config", file=sys.stderr)
            step4(args, plan, args.plan, host, port, password, at, cookie)
            print("Config carried over.")
            return 0
    raise Failure(f"the switch with MAC {plan.mac} isn't at "
                  + ", ".join(f"{h}:{p}" for h, p in dict.fromkeys(targets))
                  + " (a DHCP lease: give its address as HOST)", 5)


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Update RTLPlayground switch firmware via its web API.",
        epilog="The image is the 512 KiB RTLPlayground build "
               "(output/rtlplayground_*.bin), not the OEM upgrade image. "
               "The password can also be set in RTLP_PASSWORD.")
    parser.add_argument("image", nargs="?",
                        help="RTLPlayground firmware image (.bin); with --check, "
                             "the single positional is the host")
    parser.add_argument(
        "host", nargs="?", default=None,
        help=f"switch address, optionally host:port or http://host "
             f"(default: {DEFAULT_HOST})")
    parser.add_argument("-p", "--password",
                        help="web interface password (prompted if not given)")
    parser.add_argument("--port", type=int,
                        help="HTTP port (overrides the port in HOST)")
    parser.add_argument("--timeout", type=float, default=60,
                        help="socket timeout in seconds (default: 60)")
    parser.add_argument("--wait-timeout", type=float,
                        default=DEFAULT_WAIT_TIMEOUT,
                        help="seconds to wait for the reboot "
                             f"(default: {DEFAULT_WAIT_TIMEOUT})")
    parser.add_argument("--no-wait", action="store_true",
                        help="do not wait for the switch to reboot")
    parser.add_argument("--health", action="store_true",
                        help="after the reboot, fetch /status.json and print "
                             "a port health summary")
    parser.add_argument("--check", action="store_true",
                        help="read-only: print the version and /status.json "
                             "health, then exit without uploading; give the "
                             "host as the only positional "
                             f"(default: {DEFAULT_HOST})")
    parser.add_argument("--force", action="store_true",
                        help="upload even if the image does not name the "
                             "switch's PCB")
    parser.add_argument("--dry-run", action="store_true",
                        help="read and convert the switch's config, print "
                             "the plan, and stop before the upload")
    parser.add_argument("--drop-legacy", action="store_true",
                        help="update even if config lines would be lost "
                             "(legacy or untranslated: listed)")
    parser.add_argument("--fit-vlans", action="store_true",
                        help="drop the VLANs over the new firmware's table "
                             "(the highest VIDs) and park a pvid on a VLAN "
                             "the config lacks on 0, each with a warning")
    parser.add_argument("--boot-defaults", action="store_true",
                        help="update even if the switch has no readable "
                             "config: it boots the factory default")
    parser.add_argument("--boot-host",
                        help="where the new firmware answers after the "
                             "reboot, on its factory default (default: "
                             "HOST, then config.txt's mgmt.ip, then the "
                             "schema's)")
    parser.add_argument("--save-plan", metavar="FILE",
                        help="where to write the converted plan before the "
                             "upload (default: HOST.plan)")
    parser.add_argument("--plan", metavar="FILE",
                        help="step 4 alone (set, apply, confirm, save, "
                             "compare) from a written plan, on a switch "
                             "already updated; give the host as the only "
                             "positional")
    parser.add_argument("--machine",
                        help="the board, machine/<M>.c (default: from the "
                             "image's name)")
    parser.add_argument("--schema", action="append",
                        help="the new firmware's schema directories "
                             "(default: this tree's schema/)")
    return parser.parse_args(argv)


def resolve_password(args, host):
    password = args.password
    if password is None:
        password = os.environ.get("RTLP_PASSWORD")
    if password is None:
        password = getpass.getpass(f"Password for {host}: ")
    return password


def check_switch(args, target):
    """Read-only mode: print the running version and /status.json health."""
    host, port = parse_target(target, args.port)
    password = resolve_password(args, host)
    cookie = login(host, port, args.timeout, password)
    info = fetch_info(host, port, args.timeout, cookie)
    if not info:
        raise Failure("could not read the switch's state.sys", 4)

    name = info.get("hostname") or host
    print(f"Switch: {name} at {host}:{port}")
    print(f"Version: {describe(info)}")
    print(f"Flash: {info.get('flash.size', '?')}, "
          f"chip temperature: {info.get('temp_c', '?')}")
    return (0 if print_health(host, port, args.timeout, password, cookie,
                              "Health") else 4)


def run(args):
    if args.timeout <= 0:
        raise Failure("--timeout must be positive", 1)
    if not args.check and args.wait_timeout <= 0:
        raise Failure("--wait-timeout must be positive", 1)

    if args.check:
        if args.host is not None:
            raise Failure("--check takes only the host; do not pass an image",
                          1)
        if args.no_wait:
            raise Failure("--no-wait has no effect with --check", 1)
        target = args.image if args.image is not None else DEFAULT_HOST
        if args.image is not None and os.path.isfile(target):
            raise Failure("--check takes a host, not a firmware image", 1)
        return check_switch(args, target)

    if args.plan:
        return plan_only(args)
    if args.image is None:
        raise Failure("IMAGE is required unless --check is used", 1)
    if args.health and args.no_wait:
        raise Failure("--health needs the reboot; remove --no-wait", 1)
    image = load_image(args.image)
    print(f"{args.image}: {len(image)} bytes, magic and CRC16 0x"
          f"{IMAGE_CRC:04x} OK", file=sys.stderr)
    host, port = parse_target(args.host or DEFAULT_HOST, args.port)
    password = resolve_password(args, host)

    cookie = login(host, port, args.timeout, password)
    print(f"Logged in to {host}:{port}", file=sys.stderr)

    before = fetch_info(host, port, args.timeout, cookie)
    if before:
        print(f"Currently running {describe(before)}", file=sys.stderr)
        # Prefer the PCB name; firmware older than pcb_ver reports only
        # hw_ver (machine), and the check then runs against that rather
        # than being skipped.
        board = before.get("pcb") or before.get("machine")
        if not args.force and not image_matches_pcb(image, board):
            raise Failure(
                f"the image does not contain the switch's board name "
                f"({board}); it was probably built for another "
                "board. Use --force to upload it anyway.", 1)
    else:
        print("Could not read the switch's state.sys; skipping the PCB check",
              file=sys.stderr)

    # old firmware: its command text, converted (DESIGN.md 15.2), read
    # before the upload: the new firmware doesn't read it
    plan, back = None, []
    if not (before or {}).get("om"):
        print("Step 1/4: reading and converting the saved config",
              file=sys.stderr)
        plan, why = read_plan(args, host, port, cookie, before)
        back = plan.boot_ips
        if why:
            print(f"No readable config: {why}; the switch will boot its "
                  f"factory default, at {' or '.join(plan.boot_ips)}",
                  flush=True)
            if args.dry_run:
                return 0
            if not args.boot_defaults:
                raise Failure("no config to carry over; --boot-defaults to "
                              "update all the same", 5)
            plan = None
        else:
            plan.show()
            if args.dry_run:
                return 0
    elif args.dry_run:
        print("The switch runs the object model: its config stays as it is")
        return 0
    if plan:
        if plan.lost() and not args.drop_legacy:
            raise Failure(
                f"{plan.lost()} config lines would be lost (listed above); "
                "--drop-legacy to update all the same", 5)
        if plan.vlans:
            raise Failure(
                f"{len(plan.vlans)} VLAN problems the new firmware would refuse "
                "(listed above); --fit-vlans drops the extra VLANs and parks "
                "those pvids on 0", 5)
        if not re.fullmatch(r"[0-9a-f]{2}(:[0-9a-f]{2}){5}", plan.mac or ""):
            raise Failure("the old switch didn't say its MAC (/information.json "
                          "mac_address): no way to tell it from another switch "
                          "after the update", 5)
        if plan.lost():
            print(f"Dropping {plan.lost()} config lines (--drop-legacy)",
                  file=sys.stderr)
        if args.no_wait:
            raise Failure("the config is set after the reboot; remove "
                          "--no-wait", 1)
        plan_file = args.save_plan or f"{host}.plan"
        plan.write(plan_file, host, port)
        print(f"Plan written to {plan_file}: if step 4 fails, "
              f"`fwupdate.py -p ... --plan {plan_file} {host}` redoes it",
              file=sys.stderr)

    if plan:
        print("Step 2/4: uploading", file=sys.stderr)
    accepted, verdict = upload(
        host, port, args.timeout, cookie, image, args.image)

    if accepted is False:
        raise Failure(f"switch rejected the image: {verdict}", 3)
    if accepted is None:
        print(f"Switch: {verdict}", file=sys.stderr)
    else:
        print(f"Switch: {verdict or 'upload accepted'}", file=sys.stderr)

    if args.no_wait:
        print("Firmware staged; the switch is rebooting to apply it.")
        return 0

    also = []
    if args.boot_host:
        also.append(parse_target(args.boot_host, args.port))
    else:
        also += [(ip, 80) for ip in back]
    if plan:
        print("Step 3/4: waiting for the new firmware", file=sys.stderr)
    _, cookie, at = wait_for_switch(
        host, port, args.timeout, args.wait_timeout, password, before,
        require_down=accepted is None, also=also,
        passwords=[plan.boot_password] if plan else ["1234"] if back else [],
        mac=plan.mac if plan else None)
    if plan:
        print("Step 4/4: setting, applying and saving the config",
              file=sys.stderr)
        step4(args, plan, plan_file, host, port, password, at, cookie)
        cookie = None
    if args.health:
        print_health(host, port, args.timeout, password, cookie)
    print("Firmware update complete.")
    return 0


def main(argv=None):
    args = parse_args(argv)
    try:
        return run(args)
    except Failure as error:
        print(f"error: {error}", file=sys.stderr)
        return error.status
    except KeyboardInterrupt:
        print("\naborted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
