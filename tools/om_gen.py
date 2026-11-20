#!/usr/bin/env python3
"""om_gen.py - the object model's generator (doc/om/, ~/rtlpg-bench/object-model/DESIGN.md).

Reads the schema (schema/*.py, the declarations in section 10 of the design)
and the committed ID registry (schema/registry.txt), and writes:

  om_gen.h   structs (struct cfg_<obj>, struct cfg, struct st_<obj>, union
             om_st), object and enum constants, handler prototypes
  om_gen.c   the tables the engine reads: names, enum words, limits, field
             and object descriptors, per-object defaults, the banked handler
             table, and _Static_asserts pinning the packed layout

  om_gen.py --register   adds IDs for new config objects and fields to the
                         registry (commit the result); a build fails while
                         the registry lacks one

Offsets are never computed here: the C uses offsetof(), so SDCC's packed
structs and gcc's padded ones (host tests) both come out right.
"""
import argparse
import os
import re
import sys

# Field types: code, C type, byte size, text
T = {}
for i, n in enumerate("BOOL U8 U16 U32 U64 ENUM STR PMASK SCALED ACT".split()):
    T[n] = i + 1


class Type:
    def __init__(self, code, ctype, size, lo=None, hi=None, arg=0, words=None, enum=None):
        self.code, self.ctype, self.size = code, ctype, size
        self.lo, self.hi, self.arg, self.words, self.enum = lo, hi, arg, words, enum

    def sig(self):
        """What makes two types incompatible in a saved config (registry)."""
        if self.code == "STR":
            return "Str(%d)" % self.arg
        if self.code == "ENUM":
            return "Enum(%s)" % self.enum
        return self.code


SCHEMA = {"objs": [], "enums": {}, "cur": None}


def Enum(name, words):
    w = words.split()
    old = SCHEMA["enums"].get(name)
    if old is not None and old != w:
        raise SystemExit("enum %s declared twice with different words" % name)
    SCHEMA["enums"][name] = w
    return Type("ENUM", "uint8_t", 1, 0, len(w) - 1, words=w, enum=name)


def _int(code, ctype, size, mx):
    def f(lo=0, hi=mx):
        return Type(code, ctype, size, lo, hi)
    return f


U8 = _int("U8", "uint8_t", 1, 0xff)
U16 = _int("U16", "uint16_t", 2, 0xffff)
U32 = _int("U32", "uint32_t", 4, 0xffffffff)
Bool = Type("BOOL", "uint8_t", 1, 0, 1)
U64 = Type("U64", "uint8_t", 8)        # stored as 8 bytes, little endian
Action = Type("ACT", None, 0)


def Str(n):
    return Type("STR", "char", n + 1, arg=n)


def PortMask(cpu=False):
    return Type("PMASK", "uint16_t", 2, arg=1 if cpu else 0)


def Scaled(decimals):
    """A U16 printed with `decimals` digits after the point (e.g. decimetres as metres)."""
    return Type("SCALED", "uint16_t", 2, arg=decimals)


# Availability rules (om.c om_when())
WHEN = {"always": 0, "copper": 1, "sfp": 2, "10g": 3}


class Field:
    def __init__(self, name, typ, access, default=None, when="always", tip="", fn=None):
        if not re.fullmatch(r"[a-z][a-z0-9_]*(\.[a-z0-9_]+)*", name):
            raise SystemExit("bad field name %r" % name)
        self.name, self.typ, self.access = name, typ, access
        self.default, self.when, self.tip, self.fn = default, WHEN[when], tip, fn
        self.cname = name.replace(".", "_")
        self.id = 0


class Obj:
    def __init__(self, name, state, count, apply=None, check=None, get=None, order=0, tip=""):
        self.name, self.state, self.count = name, state, count
        self.apply, self.check, self.get, self.order, self.tip = apply, check, get, order, tip
        self.fields = []
        self.id = 0
        self.cname = ("st_" if state else "cfg_") + name


def obj(name, count=1, apply=None, check=None, order=0, tip=""):
    o = Obj(name, False, count, apply=apply, check=check, order=order, tip=tip)
    SCHEMA["objs"].append(o)
    SCHEMA["cur"] = o
    return o


