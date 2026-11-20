#!/usr/bin/env python3
"""om_gen.py - the object model's generator (doc/om/, ~/rtlpg-bench/object-model/DESIGN.md).

Reads the schema (schema/*.py, the declarations in section 10 of the design)
and the committed ID registry (schema/registry.txt), and writes:

  om_gen.h   structs (struct cfg_<obj>, struct cfg, struct st_<obj>, union
             om_st), object and enum constants, handler prototypes, and an
             OM_USE_<kind> for each index kind and type the schema uses (om.c
             compiles the others out)
  om_gen.c   the tables the engine reads: one string pool (names and enum
             words, each string shared where it already occurs), limits,
             field and object descriptors, per-object defaults, the banked
             handler table, and _Static_asserts pinning the packed layout

  --js PATH  the web UI's schema (html/schema.js, DESIGN 14.3): every
             object's fields with their type, limits or words, default, when
             rule and tip, as one `var SCHEMA = {...}`. The same on every
             board (no counts, no chip defaults), so one file serves them all.

  om_gen.py --register   adds IDs for new config objects and fields to the
                         registry (commit the result); a build fails while
                         the registry lacks one

-s may be given more than once (the host tests add a schema of their own);
each directory has its own registry.txt, and a registry's "base N" line
starts its object IDs at N.

An object's name in the tables is its path with '#' for each index:
"sys", "port.#", "vlan.#", "stp.port.#", "port.#.storm.#". Its registry name
drops the indices ("port.storm"): an index is never part of an ID.

Offsets are never computed here: the C uses offsetof(), so SDCC's packed
structs and gcc's padded ones (host tests) both come out right.
"""
import argparse
import struct
import os
import re
import sys

# Field types: code (om.h T_*)
T = {}
for i, n in enumerate(("BOOL U8 U16 U32 U64 ENUM STR PMASK SCALED ACT IPV4 MASK4 MAC "
                       "FLAGS FLAGS16 SECRET I16 TZ PORT BRID").split()):
    T[n] = i + 1
WHEN = {"always": 0, "copper": 1, "sfp": 2, "10g": 3}
IX = {"PORT": 0, "NUM": 1, "KEY": 2, "WORD": 3, "IDX": 4, "STREAM": 5}


class Type:
    def __init__(self, code, ctype, size, lo=None, hi=None, arg=0, words=None, enum=None):
        self.code, self.ctype, self.size = code, ctype, size
        self.lo, self.hi, self.arg, self.words, self.enum = lo, hi, arg, words, enum

    def sig(self):
        """What makes two types incompatible in a saved config (registry)."""
        if self.code in ("STR", "SECRET"):
            return "%s(%d)" % ("Str" if self.code == "STR" else "Secret", self.arg)
        if self.code in ("ENUM", "FLAGS", "FLAGS16"):
            return "%s(%s)" % ({"ENUM": "Enum", "FLAGS": "Flags", "FLAGS16": "Flags16"}[self.code],
                               self.enum)
        return self.code


SCHEMA = {"objs": [], "enums": {}, "msgs": {}, "consts": {}, "cur": None, "dir": None}


def _words(name, words):
    w = words.split()
    old = SCHEMA["enums"].get(name)
    if old is not None and old != w:
        raise SystemExit("enum %s declared twice with different words" % name)
    SCHEMA["enums"][name] = w
    return w


def Enum(name, words=None):
    """words=None: an enum another schema file declares (state.sched's, from
    the scheduler's generator), for a state field; resolved once all are read."""
    if words is None:
        return Type("ENUM", "uint8_t", 1, enum=name)
    w = _words(name, words)
    return Type("ENUM", "uint8_t", 1, 0, len(w) - 1, words=w, enum=name)


def Flags(name, words):
    w = _words(name, words)
    if len(w) > 16:
        raise SystemExit("flags %s: more than 16 words" % name)
    big = len(w) > 8
    return Type("FLAGS16" if big else "FLAGS", "uint16_t" if big else "uint8_t", 2 if big else 1,
                words=w, enum=name)


def _int(code, ctype, size, mx, mn=0):
    def f(lo=mn, hi=mx):
        return Type(code, ctype, size, lo, hi)
    return f


U8 = _int("U8", "uint8_t", 1, 0xff)
U16 = _int("U16", "uint16_t", 2, 0xffff)
U32 = _int("U32", "uint32_t", 4, 0xffffffff)
I16 = _int("I16", "int16_t", 2, 0x7fff, -0x8000)
Bool = Type("BOOL", "uint8_t", 1, 0, 1)
U64 = Type("U64", "uint8_t", 8)        # stored as 8 bytes, little endian
Action = Type("ACT", None, 0)
IPv4 = Type("IPV4", "uint8_t", 4)      # a.b.c.d, in memory order (uip's)
Mask4 = Type("MASK4", "uint8_t", 4)
BridgeId = Type("BRID", "uint8_t", 8)  # priority (big endian), MAC


def TZ(lo=-12 * 60, hi=14 * 60):
    """Minutes east of UTC, as +hh:mm (mm 00, 30 or 45)."""
    return Type("TZ", "int16_t", 2, lo, hi)


def MAC(auto=False):
    """aa:bb:cc:dd:ee:ff; with auto, all zeros is printed and set as `auto`."""
    return Type("MAC", "uint8_t", 6, arg=1 if auto else 0)


def Port(none=False):
    """One port: the logical port inside, its front-panel number outside; 0xff = none,
    CPU_LOGICAL_PORT (9) cpu (printed only)."""
    return Type("PORT", "uint8_t", 1, arg=1 if none else 0)


def Str(n):
    return Type("STR", "char", n + 1, arg=n)


def Secret(n):
    """A Str that get prints as *."""
    return Type("SECRET", "char", n + 1, arg=n)


def PortMask(cpu=False):
    return Type("PMASK", "uint16_t", 2, arg=1 if cpu else 0)


def Scaled(decimals):
    """A U16 printed with `decimals` digits after the point (e.g. decimetres as metres)."""
    return Type("SCALED", "uint16_t", 2, arg=decimals)


