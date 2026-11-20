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
the new firmware doesn't read (DESIGN.md 15.2).  Updating such a switch,
the tool reads its saved config (GET /config), converts it
(tools/legacy_map.py, against the schema of this tree) and prints the
plan; --dry-run stops there.  A line it can't translate, or one of an
object not on the object model yet (legacy), would be lost: it refuses
unless --drop-legacy.  After the reboot the new firmware runs on its
built-in config, so the tool looks for it at HOST, then at the schema's
default address (--boot-host), sets the converted lines, applies them
(the switch moves back to its address), saves, and compares get -s with
the plan.

Usage:
    fwupdate.py [options] IMAGE [HOST]

    fwupdate.py -p secret output/rtlplayground_....bin 192.168.10.247
    fwupdate.py -p secret --health output/rtlplayground_....bin 192.168.10.247
    fwupdate.py -p secret --check 192.168.10.247
    fwupdate.py -p secret --dry-run output/.../rtlplayground-install-....bin 192.168.10.247
    RTLP_PASSWORD=secret fwupdate.py output/rtlplayground_....bin --no-wait

Exit status:
    0  image accepted (and, unless --no-wait, the switch came back)
    1  the local image is not a valid RTLPlayground image
    2  login failed
    3  the switch rejected the upload
    4  transfer, reboot or health read failed
    5  the config can't be carried over (refused, or a line not taken)
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
             "hostname": "hostname"}


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


def wait_for_switch(host, port, timeout, wait_timeout, password, before,
                    require_down=False, also=(), passwords=()):
    """Wait until the switch has rebooted and answers again.

    require_down is set when the switch closed the upload connection without
    a verdict: then only an observed reboot proves that the image was taken.
    also: more (host, port) the switch may come back at (a new firmware on
    its built-in config), asked in turn; passwords: more to try there.
    Returns (info, cookie, (host, port)) from the fresh login after the
    reboot; info and cookie are None when that login or the state.sys read
    failed.
    """
    start = time.monotonic()
    seen_down = False
    targets = [(host, port)] + [t for t in also if t != (host, port)]
    print("Waiting for the switch to reboot ...", file=sys.stderr)
    while time.monotonic() - start < wait_timeout:
        for at in targets:
            status = probe(at[0], at[1], min(timeout, 5))
            if status is not None:
                break
        if status is not None and at != targets[0]:
            seen_down = True	# not where it was: it has rebooted
        elapsed = time.monotonic() - start
        if status is None:
            if not seen_down:
                print("Switch is down (rebooting) ...", file=sys.stderr)
                seen_down = True
            time.sleep(2)
            continue
        if (status in (200, 401) and not seen_down and elapsed >= 9
                and require_down):
            raise Failure(
                "switch never rebooted and gave no verdict; the image was "
                "probably rejected", 3)
        # Stay reachable for a few seconds before believing the boot is
        # complete; otherwise a fast reply may just be the old session.
        if status in (200, 401) and (seen_down or elapsed >= 9):
            print(f"Switch is back at {at[0]}:{at[1]}.", file=sys.stderr)
            after = None
            cookie = None
            for pw in [password] + [p for p in passwords if p != password]:
                try:
                    cookie = login(at[0], at[1], timeout, pw)
                    after = fetch_info(at[0], at[1], timeout, cookie)
                    break
                except Failure:
                    cookie = None
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
    bad, warn; the new firmware's built-in address and password."""

    def __init__(self, args, text):
        sys.path.insert(0, HERE)
        import legacy_map
        import rtlpg_client
        self.rc = rtlpg_client
        self.sch = rtlpg_client.Schema(args.schema, rtlpg_client.Board(machine_of(args)))
        self.warn = []
        self.lines, self.legacy, self.bad = legacy_map.convert(
            text, self.sch.board, self.warn, self.sch)
        self.boot_ip = self.builtin("mgmt.ip")
        self.boot_password = self.builtin("web.password")

    def builtin(self, path):
        o, ix, f = self.sch.resolve(path)
        return self.sch.fmt(o, f, self.sch.default(o, f))

    def saved(self):
        """What get -s will print once the lines are saved."""
        return self.rc.bin2txt(self.sch, self.rc.txt2bin(self.sch, self.lines), secrets=False)

    def lost(self):
        return len(self.legacy) + len(self.bad)

    def show(self):
        print(f"Config: {len(self.lines)} path lines, {len(self.legacy)} "
              f"legacy, {len(self.bad)} untranslated, {len(self.warn)} "
              "warnings")
        for line in self.lines:
            print(f"  set {line}" if not line.startswith("del ") else f"  {line}")
        for line, node in self.legacy:
            print(f"  legacy ({node}): {line}")
        for line in self.bad:
            print(f"  untranslated: {line}")
        for line in self.warn:
            print(f"  warning: {line}")
        print(f"After the reboot: the built-in config, at {self.boot_ip} "
              "until the lines are applied", flush=True)