def state(name, count=1, get=None, tip=""):
    o = Obj(name, True, count, get=get, tip=tip)
    SCHEMA["objs"].append(o)
    SCHEMA["cur"] = o
    return o


def cfg(name, typ, default, when="always", tip=""):
    o = SCHEMA["cur"]
    assert not o.state, "cfg() in a state object"
    o.fields.append(Field(name, typ, "cfg", default, when, tip))


def st(name, typ, when="always", tip=""):
    o = SCHEMA["cur"]
    assert o.state, "st() in a config object"
    o.fields.append(Field(name, typ, "state", None, when, tip))


def action(name, fn, when="always", tip=""):
    o = SCHEMA["cur"]
    assert not o.state, "action() belongs to a config object"
    o.fields.append(Field(name, Action, "action", None, when, tip, fn=fn))


DSL = {k: v for k, v in globals().items() if k in (
    "Enum U8 U16 U32 U64 Bool Str PortMask Scaled obj state cfg st action".split())}


def load_schema(d):
    for fn in sorted(os.listdir(d)):
        if fn.endswith(".py"):
            p = os.path.join(d, fn)
            exec(compile(open(p).read(), p, "exec"), dict(DSL, PORTS="ports"))
    objs = SCHEMA["objs"]
    seen = set()
    for o in objs:
        key = ("state." if o.state else "") + o.name
        if key in seen:
            raise SystemExit("object %s declared twice" % key)
        seen.add(key)
        names = set()
        for f in o.fields:
            if f.name in names:
                raise SystemExit("%s.%s declared twice" % (o.name, f.name))
            names.add(f.name)
            check_default(o, f)
    return objs


def check_default(o, f):
    t, d = f.typ, f.default
    if f.access != "cfg":
        return
    where = "%s.%s" % (o.name, f.name)
    if t.code in ("BOOL",):
        if d not in (True, False):
            raise SystemExit("%s: default must be True/False" % where)
    elif t.code in ("U8", "U16", "U32"):
        if isinstance(d, str):
            return          # a C expression
        if not (t.lo <= d <= t.hi):
            raise SystemExit("%s: default %r outside %d-%d" % (where, d, t.lo, t.hi))
    elif t.code == "ENUM":
        if d not in t.words:
            raise SystemExit("%s: default %r not one of %s" % (where, d, " ".join(t.words)))
    elif t.code == "STR":
        if len(d) > t.arg:
            raise SystemExit("%s: default longer than %d" % (where, t.arg))


# ---- the registry ----------------------------------------------------------

def read_registry(path):
    reg = {"obj": {}, "field": {}, "lines": []}
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
        elif s[0] in ("field", "retired") and len(s) >= 3:
            oid, fid = (int(x) for x in s[1].split("."))
            reg["field"][s[2]] = (oid, fid, s[0] == "retired", s[3] if len(s) > 3 else "")
        else:
            raise SystemExit("%s:%d: bad registry line" % (path, n))
    return reg