class Field:
    def __init__(self, name, typ, access, default=None, when="always", tip="", fn=None,
                 derived=False, unit="", label=""):
        # "": a state object's one value, printed as the instance's path
        if not (name == "" and access == "state") and not re.fullmatch(r"[a-z][a-z0-9_]*(\.[a-z0-9_]+)*", name):
            raise SystemExit("bad field name %r" % name)
        self.name, self.typ, self.access = name, typ, access
        self.default, self.when, self.tip, self.fn = default, WHEN[when], tip, fn
        self.unit, self.label = unit, label      # the web UI's only (schema.js)
        self.cname = name.replace(".", "_") or "v"
        if self.cname in ("static", "default", "auto", "register", "char", "int"):
            self.cname += "_"           # a C keyword
        self.id = 0
        self.derived = derived


class Obj:
    """An object: up to two index levels, each (kind, count, arg, C dimension).
    from0: count's index runs 0..count-1 (IDX), not 1..count. stream=n: a
    state object indexed 0..n-1 whose getter finds each instance (om.h
    IX_STREAM). names: the web UI's names for the indexes (schema.js only)."""

    def __init__(self, name, state, count=1, keyed=None, max=None, parent=None,
                 apply=None, check=None, get=None, derive=None, order=0, tip="",
                 from0=False, stream=None, names=None):
        if not re.fullmatch(r"[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)*", name):
            raise SystemExit("bad object name %r" % name)
        self.state = state
        self.apply, self.check, self.get, self.derive = apply, check, get, derive
        self.order, self.tip, self.names = order, tip, names
        self.fields, self.entries = [], []
        self.id = 0
        self.dir = SCHEMA["dir"]
        self.parent = None
        lv = []
        pat = []
        if parent is not None:
            par = [o for o in SCHEMA["objs"] if o.regname == parent and o.state == state]
            if not par or len(par[0].levels) != 1 or par[0].levels[0][0] not in ("PORT", "NUM"):
                raise SystemExit("%s: parent %s must be an object numbered by port or 1-n, "
                                 "declared before it" % (name, parent))
            self.parent = par[0]
            lv.append(par[0].levels[0])
            pat.append(par[0].pattern)
        pat.append(name)
        if keyed is not None:
            if parent is not None or max is None:
                raise SystemExit("%s: a keyed object needs max= and no parent" % name)
            dim = str(max)          # C: the number, or a const()'s name
            if isinstance(max, str):
                if max not in SCHEMA["consts"]:
                    raise SystemExit("%s: max=%s is no const() declared before it" % (name, max))
                max = SCHEMA["consts"][max]
            lv.append(("KEY", max, keyed, dim))
        elif stream is not None:
            if parent is not None or not state or not get:
                raise SystemExit("%s: a streamed object is a state object with get= and no parent" % name)
            lv.append(("STREAM", stream, None, "0"))
        elif count == "ports":
            lv.append(("PORT", 9, None, "OM_PORTS"))
        elif isinstance(count, Type):
            lv.append(("WORD", len(count.words), count, str(len(count.words))))
        elif count != 1:
            lv.append(("IDX" if from0 else "NUM", count, None, str(count)))
        if len(lv) > 1 and lv[1][0] in ("PORT", "KEY", "STREAM"):
            raise SystemExit("%s: the second index must be a number or a word" % name)
        self.levels = lv
        # the name with '#' for each index
        if parent is not None:
            self.pattern = pat[0] + "." + name + (".#" if len(lv) == 2 else "")
        else:
            self.pattern = name + (".#" if lv else "")
        self.regname = (parent + "." if parent else "") + name
        self.cname = ("st_" if state else "cfg_") + self.regname.replace(".", "_")
        self.member = self.regname.replace(".", "_")


def _add(o):
    SCHEMA["objs"].append(o)
    SCHEMA["cur"] = o
    return o


def obj(name, **kw):
    return _add(Obj(name, False, **kw))


def state(name, **kw):
    return _add(Obj(name, True, **kw))


# The types om.c's parse_value() sets (OM_SET_ compiles the second row in)
PARSED = set("BOOL ENUM STR SECRET PMASK U8 U16 U32 "
             "IPV4 MASK4 MAC BRID FLAGS FLAGS16 PORT I16 TZ".split())


def cfg(name, typ, default, when="always", tip="", derived=False, unit="", label=""):
    """derived: a Str whose empty value means "work it out" (the object's derive=
    handler gives it; get prints that). unit, label: for the web UI (schema.js):
    the value's unit ("B", "s"), and the UI string naming the field when not
    f_<object>_<field>."""
    o = SCHEMA["cur"]
    assert not o.state, "cfg() in a state object"
    if typ.code not in PARSED:
        raise SystemExit("%s.%s: om.c has no parser for %s: a config field can't be one" % (o.regname, name, typ.code))
    if derived and (typ.code != "STR" or default != ""):
        raise SystemExit("%s.%s: only a Str with default \"\" can be derived" % (o.regname, name))
    o.fields.append(Field(name, typ, "cfg", default, when, tip, derived=derived, unit=unit, label=label))


def st(name, typ, when="always", tip="", unit="", label=""):
    o = SCHEMA["cur"]
    assert o.state, "st() in a config object"
    o.fields.append(Field(name, typ, "state", None, when, tip, unit=unit, label=label))


def action(name, fn, when="always", tip="", label=""):
    o = SCHEMA["cur"]
    assert not o.state, "action() belongs to a config object"
    o.fields.append(Field(name, Action, "action", None, when, tip, fn=fn, label=label))


def default_entry(key, **values):
    """A keyed object's entry that exists by default (vlan.1)."""
    o = SCHEMA["cur"]
    if not o.levels or o.levels[0][0] != "KEY":
        raise SystemExit("%s: default_entry() needs a keyed object" % o.regname)
    o.entries.append((key, values))


def const(name, value):
    """A schema constant (MAX_VLANS): om_gen.h #defines it, and a keyed object's
    max= may name it (the name in C, its value here and in schema.js)."""
    if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name) or not isinstance(value, int) or value < 1:
        raise SystemExit("const %s: an upper-case name and a number of at least 1" % name)
    if re.match(r"(OM|O|E)_", name):
        raise SystemExit("const %s: OM_, O_ and E_ are om_gen.h's own names" % name)
    if SCHEMA["consts"].get(name, value) != value:
        raise SystemExit("const %s declared twice with different values" % name)
    SCHEMA["consts"][name] = value