def read_plan(args, host, port, cookie):
    status, _, _, body = http_request(
        host, port, args.timeout, "GET", "/config", cookie=cookie)
    if status != 200:
        raise Failure(f"GET /config returned HTTP {status}", 4)
    try:
        return Plan(args, body)
    except OSError as error:
        raise Failure(f"the schema or the board: {error}", 1)


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


def carry_over(args, plan, host, port, password, at, cookie):
    """Set the plan's lines on the new firmware at `at`, apply them (the
    switch may move to host:port, logging the session out), save (the old
    text goes), and compare get -s with the plan."""
    if cookie is None:
        raise Failure(f"can't log in at {at[0]}:{at[1]} after the update", 5)
    if plan.lines:
        out = cli(at[0], at[1], args.timeout, cookie,
                  [f"set {ln}" if not ln.startswith("del ") else ln for ln in plan.lines])
        errs = [ln for ln in out.splitlines() if ln.startswith("err")]
        if errs:
            cli(at[0], at[1], args.timeout, cookie, ["discard"])
            raise Failure("the switch refused lines: " + "; ".join(errs[:5]), 5)
        print(f"Set {len(plan.lines)} lines; applying ...", file=sys.stderr)
        try:
            cli(at[0], at[1], min(args.timeout, 10), cookie, ["apply"])
        except Failure:
            pass		# the address moved under the reply
        # back at its old address (or still where it is), with its password
        cookie = None
        start = time.monotonic()
        targets = list(dict.fromkeys([(host, port), at]))
        while cookie is None:
            for host, port in targets:
                for pw in dict.fromkeys([password, plan.boot_password]):
                    try:
                        cookie = login(host, port, min(args.timeout, 5), pw)
                        break
                    except Failure:
                        pass
                if cookie:
                    break
            if cookie is None:
                if time.monotonic() - start > args.wait_timeout:
                    raise Failure("the switch didn't answer after the apply", 5)
                time.sleep(2)
        print(f"Logged in to {host}:{port} after the apply", file=sys.stderr)
    else:
        host, port = at
    st = state(args, host, port, cookie, "apply", ("running", "undo"))
    if st not in ("idle", "done"):
        raise Failure(f"apply: state.cfg.apply {st}", 5)
    out = cli(host, port, args.timeout, cookie, ["save"])
    st = state(args, host, port, cookie, "save", ("erasing", "writing"))
    if not out.rstrip().endswith("ok") or st != "done":
        raise Failure(f"save: {out.strip()} (state.cfg.save {st})", 5)
    got = [ln for ln in cli(host, port, args.timeout, cookie, ["get -s"]).splitlines()
           if ln and ln != "ok"]
    want = plan.saved()
    if got != want:
        diff = [f"-{ln}" for ln in want if ln not in got] + [f"+{ln}" for ln in got if ln not in want]
        raise Failure("get -s differs from the plan: " + " ".join(diff[:10]), 5)
    print(f"Saved: get -s is the plan ({len(got)} lines)", file=sys.stderr)


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
    parser.add_argument("--boot-host",
                        help="where the new firmware answers after the "
                             "reboot, on its built-in config (default: "
                             "HOST, then the schema's mgmt.ip)")
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

    # old firmware: its command text, converted (DESIGN.md 15.2)
    plan = None
    if not (before or {}).get("om"):
        plan = read_plan(args, host, port, cookie)
        plan.show()
        if args.dry_run:
            return 0
        if plan.lost() and not args.drop_legacy:
            raise Failure(
                f"{plan.lost()} config lines would be lost (listed above); "
                "--drop-legacy to update all the same", 5)
        if plan.lost():
            print(f"Dropping {plan.lost()} config lines (--drop-legacy)",
                  file=sys.stderr)
        if args.no_wait:
            raise Failure("the config is set after the reboot; remove "
                          "--no-wait", 1)
    elif args.dry_run:
        print("The switch runs the object model: its config stays as it is")
        return 0

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
    elif plan:
        also.append((plan.boot_ip, 80))
    _, cookie, at = wait_for_switch(
        host, port, args.timeout, args.wait_timeout, password, before,
        require_down=accepted is None, also=also,
        passwords=[plan.boot_password] if plan else [])
    if plan:
        carry_over(args, plan, host, port, password, at, cookie)
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