def apply_registry(objs, reg, path, register):
    """Give every config object and field its ID; with register, add new ones."""
    added = []
    used_obj = set(reg["obj"].values())
    for o in objs:
        if o.state:
            continue
        if o.name not in reg["obj"]:
            if not register:
                raise SystemExit("schema: object %s has no ID: run tools/om_gen.py --register" % o.name)
            oid = max(used_obj | {0}) + 1
            reg["obj"][o.name] = oid
            used_obj.add(oid)
            added.append("obj %d %s" % (oid, o.name))
        o.id = reg["obj"][o.name]
        used = {fid for (oid, fid, _, _) in reg["field"].values() if oid == o.id}
        for f in o.fields:
            if f.access != "cfg":
                continue
            path_ = "%s.%s" % (o.name, f.name)
            r = reg["field"].get(path_)
            if r is None:
                if not register:
                    raise SystemExit("schema: %s has no ID: run tools/om_gen.py --register" % path_)
                fid = max(used | {0}) + 1
                used.add(fid)
                reg["field"][path_] = (o.id, fid, False, f.typ.sig())
                added.append("field %d.%d %s %s" % (o.id, fid, path_, f.typ.sig()))
                r = reg["field"][path_]
            oid, fid, retired, sig = r
            if retired:
                raise SystemExit("schema: %s was retired; a new field needs a new name" % path_)
            if sig and sig != f.typ.sig():
                raise SystemExit("schema: %s changed type %s -> %s: retire it and add a new field"
                                 % (path_, sig, f.typ.sig()))
            f.id = fid
    # every registered, unretired field must still exist
    have = {"%s.%s" % (o.name, f.name) for o in objs if not o.state for f in o.fields}
    for p, (oid, fid, retired, sig) in reg["field"].items():
        if not retired and p not in have:
            raise SystemExit("schema: %s (%d.%d) is registered but gone: "
                             "mark it 'retired' in %s" % (p, oid, fid, path))
    if added:
        with open(path, "a") as fp:
            if not reg["lines"]:
                fp.write("# The object model's stable IDs (DESIGN.md 10.5): appended by\n"
                         "# tools/om_gen.py --register, never edited or reused.\n"
                         "#   obj <id> <object>          field <obj>.<id> <path> <type>\n"
                         "#   retired <obj>.<id> <path>  (its ID is never handed out again)\n")
            for a in added:
                fp.write(a + "\n")
                print("registered:", a)


# ---- C output ----------------------------------------------------------------

def c_default(o, f):
    t, d = f.typ, f.default
    if t.code == "BOOL":
        return "1" if d else "0"
    if t.code == "ENUM":
        return "%d" % t.words.index(d)
    if t.code == "STR":
        return '"%s"' % d
    if t.code == "PMASK":
        return d if isinstance(d, str) else "0x%04x" % d
    return str(d)