def message(name, text):
    """A handler's error message: om_fail(OM_E_<NAME>) prints "err <path>: text"."""
    if not re.fullmatch(r"[a-z][a-z0-9_]*", name):
        raise SystemExit("bad message name %r" % name)
    if not re.fullmatch(r"[ -~]+", text) or '"' in text or "\\" in text:
        raise SystemExit("message %s: printable text without \" or \\" % name)
    if SCHEMA["msgs"].get(name, text) != text:
        raise SystemExit("message %s declared twice with different text" % name)
    SCHEMA["msgs"][name] = text


DSL = {k: v for k, v in globals().items() if k in (
    "Enum Flags U8 U16 U32 I16 U64 Bool Str Secret PortMask Scaled IPv4 Mask4 MAC Port TZ BridgeId "
    "obj state cfg st action default_entry message const".split())}


def load_schema(dirs, skip=(), js=False):
    for d in dirs:
        SCHEMA["dir"] = d
        for fn in sorted(os.listdir(d)):
            if fn.endswith(".py") and fn not in skip:
                p = os.path.join(d, fn)
                exec(compile(open(p).read(), p, "exec"), dict(DSL, PORTS="ports"))
    objs = SCHEMA["objs"]
    # the config objects in apply order (DESIGN 6.21; a child at its parent's), stable,
    # in the slots config objects had: the state objects stay where the files put them
    cfg = iter(sorted((o for o in objs if not o.state), key=lambda o: (o.parent or o).order))
    objs[:] = [o if o.state else next(cfg) for o in objs]
    seen = set()
    for o in objs:
        key = ("state." if o.state else "") + o.regname
        if key in seen:
            raise SystemExit("object %s declared twice" % key)
        seen.add(key)
        names = set()
        for f in o.fields:
            if f.name in names:
                raise SystemExit("%s.%s declared twice" % (o.regname, f.name))
            names.add(f.name)
            if f.when and (not o.levels or o.levels[0][0] != "PORT"):
                raise SystemExit("%s.%s: when= needs an object numbered by port" % (o.regname, f.name))
            check_default(o, f)
        for key_, vals in o.entries:
            lo, hi = o.levels[0][2].lo, o.levels[0][2].hi
            if not lo <= key_ <= hi:
                raise SystemExit("%s: default entry %d outside %d-%d" % (o.regname, key_, lo, hi))
            for n in vals:
                if n not in [f.cname for f in o.fields]:
                    raise SystemExit("%s: default entry sets no field %s" % (o.regname, n))
        for f in o.fields:
            if f.typ.code == "ENUM" and f.typ.words is None:
                if o.state is False:
                    raise SystemExit("%s.%s: Enum(%r) without words in a config object" % (o.regname, f.name, f.typ.enum))
                if f.typ.enum not in SCHEMA["enums"] and not js:
                    raise SystemExit("%s.%s: Enum(%r) is declared nowhere" % (o.regname, f.name, f.typ.enum))
        dv = [f.name for f in o.fields if f.derived]
        if bool(dv) != bool(o.derive):
            raise SystemExit("%s: derive= and derived fields go together" % o.regname)
        if o.entries and len(o.entries) > o.levels[0][1]:
            raise SystemExit("%s: more default entries than max" % o.regname)
    return objs


def chip(d):
    return isinstance(d, dict)


def check_default(o, f):
    if f.access != "cfg":
        return
    for d in (f.default.values() if chip(f.default) else [f.default]):
        check_value(o, f, d)


def check_value(o, f, d):
    t = f.typ
    where = "%s.%s" % (o.regname, f.name)
    if isinstance(d, str) and t.code in ("U8", "U16", "U32", "PMASK", "PORT") and not d.isdigit() \
            and d != "none":
        return              # a C expression
    if t.code == "BOOL":
        if d not in (True, False):
            raise SystemExit("%s: default must be True/False" % where)
    elif t.code in ("U8", "U16", "U32", "I16"):
        if not (t.lo <= d <= t.hi):
            raise SystemExit("%s: default %r outside %d-%d" % (where, d, t.lo, t.hi))
    elif t.code == "TZ":
        if not (t.lo <= tz_min(where, d) <= t.hi):
            raise SystemExit("%s: default %r outside the range" % (where, d))
    elif t.code == "ENUM":
        if d not in t.words:
            raise SystemExit("%s: default %r not one of %s" % (where, d, " ".join(t.words)))
    elif t.code in ("FLAGS", "FLAGS16"):
        for w in ([] if d == "none" else d.split(",")):
            if w not in t.words:
                raise SystemExit("%s: default %r not of %s" % (where, w, " ".join(t.words)))
    elif t.code in ("STR", "SECRET"):
        if len(d) > t.arg:
            raise SystemExit("%s: default longer than %d" % (where, t.arg))
        if '"' in d or "\\" in d:
            raise SystemExit("%s: default has \" or \\" % where)
    elif t.code in ("IPV4", "MASK4"):
        ip_bytes(where, d)
    elif t.code == "MAC":
        mac_bytes(where, d, t.arg)
    elif t.code == "PORT":
        if d != "none" or not t.arg:
            raise SystemExit("%s: a Port default can only be none (front numbers are per board)" % where)


def tz_min(where, d):
    m = re.fullmatch(r"([+-]?)(\d\d):(00|30|45)", d)
    if not m:
        raise SystemExit("%s: default %r is not +hh:mm" % (where, d))
    v = int(m.group(2)) * 60 + int(m.group(3))
    return -v if m.group(1) == "-" else v


def ip_bytes(where, d):
    b = d.split(".")
    if len(b) != 4 or not all(x.isdigit() and int(x) < 256 for x in b):
        raise SystemExit("%s: default %r is not a.b.c.d" % (where, d))
    return [int(x) for x in b]


def mac_bytes(where, d, auto):
    if d == "auto" and auto:
        return [0] * 6
    b = re.split("[:-]", d)
    if len(b) != 6 or not all(re.fullmatch("[0-9a-fA-F]{2}", x) for x in b):
        raise SystemExit("%s: default %r is not a MAC" % (where, d))
    return [int(x, 16) for x in b]


# ---- the registry ----------------------------------------------------------

