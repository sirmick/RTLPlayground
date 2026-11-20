#!/usr/bin/env python3
"""rtlpg_client.py - the object model from the host: the binary saved config
(DESIGN.md 15.3) both ways, old command text converted to paths, and a
switch's /cli. Python's standard library only.

  rtlpg_client.py txt2bin [--machine M] in.txt out.bin   path lines -> records
  rtlpg_client.py bin2txt [--machine M] in.bin           records -> path lines
  rtlpg_client.py convert old.txt                        old commands -> path lines
  rtlpg_client.py default [--machine M] in.txt out       the build's default config sector
  rtlpg_client.py --host H [--password P] cli LINE...    POST /cli, print the reply
  rtlpg_client.py --host H [--password P] get-config     GET /config (get -s)

The codec reads the schema with tools/om_gen.py (its IDs from the registry)
and the board from machine/<M>.c (port numbers, SFP ports): a port mask is
logical ports inside, front-panel numbers outside. The default board is the
8x: nine ports, the ninth an SFP, RTL8373.

txt2bin writes what the firmware's save writes for the same config: the
fields off their default, the instances that have one, a keyed object as a
reset and then its entries in key order. Lines are "path value", with or
without "set "; "del <entry>" removes a keyed entry (a default one too).
bin2txt prints a line per record a load takes, the Secrets too
(--no-secrets leaves them out, as get -s and GET /config do); what a load
would skip goes to stderr.
"""
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import om_gen  # noqa: E402

MAGIC = b"\0RC1"
HDR = 8
CONFIG_LEN = 0x1000
CPU = 9                 # CPU_LOGICAL_PORT


def crc16(data, c=0):
    """CRC-16/0xA001 from 0: crc16_bank1's (the mdata convention)."""
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c


class Board:
    """A machine's ports: log_to_phys, phys_to_log, is_sfp, chip, n_10g."""

    def __init__(self, machine=None):
        self.name = machine or "8x"
        self.l2p = list(range(1, 10))
        self.p2l = list(range(9))
        self.sfp = [0] * 8 + [1]
        self.rtl8373 = True
        self.n_10g = 0
        self.min_port, self.max_port = 0, 8
        if machine:
            self._read(machine)

    def _read(self, m):
        c = open(os.path.join(ROOT, "machine", m + ".c")).read()
        h = open(os.path.join(ROOT, "machine", m + ".h")).read()
        arr = lambda n: [int(x, 0) for x in re.search(r"\.%s\s*=\s*\{([^}]*)\}" % n, c)[1].split(",") if x.strip()]
        self.l2p, self.p2l = arr("log_to_phys_port"), arr("phys_to_log_port")
        self.sfp = arr("is_sfp")
        t = re.search(r"\.n_10g\s*=\s*(\d+)", c)
        self.n_10g = int(t[1]) if t else 0
        chip = re.search(r"#define\s+MACHINE_CHIPSET\s+(\w+)", h)[1]
        self.rtl8373 = chip == "RTL8373"
        self.min_port = 3 if chip == "RTL8372" else 0
        self.max_port = 7 if chip == "RTL8370" else 8
        for n, attr in (("MACHINE_MIN_PORT", "min_port"), ("MACHINE_MAX_PORT", "max_port")):
            t = re.search(r"#define\s+%s\s+(\d+)" % n, h)
            if t:
                setattr(self, attr, int(t[1]))

    def valid(self, lp):
        return self.min_port <= lp <= self.max_port

    def log_of(self, n):
        """A front-panel number's logical port, or None (om.c log_of)."""
        if not 1 <= n <= 9:
            return None
        lp = self.p2l[n - 1]
        return lp if self.valid(lp) else None

    def fronts(self):
        return [n for n in range(1, 10) if self.log_of(n) is not None]

    def when(self, w, lp):
        if w == 1:
            return not self.sfp[lp]
        if w == 2:
            return bool(self.sfp[lp])
        if w == 3:
            return (lp == 3 and self.n_10g) or (lp == 8 and self.n_10g == 2)
        return True

    def isolate_default(self):
        """OM_ISOLATE_DEFAULT: every user port and the CPU."""
        return (1 << CPU) | (((1 << (self.max_port + 1)) - 1) & ~((1 << self.min_port) - 1))


class Bad(Exception):
    pass