def gen(objs, outdir):
    enums = SCHEMA["enums"]
    ename = list(enums)
    h, c = [], []
    H = h.append
    C = c.append
    H("/* generated by tools/om_gen.py from schema/: do not edit */")
    H("#ifndef OM_GEN_H")
    H("#define OM_GEN_H")
    H("#include <stdint.h>")
    H('#include "om.h"')
    H("")
    H("#define OM_PORTS 9\t/* logical ports 0..8: one layout for every board */")
    H("#define OM_N_OBJ %d" % len(objs))
    for i, o in enumerate(objs):
        H("#define O_%s %d" % (o.cname.upper(), i))
    H("")
    for e in ename:
        for i, w in enumerate(enums[e]):
            H("#define E_%s_%s %d" % (e.upper(), w.upper(), i))
    H("")
    for o in objs:
        H("struct %s {" % o.cname)
        for f in o.fields:
            t = f.typ
            if t.code == "ACT":
                continue
            if t.code == "STR":
                H("\tchar %s[%d];" % (f.cname, t.size))
            elif t.code == "U64":
                H("\tuint8_t %s[8];" % f.cname)
            else:
                H("\t%s %s;" % (t.ctype, f.cname))
        if not [f for f in o.fields if f.typ.code != "ACT"]:
            H("\tuint8_t none;")
        H("};")
    H("struct cfg {")
    for o in objs:
        if not o.state:
            H("\tstruct %s %s%s;" % (o.cname, o.name, "[OM_PORTS]" if o.count == "ports" else ""))
    H("};")
    H("union om_st {")
    for o in objs:
        if o.state:
            H("\tstruct %s %s;" % (o.cname, o.name))
    H("};")
    H("extern __xdata struct cfg cfg_run, cfg_cand, cfg_roll;")
    H("extern __xdata union om_st om_st;")
    H("extern __code const struct om_obj om_obj[OM_N_OBJ];")
    H("extern __code const om_fn om_fns[];")
    H("")
    fns = []
    for o in objs:
        for fn in (o.apply, o.check, o.get):
            if fn and fn not in fns:
                fns.append(fn)
        for f in o.fields:
            if f.fn and f.fn not in fns:
                fns.append(f.fn)
    for i, fn in enumerate(fns):
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
    # enum words: one string per enum, words NUL-separated, an empty word ends it
    for e in ename:
        # each word its own literal: "\0" before a digit would be an octal escape
        C('static __code const char e_%s[] = %s "";' % (e, " ".join('"%s\\0"' % w for w in enums[e])))
    C("__code const char * __code const om_enum[] = { %s };" %
      ", ".join("e_%s" % e for e in ename) if ename else "")
    C("")
    lims = []

    def lim(t):
        k = (t.lo, t.hi)
        if k not in lims:
            lims.append(k)
        return lims.index(k)
    for o in objs:
        C('static __code const char n_%s[] = "%s";' % (o.cname, o.name))
        for f in o.fields:
            C('static __code const char n_%s_%s[] = "%s";' % (o.cname, f.cname, f.name))
    C("")
    for o in objs:
        C("static __code const struct om_field f_%s[] = {" % o.cname)
        for f in o.fields:
            t = f.typ
            if t.code == "ACT":
                off, arg, li = "0", str(fns.index(f.fn)), 0
            else:
                off = "offsetof(struct %s, %s)" % (o.cname, f.cname)
                arg = str(ename.index(t.enum)) if t.code == "ENUM" else str(t.arg)
                li = lim(t) if t.code in ("U8", "U16", "U32") else 0
            C("\t{ n_%s_%s, %d, T_%s, %s, %s, %d, %d }," %
              (o.cname, f.cname, f.id, t.code, off, arg, li, f.when))
        C("};")
    C("")
    C("__code const struct om_lim om_lim[] = {")
    for lo, hi in lims or [(0, 0)]:
        C("\t{ %dUL, %dUL }," % (lo, hi))
    C("};")
    C("")
    for o in objs:
        if o.state:
            continue
        inits = ["%s" % c_default(o, f) for f in o.fields if f.typ.code != "ACT"]
        C("static __code const struct %s d_%s = { %s };" % (o.cname, o.cname, ", ".join(inits)))
    C("")

    def fi(fn):
        return fns.index(fn) if fn else 0xff
    C("__code const struct om_obj om_obj[OM_N_OBJ] = {")
    for o in objs:
        kind = "OM_STATE" if o.state else "OM_CFG"
        if o.count == "ports":
            kind += " | OM_PORTOBJ"
        base = "0" if o.state else "offsetof(struct cfg, %s)" % o.name
        dflt = "0" if o.state else "(__code const uint8_t *)&d_%s" % o.cname
        C("\t{ n_%s, %d, %s, %d, f_%s, %s, sizeof(struct %s), %s, %d, %d, %d }," %
          (o.cname, o.id, kind, len(o.fields), o.cname, base, o.cname, dflt,
           fi(o.apply), fi(o.check), fi(o.get)))
    C("};")
    C("")
    C("__code const om_fn om_fns[] = { %s };" % ", ".join(fns) if fns else "")
    C("")
    # layout pins: what the binary codec and rtlsim's read_struct assume (SDCC packs)
    C("#ifndef RTLP_HOST_TEST")
    for o in objs:
        sz = sum(f.typ.size for f in o.fields if f.typ.code != "ACT") or 1
        C("_Static_assert(sizeof(struct %s) == %d, \"%s layout\");" % (o.cname, sz, o.cname))
    C("#endif")

    os.makedirs(outdir, exist_ok=True)
    for name, lines in (("om_gen.h", h), ("om_gen.c", c)):
        p = os.path.join(outdir, name)
        text = "\n".join(lines) + "\n"
        if not os.path.exists(p) or open(p).read() != text:
            with open(p, "w") as fp:
                fp.write(text)
    return fns


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-s", "--schema", default="schema")
    ap.add_argument("-o", "--out")
    ap.add_argument("--register", action="store_true")
    a = ap.parse_args()
    objs = load_schema(a.schema)
    reg = os.path.join(a.schema, "registry.txt")
    apply_registry(objs, read_registry(reg), reg, a.register)
    if a.out:
        gen(objs, a.out)
    n_cfg = sum(len(o.fields) for o in objs if not o.state)
    n_st = sum(len(o.fields) for o in objs if o.state)
    print("om_gen: %d objects, %d config/action fields, %d state fields" % (len(objs), n_cfg, n_st))


if __name__ == "__main__":
    main()