def read_registry(path):
    reg = {"obj": {}, "field": {}, "lines": [], "base": 1, "path": path}
    if not os.path.exists(path):
        return reg
    for n, line in enumerate(open(path), 1):
        line = line.rstrip("\n")
        reg["lines"].append(line)
        s = line.split("#")[0].split()
        if not s:
            continue
        if s[0] == "obj" and len(s) == 3:
            reg["obj"][s[2]] = int(s[1])
        elif s[0] == "base" and len(s) == 2:
            reg["base"] = int(s[1])
        elif s[0] in ("field", "retired") and len(s) >= 3:
            oid, fid = (int(x) for x in s[1].split("."))
            reg["field"][s[2]] = (oid, fid, s[0] == "retired", s[3] if len(s) > 3 else "")
        else:
            raise SystemExit("%s:%d: bad registry line" % (path, n))
    return reg


def apply_registry(objs, regs, register):
    """Give every config object and field its ID (from its directory's registry);
    with register, add new ones."""
    added = {d: [] for d in regs}
    used_all = {}
    for d, reg in regs.items():
        for name, oid in reg["obj"].items():
            if oid in used_all:
                raise SystemExit("schema: object ID %d is %s in %s and %s in %s"
                                 % (oid, used_all[oid][0], used_all[oid][1], name, reg["path"]))
            used_all[oid] = (name, reg["path"])
    for o in objs:
        if o.state:
            continue
        reg = regs[o.dir]
        used_obj = {oid for oid in reg["obj"].values()}
        if o.regname not in reg["obj"]:
            if not register:
                raise SystemExit("schema: object %s has no ID: run tools/om_gen.py --register" % o.regname)
            oid = max(used_obj | {reg["base"] - 1}) + 1
            if oid in used_all:
                raise SystemExit("schema: object ID %d for %s is taken (%s)" % (oid, o.regname, used_all[oid][0]))
            reg["obj"][o.regname] = oid
            used_all[oid] = (o.regname, reg["path"])
            added[o.dir].append("obj %d %s" % (oid, o.regname))
        o.id = reg["obj"][o.regname]
        used = {fid for (oid, fid, _, _) in reg["field"].values() if oid == o.id}
        for f in o.fields:
            if f.access != "cfg":
                continue
            path_ = "%s.%s" % (o.regname, f.name)
            r = reg["field"].get(path_)
            if r is None:
                if not register:
                    raise SystemExit("schema: %s has no ID: run tools/om_gen.py --register" % path_)
                fid = max(used | {0}) + 1
                if fid > 255:
                    raise SystemExit("schema: %s: object %s has no field IDs left" % (path_, o.regname))
                used.add(fid)
                reg["field"][path_] = (o.id, fid, False, f.typ.sig())
                added[o.dir].append("field %d.%d %s %s" % (o.id, fid, path_, f.typ.sig()))
                r = reg["field"][path_]
            oid, fid, retired, sig = r
            if retired:
                raise SystemExit("schema: %s was retired; a new field needs a new name" % path_)
            if sig and sig != f.typ.sig():
                raise SystemExit("schema: %s changed type %s -> %s: retire it and add a new field"
                                 % (path_, sig, f.typ.sig()))
            f.id = fid
    # every registered, unretired field must still exist
    for d, reg in regs.items():
        have = {"%s.%s" % (o.regname, f.name) for o in objs if not o.state and o.dir == d
                for f in o.fields}
        for p, (oid, fid, retired, sig) in reg["field"].items():
            if not retired and p not in have:
                raise SystemExit("schema: %s (%d.%d) is registered but gone: "
                                 "mark it 'retired' in %s" % (p, oid, fid, reg["path"]))
        if added[d]:
            with open(reg["path"], "a") as fp:
                if not reg["lines"]:
                    fp.write("# The object model's stable IDs (DESIGN.md 10.5): appended by\n"
                             "# tools/om_gen.py --register, never edited or reused.\n"
                             "#   obj <id> <object>          field <obj>.<id> <path> <type>\n"
                             "#   retired <obj>.<id> <path>  (its ID is never handed out again)\n")
                for a in added[d]:
                    fp.write(a + "\n")
                    print("registered:", a)


# ---- C output ----------------------------------------------------------------

def c_value(o, f, d):
    t = f.typ
    where = "%s.%s" % (o.regname, f.name)
    if t.code == "BOOL":
        return "1" if d else "0"
    if t.code == "ENUM":
        return "%d" % t.words.index(d)
    if t.code in ("FLAGS", "FLAGS16"):
        v = 0
        for w in ([] if d == "none" else d.split(",")):
            v |= 1 << t.words.index(w)
        return "0x%x" % v
    if t.code in ("STR", "SECRET"):
        return '"%s"' % d
    if t.code in ("IPV4", "MASK4"):
        return "{ %s }" % ", ".join(str(b) for b in ip_bytes(where, d))
    if t.code == "MAC":
        return "{ %s }" % ", ".join("0x%02x" % b for b in mac_bytes(where, d, t.arg))
    if t.code == "BRID":
        return "{ 0 }"
    if t.code == "TZ":
        return str(tz_min(where, d))
    if t.code == "PORT":
        return "0xff" if d == "none" else d
    if t.code == "PMASK":
        return d if isinstance(d, str) else "0x%04x" % d
    if t.code in ("U64",):
        return "{ 0 }"
    return str(d)


def element_init(o, which=None, entry=None):
    """The initializer of one element: the defaults (chip `which`), or a default entry."""
    v = []
    if o.levels and o.levels[0][0] == "KEY":
        v.append(str(entry[0]) if entry else "0")
    for f in o.fields:
        if f.typ.code == "ACT":
            continue
        d = f.default
        if chip(d):
            d = d[which or "rtl8372"]
        if entry and f.cname in entry[1]:
            d = entry[1][f.cname]
            check_value(o, f, d)
        v.append(c_value(o, f, d))
    return "{ %s }" % (", ".join(v) or "0")   # actions only: the struct's `none`


class Pool:
    """One string pool: each string a substring of it where it already occurs."""

    def __init__(self):
        self.data = b""

    def add(self, s):
        b = s.encode() + b"\0"
        i = self.data.find(b)
        if i < 0:
            i = len(self.data)
            self.data += b
        return i

    def c(self):
        out, cur = [], ""
        for ch in self.data.decode():
            if ch == "\0":
                out.append('"%s\\0"' % cur)   # its own literal: "\0" before a digit is octal
                cur = ""
            else:
                cur += ch
        return "\n\t".join(out)