class Schema:
    """The config objects, in om_obj[] order, with their IDs."""

    def __init__(self, dirs=None, board=None):
        dirs = dirs or [os.path.join(ROOT, "schema")]
        om_gen.SCHEMA.update({"objs": [], "enums": {}, "msgs": {}, "cur": None, "dir": None})
        objs = om_gen.load_schema(dirs, js=True)	# a state Enum's words come with the build (state.sys.reset.slot)
        regs = {d: om_gen.read_registry(os.path.join(d, "registry.txt")) for d in dirs}
        om_gen.apply_registry(objs, regs, False)
        self.objs = [o for o in objs if not o.state]
        self.board = board or Board()
        for o in self.objs:
            o.cfields = [f for f in o.fields if f.access == "cfg"]

    # ---- the parts of a path

    def kinds(self, o):
        return [lv[0] for lv in o.levels]

    def ix_text(self, o, lv, v):
        k, n, a, _ = o.levels[lv]
        if k == "NUM":
            return str(v + 1)
        if k == "WORD":
            return a.words[v]
        return str(v)

    def path(self, o, ix, f):
        parts, lv = [], 0
        for c in o.pattern.split("."):
            if c == "#":
                parts.append(self.ix_text(o, lv, ix[lv]))
                lv += 1
            else:
                parts.append(c)
        return ".".join(parts) + "." + f.name

    def resolve(self, path):
        """path -> (obj, index tuple, field or None for the instance)."""
        for o in self.objs:
            rx, lv = [], 0
            for c in o.pattern.split("."):
                if c == "#":
                    rx.append(r"([^.]+)")
                    lv += 1
                else:
                    rx.append(re.escape(c))
            m = re.fullmatch(r"\.".join(rx) + r"(?:\.(.+))?", path)
            if not m:
                continue
            ix = []
            for i in range(lv):
                v = self.ix_parse(o, i, m[i + 1])
                if v is None:
                    raise Bad("%s: no such %s" % (path, "port" if o.levels[i][0] == "PORT" else "index"))
                ix.append(v)
            rest = m[lv + 1]
            if rest is None:
                return o, tuple(ix), None
            for f in o.cfields:
                if f.name == rest:
                    return o, tuple(ix), f
        raise Bad("%s: no such path" % path)

    def ix_parse(self, o, lv, s):
        k, n, a, _ = o.levels[lv]
        if k == "WORD":
            return a.words.index(s) if s in a.words else None
        if not s.isdigit():
            return None
        v = int(s)
        if k == "PORT":
            return v if self.board.log_of(v) is not None else None
        if k == "NUM":
            return v - 1 if 1 <= v <= n else None
        return v if a.lo <= v <= a.hi else None

    # ---- the record index (om.c enc_pass, d_obj)

    def rec_ix(self, o, ix):
        if not ix:
            return 0
        k, n = o.levels[0][0], o.levels[0][1]
        v = ix[0] + (1 if k == "NUM" and n > 1 else 0)
        if len(ix) == 2:
            v = v << 8 | ix[1]
        return v

    def from_rec_ix(self, o, v):
        if not o.levels:
            return () if v == 0 else None
        k, n = o.levels[0][0], o.levels[0][1]
        ix = [v >> 8, v & 0xff] if len(o.levels) == 2 else [v]
        if k == "NUM" and n > 1:
            ix[0] -= 1
        for lv, x in enumerate(ix):
            kk, nn, a, _ = o.levels[lv]
            if kk == "PORT" and self.board.log_of(x) is None:
                return None
            if kk in ("NUM", "WORD") and not 0 <= x < nn:
                return None
            if kk == "KEY" and not a.lo <= x <= a.hi:
                return None
        return tuple(ix)

    def instances(self, o):
        """Every instance an object has on this board, in the firmware's order."""
        if not o.levels:
            return [()]
        k, n = o.levels[0][0], o.levels[0][1]
        first = self.board.fronts() if k == "PORT" else range(n)
        if len(o.levels) == 1:
            return [(x,) for x in first]
        return [(x, y) for x in first for y in range(o.levels[1][1])]

    def lport(self, o, ix):
        return self.board.log_of(ix[0]) if o.levels and o.levels[0][0] == "PORT" else 0

    # ---- values

    def default(self, o, f, entry=None):
        d = f.default
        if om_gen.chip(d):
            d = d["rtl8373" if self.board.rtl8373 else "rtl8372"]
        if entry is not None and f.cname in entry:
            d = entry[f.cname]
        return self.enc_default(o, f, d)

    def enc_default(self, o, f, d):
        t = f.typ
        if isinstance(d, str) and d == "OM_ISOLATE_DEFAULT":
            return struct.pack("<H", self.board.isolate_default())
        if t.code == "PMASK":
            return struct.pack("<H", int(d, 0) if isinstance(d, str) else d)
        if t.code == "PORT":
            return bytes([0xff])
        if t.code == "BOOL":
            return bytes([1 if d else 0])
        if t.code in ("U8", "U16", "U32"):
            return int(d if not isinstance(d, str) else int(d, 0)).to_bytes(t.size, "little")
        if t.code == "I16":
            return struct.pack("<h", d)
        if t.code == "TZ":
            return struct.pack("<h", om_gen.tz_min(f.name, d))
        if t.code == "ENUM":
            return bytes([t.words.index(d)])
        if t.code in ("FLAGS", "FLAGS16"):
            v = 0
            for w in ([] if d == "none" else d.split(",")):
                v |= 1 << t.words.index(w)
            return v.to_bytes(t.size, "little")
        if t.code in ("STR", "SECRET"):
            return d.encode().ljust(t.arg + 1, b"\0")
        if t.code in ("IPV4", "MASK4"):
            return bytes(om_gen.ip_bytes(f.name, d))
        if t.code == "MAC":
            return bytes(om_gen.mac_bytes(f.name, d, t.arg))
        if t.code == "BRID":
            return bytes(8)
        raise Bad("%s.%s: default %r" % (o.regname, f.name, d))

    def parse(self, o, f, s):
        """Text -> the field's bytes, as om.c parse_value() takes it."""
        t, b = f.typ, self.board
        c = t.code
        if c == "BOOL":
            if s not in ("on", "off"):
                raise Bad("on off")
            return bytes([s == "on"])
        if c == "ENUM":
            if s not in t.words:
                raise Bad(" ".join(t.words))
            return bytes([t.words.index(s)])
        if c in ("STR", "SECRET"):
            if len(s) > t.arg or any(not 0x20 <= ord(ch) <= 0x7e or ch in "\"\\" for ch in s) \
                    or (c == "SECRET" and " " in s):
                raise Bad("printable text without \" or \\, at most its length")
            return s.encode().ljust(t.arg + 1, b"\0")
        if c == "PMASK":
            v = 0
            for w in s.split(","):
                if w == "none":
                    continue
                if w == "cpu" and t.arg:
                    v |= 1 << CPU
                    continue
                if w == "all":
                    for n in b.fronts():
                        v |= 1 << b.log_of(n)
                    if t.arg:
                        v |= 1 << CPU
                    continue
                m = re.fullmatch(r"(\d+)(?:-(\d+))?", w)
                if not m or (m[2] and int(m[2]) < int(m[1])):
                    raise Bad("ports like 1-4,9 (none, all, cpu)")
                for n in range(int(m[1]), int(m[2] or m[1]) + 1):
                    if b.log_of(n) is None:
                        raise Bad("ports like 1-4,9 (none, all, cpu)")
                    v |= 1 << b.log_of(n)
            return struct.pack("<H", v)
        if c in ("IPV4", "MASK4"):
            p = s.split(".")
            if len(p) != 4 or not all(x.isdigit() and int(x) < 256 for x in p):
                raise Bad("a.b.c.d")
            v = bytes(int(x) for x in p)
            if c == "MASK4":
                bits = int.from_bytes(v, "big")
                if bits and (~bits & 0xffffffff) & ((~bits & 0xffffffff) + 1):
                    raise Bad("a.b.c.d, contiguous")
            return v
        if c == "MAC":
            if t.arg and s == "auto":
                return bytes(6)
            return self._mac(s)
        if c == "BRID":
            m = re.fullmatch(r"(\d+)\.(\d+)\.(.+)", s)
            if not m or int(m[1]) > 15 or int(m[2]) > 4095:
                raise Bad("0-15.0-4095.aa:bb:cc:dd:ee:ff")
            return bytes([int(m[1]) << 4 | int(m[2]) >> 8, int(m[2]) & 0xff]) + self._mac(m[3])
        if c in ("FLAGS", "FLAGS16"):
            v = 0
            if s != "none":
                for w in s.split(","):
                    if w not in t.words:
                        raise Bad(" ".join(t.words))
                    v |= 1 << t.words.index(w)
            return v.to_bytes(t.size, "little")
        if c == "PORT":
            if t.arg and s == "none":
                return bytes([0xff])
            if not s.isdigit() or b.log_of(int(s)) is None:
                raise Bad("a port")
            return bytes([b.log_of(int(s))])
        if c in ("I16", "TZ"):
            m = re.fullmatch(r"([+-]?)(\d+)" + (r":(00|30|45)" if c == "TZ" else ""), s)
            if not m:
                raise Bad("%d..%d" % (t.lo, t.hi))
            v = int(m[2]) * 60 + int(m[3]) if c == "TZ" else int(m[2])
            v = -v if m[1] == "-" else v
            if not t.lo <= v <= t.hi:
                raise Bad("%d..%d" % (t.lo, t.hi))
            return struct.pack("<h", v)
        if c in ("U8", "U16", "U32"):
            if not s.isdigit() or not t.lo <= int(s) <= t.hi:
                raise Bad("%d-%d" % (t.lo, t.hi))
            return int(s).to_bytes(t.size, "little")
        raise Bad("read only")

    def _mac(self, s):
        p = re.split("[:-]", s)
        if len(p) != 6 or not all(re.fullmatch("[0-9a-fA-F]{2}", x) for x in p):
            raise Bad("aa:bb:cc:dd:ee:ff")
        return bytes(int(x, 16) for x in p)

    def fmt(self, o, f, v, secrets=True):
        """The field's bytes -> text, as om.c emit_value() prints it."""
        t, b = f.typ, self.board
        c = t.code
        if c == "BOOL":
            return "on" if v[0] else "off"
        if c == "ENUM":
            return t.words[v[0]] if v[0] < len(t.words) else "?"
        if c == "SECRET" and not secrets:
            return "*"
        if c in ("STR", "SECRET"):
            return v.split(b"\0")[0].decode("latin-1")
        if c == "PMASK":
            m = v[0] | v[1] << 8
            out, n = [], 1
            while n <= 9:
                lp = b.log_of(n)
                if lp is None or not m >> lp & 1:
                    n += 1
                    continue
                i = n
                while n < 9 and b.log_of(n + 1) is not None and m >> b.log_of(n + 1) & 1:
                    n += 1
                out.append(str(i) if n == i else "%d-%d" % (i, n))
                n += 1
            if m >> CPU & 1:
                out.append("cpu")
            return ",".join(out) or "none"
        if c in ("IPV4", "MASK4"):
            return ".".join(str(x) for x in v)
        if c == "MAC":
            if t.arg and not any(v):
                return "auto"
            return ":".join("%02x" % x for x in v)
        if c == "BRID":
            return "%d.%d.%s" % (v[0] >> 4, (v[0] & 15) << 8 | v[1], ":".join("%02x" % x for x in v[2:]))
        if c in ("FLAGS", "FLAGS16"):
            m = int.from_bytes(v, "little")
            return ",".join(w for i, w in enumerate(t.words) if m >> i & 1) or "none"
        if c == "PORT":
            return "none" if v[0] == 0xff else str(b.l2p[v[0]])
        if c == "I16":
            return str(struct.unpack("<h", v)[0])
        if c == "TZ":
            m = struct.unpack("<h", v)[0]
            return "%s%02d:%02d" % ("-" if m < 0 else "+", abs(m) // 60, abs(m) % 60)
        return str(int.from_bytes(v, "little"))

    def saved_len(self, f, v):
        """The bytes of a value that are saved: a Str without its NUL (om.c f_len)."""
        if f.typ.code in ("STR", "SECRET"):
            return v.split(b"\0")[0]
        return v


class Config:
    """A config as values: the defaults, then path lines."""

    def __init__(self, sch):
        self.s = sch
        self.v = {}             # (obj regname, ix) -> {field name: bytes}
        self.keys = {}          # keyed obj regname -> {key: entry defaults}
        for o in sch.objs:
            if o.levels and o.levels[0][0] == "KEY":
                self.keys[o.regname] = {}
                for key, vals in o.entries:
                    self._entry(o, (key,), vals)
            else:
                for ix in sch.instances(o):
                    self.v[(o.regname, ix)] = {f.name: sch.default(o, f) for f in o.cfields}

    def _entry(self, o, ix, vals=None):
        self.keys[o.regname][ix[0]] = True
        self.v[(o.regname, ix)] = {f.name: self.s.default(o, f, vals) for f in o.cfields}

    def line(self, text):
        """One "path value", "set path value" or "del path" line."""
        w = text.strip()
        if not w or w.startswith("#"):
            return
        if w.startswith("set "):
            w = w[4:]
        dele = w.startswith("del ")
        if dele:
            w = w[4:].strip()
        path, _, val = w.partition(" ")
        o, ix, f = self.s.resolve(path)
        keyed = o.levels and o.levels[0][0] == "KEY"
        if dele:
            if f is None and keyed:
                self.keys[o.regname].pop(ix[0], None)
                self.v.pop((o.regname, ix), None)
                return
            raise Bad("%s: del takes a keyed entry here" % path)
        if keyed and (o.regname, ix) not in self.v:
            self._entry(o, ix)
        if f is None:
            if keyed:
                return          # the entry, with its defaults
            raise Bad("%s: a field" % path)
        if (o.regname, ix) not in self.v or not self.s.board.when(f.when, self.s.lport(o, ix)):
            raise Bad("%s: not on this port" % path)
        try:
            self.v[(o.regname, ix)][f.name] = self.s.parse(o, f, val)
        except Bad as e:
            raise Bad("%s: %s" % (path, e))

    def records(self):
        """The firmware's enc_pass(): the records, without the header."""
        s, out = self.s, bytearray()
        for o in s.objs:
            keyed = o.levels and o.levels[0][0] == "KEY"
            if keyed:
                out += bytes([o.id, 0, 0, 0])
                insts = [(k,) for k in sorted(self.keys[o.regname])]
            else:
                insts = s.instances(o)
            for ix in insts:
                lp = s.lport(o, ix)
                vals = self.v[(o.regname, ix)]
                fs = [f for f in o.cfields if s.board.when(f.when, lp) and vals[f.name] != s.default(o, f)]
                if not fs and not keyed:
                    continue
                out += bytes([o.id]) + struct.pack("<H", s.rec_ix(o, ix)) + bytes([len(fs)])
                for f in fs:
                    v = s.saved_len(f, vals[f.name])
                    out += bytes([f.id, len(v)]) + v
        return bytes(out)


def txt2bin(sch, lines, errs=None):
    c = Config(sch)
    for n, ln in enumerate(lines, 1):
        try:
            c.line(ln)
        except Bad as e:
            if errs is None:
                raise
            errs.append("line %d: %s" % (n, e))
    r = c.records()
    if HDR + len(r) > CONFIG_LEN:
        raise Bad("%d B: more than the sector's %d" % (HDR + len(r), CONFIG_LEN))
    return MAGIC + struct.pack("<HH", len(r), crc16(r)) + r


def bin2txt(sch, data, secrets=True, skipped=None):
    """The lines a load takes (as get -s prints them, with the Secrets unless
    secrets is False); what it would skip goes to skipped."""
    if len(data) < HDR or data[:4] != MAGIC:
        raise Bad("blank" if not data or data[0] == 0xff else "bad, or old command text")
    n, c = struct.unpack("<HH", data[4:8])
    r = data[HDR:HDR + n]
    if n > CONFIG_LEN - HDR or len(r) != n or crc16(r) != c:
        raise Bad("bad")
    skipped = [] if skipped is None else skipped
    by_id = {o.id: o for o in sch.objs}
    out, i = [], 0
    while i + 4 <= n:
        oid, rix, nf = r[i], r[i + 1] | r[i + 2] << 8, r[i + 3]
        i += 4
        o = by_id.get(oid)
        ix = sch.from_rec_ix(o, rix) if o else None
        if o and o.levels and o.levels[0][0] == "KEY" and rix == 0:
            ix = None
        elif ix is None:
            skipped.append("object %d index %d" % (oid, rix))
        for _ in range(nf):
            fid, ln = r[i], r[i + 1]
            v = bytes(r[i + 2:i + 2 + ln])
            i += 2 + ln
            if ix is None:
                continue
            f = next((f for f in o.cfields if f.id == fid), None)
            if f is None:
                skipped.append("%s field %d" % (o.regname, fid))
                continue
            p = sch.path(o, ix, f)
            size = f.typ.size if f.typ.code not in ("STR", "SECRET") else f.typ.arg + 1
            if (f.typ.code in ("STR", "SECRET") and ln > f.typ.arg) or \
                    (f.typ.code not in ("STR", "SECRET") and ln != size) or \
                    not sch.board.when(f.when, sch.lport(o, ix)):
                skipped.append(p)
                continue
            v = v.ljust(size, b"\0")
            text = sch.fmt(o, f, v)
            try:
                sch.parse(o, f, text)       # what set takes, as the load checks it
            except Bad:
                skipped.append(p)
                continue
            if f.typ.code == "SECRET" and not secrets:
                continue
            out.append("%s %s" % (p, text))
    return out


# ---- the switch: login, /cli -------------------------------------------------

class Switch:
    def __init__(self, host, password="1234"):
        import http.client
        self.host, self.password, self.cookie = host, password, None
        self._hc = http.client

    def _req(self, method, path, body=None, ctype=None):
        c = self._hc.HTTPConnection(self.host, 80, timeout=30)
        h = {"Connection": "close"}
        if ctype:
            h["Content-Type"] = ctype
        if self.cookie:
            h["Cookie"] = "session=" + self.cookie
        c.request(method, path, body, h)
        r = c.getresponse()
        return r, r.read()

    def login(self):
        import urllib.parse
        r, _ = self._req("POST", "/login", "pwd=" + urllib.parse.quote(self.password),
                         "application/x-www-form-urlencoded")
        m = re.search(r"session=([^;]+)", r.getheader("Set-Cookie") or "")
        if not m:
            raise Bad("login refused")
        self.cookie = m[1]

    def cli(self, lines):
        if not self.cookie:
            self.login()
        r, b = self._req("POST", "/cli", "\n".join(lines), "text/plain")
        if r.status != 200:
            raise Bad("/cli %d" % r.status)
        return b.decode("latin-1")

    def get_config(self):
        if not self.cookie:
            self.login()
        r, b = self._req("GET", "/config")
        return b.decode("latin-1")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--machine", help="machine/<M>.c for the ports (default: the 8x)")
    ap.add_argument("--schema", action="append", help="schema directories (default: schema/)")
    ap.add_argument("--host")
    ap.add_argument("--password", default="1234")
    ap.add_argument("--no-secrets", action="store_true")
    ap.add_argument("cmd", choices=["txt2bin", "bin2txt", "convert", "default", "cli", "get-config"])
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    try:
        if a.cmd in ("cli", "get-config"):
            if not a.host:
                ap.error("--host")
            sw = Switch(a.host, a.password)
            sys.stdout.write(sw.cli(a.args) if a.cmd == "cli" else sw.get_config())
            return 0
        if a.cmd == "convert":
            import legacy_map
            lines, bad = legacy_map.convert(open(a.args[0], "rb").read(), Board(a.machine))
            for ln in lines:
                print(ln)
            for b in bad:
                print("convert: not translated: %s" % b, file=sys.stderr)
            return 1 if bad else 0
        if a.cmd == "default":
            # the board's default config (DESIGN.md 15.1): records once every
            # line is a path; old command text stays text (replayed at boot)
            # until its objects are on the object model
            text = open(a.args[0]).read()
            lines = [ln.strip() for ln in text.splitlines() if ln.strip() and not ln.strip().startswith("#")]
            if any("." not in ln.split()[1 if ln.startswith("set ") else 0] for ln in lines):
                open(a.args[1], "w").write(text)
                print("default: %s is old command text: kept as text" % a.args[0], file=sys.stderr)
                return 0
            data = txt2bin(Schema(a.schema, Board(a.machine)), lines)
            open(a.args[1], "wb").write(data)
            print("default: %s: %d B of records" % (a.args[0], len(data)), file=sys.stderr)
            return 0
        sch = Schema(a.schema, Board(a.machine))
        if a.cmd == "txt2bin":
            data = txt2bin(sch, open(a.args[0]).read().splitlines())
            open(a.args[1], "wb").write(data)
            print("txt2bin: %d B" % len(data), file=sys.stderr)
            return 0
        skipped = []
        for ln in bin2txt(sch, open(a.args[0], "rb").read(), not a.no_secrets, skipped):
            print(ln)
        for s in skipped:
            print("bin2txt: a load skips %s" % s, file=sys.stderr)
        return 0
    except Bad as e:
        print("%s: %s" % (a.cmd, e), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