def gen(objs, outdir):
    enums = SCHEMA["enums"]
    ename = list(enums)
    h, c = [], []
    H = h.append
    C = c.append
    use, sets = set(), set()
    for o in objs:
        for lv in o.levels:
            if lv[0] in ("KEY", "WORD"):
                use.add(lv[0])
            if lv[0] in ("NUM", "IDX", "STREAM"):
                use.add(lv[0])
        if len(o.levels) == 2:
            use.add("IX2")
        if any(chip(f.default) for f in o.fields):
            use.add("CHIPDEF")
        if o.derive:
            use.add("DERIVE")
        for f in o.fields:
            use.add({"MASK4": "IPV4", "FLAGS16": "FLAGS"}.get(f.typ.code, f.typ.code))
            if f.access == "cfg":
                sets.add({"MASK4": "IPV4", "FLAGS16": "FLAGS"}.get(f.typ.code, f.typ.code))
    vmax = max([8] + [f.typ.size for o in objs for f in o.fields if f.access == "cfg"])
    H("/* generated by tools/om_gen.py from schema/: do not edit */")
    H("#ifndef OM_GEN_H")
    H("#define OM_GEN_H")
    H("#include <stdint.h>")
    H('#include "om.h"')
    H("")
    H("#define OM_PORTS 9\t/* logical ports 0..8: one layout for every board */")
    for k, v in SCHEMA["consts"].items():
        H("#define %s %d" % (k, v))
    H("#define OM_N_OBJ %d" % len(objs))
    H("#define OM_VMAX %d\t/* the largest settable value */" % vmax)
    H("#define OM_H_UNIT (0x80 + 21)\t/* help: the blob's unit tokens (help_blob()) */")
    H("#define OM_H_UNITS %d" % len(help_units(objs)))
    H("#define OM_H_LINE %d\t/* help's longest line, at most */" % help_line_max(objs))
    for u in ("NUM KEY WORD IDX STREAM IX2 CHIPDEF DERIVE IPV4 MAC FLAGS SECRET I16 TZ PORT BRID").split():
        H("#define OM_USE_%s %d" % (u, u in use))
    # OM_SET_<type>: a config field has it, so om.c parses it (a state-only
    # type is printed, never parsed)
    for u in ("IPV4 MAC FLAGS I16 TZ PORT BRID").split():
        H("#define OM_SET_%s %d" % (u, u in sets))
    for i, o in enumerate(objs):
        H("#define O_%s %d" % (o.cname.upper(), i))
    H("")
    for e in ename:
        for i, w in enumerate(enums[e]):
            H("#define E_%s_%s %d" % (e.upper(), re.sub(r"\W", "_", w.upper()), i))	# lag.1: E_X_LAG_1
    for i, m in enumerate(SCHEMA["msgs"]):
        H("#define OM_E_%s %d" % (m.upper(), i))
    H("")
    for o in objs:
        H("struct %s {" % o.cname)
        if o.levels and o.levels[0][0] == "KEY":
            H("\tuint16_t key;\t/* 0: a free slot */")
        for f in o.fields:
            t = f.typ
            if t.code == "ACT":
                continue
            if t.code in ("STR", "SECRET"):
                H("\tchar %s[%d];" % (f.cname, t.size))
            elif t.ctype == "uint8_t" and t.size > 1:
                H("\tuint8_t %s[%d];" % (f.cname, t.size))
            else:
                H("\t%s %s;" % (t.ctype, f.cname))
        if not [f for f in o.fields if f.typ.code != "ACT"] and not (o.levels and o.levels[0][0] == "KEY"):
            H("\tuint8_t none;")
        H("};")
    H("struct cfg {")
    for o in objs:
        if not o.state:
            H("\tstruct %s %s%s;" % (o.cname, o.member, "".join("[%s]" % lv[3] for lv in o.levels)))
    H("};")
    H("union om_st {")
    for o in objs:
        if o.state:
            H("\tstruct %s %s;" % (o.cname, o.member))
    H("};")
    H("extern __xdata struct cfg cfg_run, cfg_cand, cfg_roll;")
    H("extern __xdata union om_st om_st;")
    H("extern __code const struct om_obj om_obj[OM_N_OBJ];")
    H("extern __code const om_fn om_fns[];")
    H("")
    fns = []
    for o in objs:
        for fn in (o.apply, o.check, o.get, o.derive):
            if fn and fn not in fns:
                fns.append(fn)
        for f in o.fields:
            if f.fn and f.fn not in fns:
                fns.append(f.fn)
    for fn in fns:
        H("uint8_t %s(uint8_t idx) __banked;" % fn)
    H("")
    H("#endif")

    C("/* generated by tools/om_gen.py from schema/: do not edit */")
    C("#include <stddef.h>")
    C('#include "om_gen.h"')
    C("")
    C("__xdata struct cfg cfg_run, cfg_cand, cfg_roll;")
    C("__xdata union om_st om_st;")
    C("")
    # the pool: the longest strings first, so the short ones fall inside them;
    # enum words are NUL-separated and end in an empty word
    pool = Pool()
    strs = [("e", e, "\0".join(enums[e]) + "\0") for e in ename]
    strs += [("o", o.cname, o.pattern) for o in objs]
    strs += [("m", m, t) for m, t in SCHEMA["msgs"].items()]
    strs += [("f", (o.cname, f.cname), f.name) for o in objs for f in o.fields]
    at = {}
    for kind, key, s in sorted(strs, key=lambda x: -len(x[2])):
        at[(kind, key)] = pool.add(s)
    C("static __code const char om_str[] =\n\t%s;" % pool.c())
    C("__code const char * __code const om_enum[] = { %s };" %
      ", ".join("om_str + %d" % at[("e", e)] for e in ename) if ename else "")
    C("__code const char * __code const om_msg[] = { %s };" %
      ", ".join("om_str + %d" % at[("m", m)] for m in SCHEMA["msgs"]) if SCHEMA["msgs"] else
      "__code const char * __code const om_msg[] = { om_str };")
    C("")
    lims = []

    def lim(lo, hi):
        k = (lo, hi)
        if k not in lims:
            lims.append(k)
        return lims.index(k)
    for o in objs:
        C("static __code const struct om_field f_%s[] = {" % o.cname)
        for f in o.fields:
            t = f.typ
            if t.code == "ACT":
                off, arg = "0", str(fns.index(f.fn))
            else:
                off = "offsetof(struct %s, %s)" % (o.cname, f.cname)
                if t.code in ("ENUM", "FLAGS", "FLAGS16"):
                    arg = str(ename.index(t.enum))
                elif t.code in ("U8", "U16", "U32"):
                    arg = str(lim(t.lo, t.hi))
                elif t.code in ("I16", "TZ"):
                    arg = str(lim(t.lo + 0x8000, t.hi + 0x8000))
                else:
                    arg = str(t.arg)
            C("\t{ om_str + %d, %d, T_%s | %d << T_WHEN%s, %s, %s }," %
              (at[("f", (o.cname, f.cname))], f.id, t.code, f.when,
               " | T_DERIVED" if f.derived else "", off, arg))
        C("};")
    for o in objs:
        for lv in o.levels:
            if lv[0] == "KEY":
                lv_lim = lim(lv[2].lo, lv[2].hi)
                o.key_lim = lv_lim
            if lv[0] == "STREAM":
                o.key_lim = lim(0, lv[1] - 1)
    C("")
    C("__code const struct om_lim om_lim[] = {")
    for lo, hi in lims or [(0, 0)]:
        C("\t{ %dUL, %dUL }," % (lo, hi))
    C("};")
    C("")
    for o in objs:
        if o.state:
            continue
        cd = any(chip(f.default) for f in o.fields)
        if cd and o.entries:
            raise SystemExit("%s: per-chipset defaults in a keyed object" % o.regname)
        inits = [element_init(o, "rtl8372")]
        if cd:
            inits.append(element_init(o, "rtl8373"))
        for e in o.entries:
            inits.append(element_init(o, entry=e))
        o.chipdef = cd
        C("static __code const struct %s d_%s[%d] = {\n\t%s\n};" %
          (o.cname, o.cname, len(inits), ",\n\t".join(inits)))
    C("")

    def fi(fn):
        return fns.index(fn) if fn else 0xff

    def lvl(o, n):
        if n >= len(o.levels):
            return ("IX_NUM", "1", "0")
        kind, cnt, arg, dim = o.levels[n]
        a = "0"
        if kind in ("KEY", "STREAM"):
            a = str(o.key_lim)
            cnt = dim               # a const()'s name stays a name in C; a stream's 0
        elif kind == "WORD":
            a = str(ename.index(arg.enum))
        return ("IX_" + kind, str(cnt), a)
    C("__code const struct om_obj om_obj[OM_N_OBJ] = {")
    for o in objs:
        flags = "OM_STATE" if o.state else "OM_CFG"
        if not o.state and o.chipdef:
            flags += " | OM_CHIPDEF"
        base = "0" if o.state else "offsetof(struct cfg, %s)" % o.member
        dflt = "0" if o.state else "(__code const uint8_t *)d_%s" % o.cname
        l0, l1 = lvl(o, 0), lvl(o, 1)
        C("\t{ om_str + %d, f_%s, %s, %s, %d, %s, %d, sizeof(struct %s),\n"
          "\t  { %s, %s }, { %s, %s }, { %s, %s }, %d, %d, %d, %d, %d }," %
          (at[("o", o.cname)], o.cname, dflt, base, o.id, flags, len(o.fields), o.cname,
           l0[0], l1[0], l0[1], l1[1], l0[2], l1[2], len(o.entries),
           fi(o.apply), fi(o.check), fi(o.get), fi(o.derive)))
    C("};")
    C("")
    C("__code const om_fn om_fns[] = { %s };" % ", ".join(fns) if fns else "")
    C("")
    # layout pins: what the binary codec and rtlsim's read_struct assume (SDCC packs)
    C("#ifndef RTLP_HOST_TEST")
    for o in objs:
        sz = sum(f.typ.size for f in o.fields if f.typ.code != "ACT")
        if o.levels and o.levels[0][0] == "KEY":
            sz += 2
        C("_Static_assert(sizeof(struct %s) == %d, \"%s layout\");" % (o.cname, sz or 1, o.cname))
    C("#endif")

    os.makedirs(outdir, exist_ok=True)
    for name, lines in (("om_gen.h", h), ("om_gen.c", c)):
        p = os.path.join(outdir, name)
        text = "\n".join(lines) + "\n"
        if not os.path.exists(p) or open(p).read() != text:
            with open(p, "w") as fp:
                fp.write(text)
    return fns


# The web UI's schema (--js). Per object (its path, "state." before a state
# object's): i its index kinds (P port, N<count>, K<max> keyed, W<words>,
# I<count> from 0, S<count> streamed), t its tip, n the indexes' names (a
# name, or [high, low] for two 32-bit halves), f its fields (the name "": the
# instance's one value) in schema order, each an array [type, a, b,
# default, when, tip, unit, label] with trailing empties dropped. a, b by type: e/f the
# words; n the limits; d the decimals; s/p the length; m 1 with the CPU;
# mac 1 with auto; pt 1 with none; tz the limits in minutes. when: 0 always,
# 1 copper, 2 sfp, 3 10g. A default that depends on the chip or is a C
# expression is null. label: the UI string naming the field (app.js's
# LANG), when not the default f_<object>_<field> with "_" for ".".
# An e without words: a state field whose words are the build's (the
# scheduler's, state.sys.reset.slot): get prints them, schema/ hasn't them.
JS_T = {"BOOL": "b", "ENUM": "e", "FLAGS": "f", "FLAGS16": "f", "U8": "n", "U16": "n",
        "U32": "n", "I16": "n", "U64": "n", "SCALED": "d", "STR": "s", "SECRET": "p",
        "PMASK": "m", "IPV4": "ip", "MASK4": "ip", "MAC": "mac", "PORT": "pt", "TZ": "tz",
        "BRID": "br", "ACT": "a"}


def js_default(f):
    t, d = f.typ, f.default
    if f.access != "cfg" or chip(d) or t.code in ("PMASK", "U64", "BRID") \
            or (isinstance(d, str) and t.code in ("U8", "U16", "U32", "PORT") and not d.isdigit()
                and d != "none"):
        return None
    if t.code == "BOOL":
        return "on" if d else "off"
    return str(d)


def js_field(f):
    t = f.typ
    a = b = None
    if t.code in ("ENUM", "FLAGS", "FLAGS16") and t.words:
        a = " ".join(t.words)
    elif t.code in ("U8", "U16", "U32", "I16", "TZ"):
        a, b = t.lo, t.hi
    elif t.code in ("SCALED", "STR", "SECRET", "PMASK", "MAC", "PORT"):
        a = t.arg
    v = [JS_T[t.code], a, b, js_default(f), f.when, f.tip, f.unit, f.label]
    while v[-1] in (None, "", 0):
        v.pop()
    return v


def js(objs, path):
    import json
    out = {}
    for o in objs:
        e = {}
        if o.levels:
            e["i"] = "".join(k[0] + ("" if k == "PORT" else str(n)) for k, n, _, _ in o.levels)
        if o.tip:
            e["t"] = o.tip
        if o.names:
            e["n"] = o.names
        e["f"] = {f.name: js_field(f) for f in o.fields}
        out[("state." if o.state else "") + o.regname] = e
    text = ("// Generated by tools/om_gen.py --js from schema/: do not edit (DESIGN 14.3)\n"
            "var SCHEMA = " + json.dumps(out, separators=(",", ":")) + ";\n")
    # in the source tree, shared by every BUILDDIR: a build in parallel
    # never sees half a file
    if not os.path.exists(path) or open(path).read() != text:
        tmp = "%s.%d.tmp" % (path, os.getpid())
        with open(tmp, "w") as fp:
            fp.write(text)
        os.replace(tmp, path)


# The help blob (--help-blob, DESIGN 13): what om.c's help prints that the
# tables don't hold, read from the default config sector's tail. A table of
# u16 offsets from the blob's start (each object's entries, the verbs' usage,
# each token, then the tokens' end), the tokens, then per object its entry
# and one per field, in table order, and the usage, each NUL-ended. A byte
# >= 0x80 in an entry is token byte - 0x80. Tokens 0-19 are the types by T
# code, each with what om.c prints before the form from the tables ("u16 ",
# "str 0-") or with its fixed form ("bool on|off"); 20 is "object"; then the
# units (om_gen.h OM_H_UNIT, OM_H_UNITS: 'unit B "', which a field's entry
# starts with); the rest are the commonest substrings, picked greedily.
H_TYPE = {"BOOL": "bool on|off", "U8": "u8 ", "U16": "u16 ", "U32": "u32 ",
          "U64": "u64 0-18446744073709551615", "ENUM": "enum ", "STR": "str 0-", "PMASK": "ports ",
          "SCALED": "decimal -", "ACT": "action -", "IPV4": "ipv4 a.b.c.d", "MASK4": "netmask a.b.c.d",
          "MAC": "mac aa:bb:cc:dd:ee:ff", "FLAGS": "flags ", "FLAGS16": "flags ", "SECRET": "secret 0-",
          "I16": "i16 ", "TZ": "tz ", "PORT": "port ", "BRID": "bridge 0-15.0-4095.aa:bb:cc:dd:ee:ff"}
H_TOK_MAX = 15          # a picked token's bytes at most (a type's fit om.c's om_txt)
# help alone: om.c's verbs (om_verbs), each with its usage
H_USAGE = ("get [-c|-s|-d] PATH...\nset PATH VALUE\ndel PATH\ndiff\napply [N]\nconfirm\ndiscard\n"
           "do ACTION\nsave\nload saved|default\nhelp [-a|PATH]\nTab completes\n")


def help_units(objs):
    return sorted({f.unit for o in objs for f in o.fields if f.unit})


def help_line_max(objs):
    """The longest line help prints (om.c h_line()), at most: om.c waits for that
    much room in the console's ring, and a web step's outbuf room must hold it."""
    def unit(f):
        return len('unit %s "' % (f.unit if " " not in f.unit else '"%s"' % f.unit)) if f.unit else 1
    def form(t):
        c = t.code
        if c in ("ENUM", "FLAGS", "FLAGS16"):   # words=None: another file's (state.sched's)
            return len("|".join(t.words or SCHEMA["enums"].get(t.enum, [])))
        if c in ("U8", "U16", "U32", "I16"):
            return len("%d..%d" % (t.lo, t.hi))
        if c == "TZ":
            return len("-hh:mm..+hh:mm")
        if c in ("STR", "SECRET"):
            return len(str(t.arg))
        if c in ("PMASK", "PORT"):
            return text_max(t)
        return 0
    def ix(lv):
        k, n, a, _ = lv
        if k == "PORT":
            return len("1,3,5,7,9")
        if k == "WORD":
            return len("|".join(a.words))
        if k == "KEY":
            return len("%d-%d" % (a.lo, a.hi))
        return len("0-%d" % n)
    n = 0
    for o in objs:
        path = len("state." if o.state else "") + len(o.pattern)
        n = max(n, path + len(" object") + sum(1 + ix(lv) for lv in o.levels) + 2 + len(o.tip) + 2)
        for f in o.fields:
            d = len(" default ") + text_max(f.typ) + 2 if f.access == "cfg" else 0
            n = max(n, path + 1 + len(f.name) + 1 + len(H_TYPE[f.typ.code]) + form(f.typ) + d +
                    1 + unit(f) + len(f.tip) + 2)
    return n


def help_blob(objs, room):
    for o in objs:
        for what, s in [(o.regname, o.tip)] + [("%s.%s" % (o.regname, f.name), f.tip) for f in o.fields] + \
                [("%s.%s unit" % (o.regname, f.name), f.unit) for f in o.fields]:
            if not re.fullmatch(r"[ -~]*", s) or '"' in s or "\\" in s:
                raise SystemExit("schema: %s: a tip or unit is printable text without \" or \\" % what)
    used = {f.typ.code for o in objs for f in o.fields}
    units = help_units(objs)
    fixed = [(H_TYPE[n] if n in used else "").encode() for n in sorted(T, key=T.get)] + [b"object"]
    fixed += [('unit %s "' % (u if " " not in u else '"%s"' % u)).encode() for u in units]
    ents = []                   # per object: its entries; then the usage
    for o in objs:
        e = [o.tip.encode()]
        for f in o.fields:
            e.append((bytes([0x80 + 21 + units.index(f.unit)]) if f.unit else b"") + f.tip.encode())
        ents.append(e)
    ents.append([H_USAGE.encode()])
    # the tokens: the substring that saves most, while one does
    text = [t for e in ents for t in e]
    toks = []
    while len(fixed) + len(toks) < 128:
        cnt = {}
        for t in text:
            n = len(t)
            for i in range(n):
                if t[i] >= 0x80:
                    continue
                for j in range(i + 2, min(n, i + H_TOK_MAX) + 1):
                    if t[j - 1] >= 0x80:
                        break
                    s = t[i:j]
                    cnt[s] = cnt.get(s, 0) + 1
        best, bs = None, 0
        for s, c in cnt.items():
            g = c * (len(s) - 1) - (len(s) + 2)
            if g > bs or (g == bs and best is not None and s < best):
                best, bs = s, g
        if best is None:
            break
        b = bytes([0x80 + len(fixed) + len(toks)])
        toks.append(best)
        text = [t.replace(best, b) for t in text]
    for e in ents:
        for k in range(len(e)):
            e[k] = text.pop(0)
    tok = fixed + toks
    at = 2 * (len(ents) + len(tok) + 1)
    tab, body = [], b""
    for t in tok:
        tab.append(at + len(body))
        body += t
    tab.append(at + len(body))
    ent_at = []
    for e in ents:
        ent_at.append(at + len(body))
        body += b"".join(t + b"\0" for t in e)
    blob = b"".join(struct.pack("<H", v) for v in ent_at + tab) + body
    if help_line_max(objs) > CLI_WEB_ROOM:
        raise SystemExit("schema: a help line can reach %d B, more than a web step's room (tasks.c CLI_WEB_ROOM %d)"
                         % (help_line_max(objs), CLI_WEB_ROOM))
    if len(blob) > room:
        print("om_gen: the help blob is %d B, %d over its %d B; per object (B of tips):" % (len(blob), len(blob) - room, room),
              file=sys.stderr)
        for o, e in zip(objs, ents):
            print("  %5d %s%s" % (sum(len(t) + 1 for t in e), "state." if o.state else "", o.regname), file=sys.stderr)
        raise SystemExit("schema: the tips don't fit: shorten them")
    return blob, sum(len(t) for t in tok)


CONFIG_LEN = 0x1000      # layout.h: the saved config's sector
E_ROOM = 256             # om.c: a save step's records at most


# om.h's OM_TXT: om_txt, the text a load's val_ok() prints a value into
OM_TXT = int(re.search(r"#define OM_TXT\s+(\d+)",
                       open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "om.h")).read())[1])
# tasks.c's: outbuf's room a web step starts with, which a line must fit
CLI_WEB_ROOM = int(re.search(r"#define CLI_WEB_ROOM\s+(\d+)",
                             open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tasks.c")).read())[1])


def text_max(t):
    """The longest text emit_value() prints for a value of type t."""
    c = t.code
    if c in ("STR", "SECRET"):
        return t.arg
    if c == "ENUM":
        return max(len(w) for w in t.words)
    if c in ("FLAGS", "FLAGS16"):
        return max(4, sum(len(w) + 1 for w in t.words) - 1)
    if c == "PMASK":
        return len("1,3,5,7,9,cpu")
    if c == "BRID":
        return len("15.4095.aa:bb:cc:dd:ee:ff")
    if c == "SCALED":
        return len("65535") + 1
    return {"BOOL": 3, "U8": 3, "U16": 5, "U32": 10, "U64": 20, "I16": 6, "TZ": 6, "IPV4": 15,
            "MASK4": 15, "MAC": 17, "PORT": 4}[c]


def saved_worst(objs):
    """The binary saved config at its largest (DESIGN.md 15.3: every field off
    its default), and its largest instance; the build fails if the sector or
    a save step can't hold them."""
    total, big = 8, (0, None)
    for o in objs:
        sz = sum(f.typ.size for f in o.fields if f.typ.code != "ACT") + (2 if o.levels and o.levels[0][0] == "KEY" else 0)
        if sz > 255:            # om_obj.size and om.c's code_differs() indexes are uint8
            raise SystemExit("schema: %s is %d B, over 255 for an object" % (o.regname, sz))
        if o.state:
            continue
        inst = 4 + sum(2 + (f.typ.arg if f.typ.code in ("STR", "SECRET") else f.typ.size)
                       for f in o.fields if f.access == "cfg")
        n = 1
        for lv in o.levels:
            n *= lv[1]
        total += n * inst + (4 if o.levels and o.levels[0][0] == "KEY" else 0)
        big = max(big, (inst, o.regname))
        for f in o.fields:
            if f.access == "cfg" and text_max(f.typ) >= OM_TXT:
                raise SystemExit("schema: %s.%s's text can reach %d chars, more than a load's om_txt holds (%d)"
                                 % (o.regname, f.name, text_max(f.typ), OM_TXT - 1))
    if total > CONFIG_LEN:
        raise SystemExit("schema: the saved config can reach %d B, more than its sector's %d" % (total, CONFIG_LEN))
    if big[0] > E_ROOM:
        raise SystemExit("schema: %s's record can reach %d B, more than a save step's %d" % (big[1], big[0], E_ROOM))
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-s", "--schema", action="append")
    ap.add_argument("-o", "--out")
    ap.add_argument("-x", "--skip", action="append", default=[], help="a schema file (name) left out")
    ap.add_argument("--register", action="store_true")
    ap.add_argument("--js", help="write the web UI's schema.js here")
    ap.add_argument("--help-blob", help="write the help blob (the tips) here")
    ap.add_argument("--help-room", type=int, default=0, help="its room in flash: the build fails past it")
    a = ap.parse_args()
    dirs = a.schema or ["schema"]
    objs = load_schema(dirs, a.skip, js=a.js and not a.out)
    regs = {d: read_registry(os.path.join(d, "registry.txt")) for d in dirs}
    apply_registry(objs, regs, a.register)
    if a.out:
        gen(objs, a.out)
    if a.js:
        js(objs, a.js)
    if a.help_blob:
        blob, nt = help_blob(objs, a.help_room)
        if not os.path.exists(a.help_blob) or open(a.help_blob, "rb").read() != blob:
            with open(a.help_blob, "wb") as fp:
                fp.write(blob)
        print("om_gen: help blob %d B of %d (tokens %d B)" % (len(blob), a.help_room, nt))
    n_cfg = sum(len(o.fields) for o in objs if not o.state)
    n_st = sum(len(o.fields) for o in objs if o.state)
    print("om_gen: %d objects, %d config/action fields, %d state fields; saved config %d B at most"
          % (len(objs), n_cfg, n_st, saved_worst(objs)))


if __name__ == "__main__":
    main()
