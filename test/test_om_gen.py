#!/usr/bin/env python3
"""test_om_gen.py - tools/om_gen.py: the schema checks and the ID registry.

Each case writes a small schema into a temporary directory and runs the
generator on it: broken schemas must fail with their message; the registry
must give a field added anywhere the next free ID and renumber nothing; a
type change, a removed field and a missing ID must fail the build.
"""
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "..", "tools", "om_gen.py")
fails = 0


def gen(schema, registry=None, register=False, out=False):
    d = tempfile.mkdtemp()
    with open(os.path.join(d, "a.py"), "w") as f:
        f.write(schema)
    if registry is not None:
        with open(os.path.join(d, "registry.txt"), "w") as f:
            f.write(registry)
    args = [sys.executable, GEN, "-s", d]
    if register:
        args.append("--register")
    if out:
        args += ["-o", os.path.join(d, "out")]
    p = subprocess.run(args, capture_output=True, text=True)
    reg = open(os.path.join(d, "registry.txt")).read() if os.path.exists(os.path.join(d, "registry.txt")) else ""
    return p.returncode, p.stdout + p.stderr, reg


def check(cond, msg, detail=""):
    global fails
    print(("  ok:   " if cond else "  FAIL: ") + msg)
    if not cond:
        fails += 1
        if detail:
            print("        " + detail.replace("\n", "\n        "))


BASE = '''obj("port", count=PORTS)
cfg("name", Str(7), "")
cfg("mtu", U16(64, 9000), 1500)
cfg("pvid", U16(0, 4094), 1)
'''

print("broken schemas")
for name, schema, want in [
    ("default out of range", 'obj("p")\ncfg("a", U16(1, 10), 11)\n', "outside 1-10"),
    ("enum default not a word", 'obj("p")\ncfg("a", Enum("e", "x y"), "z")\n', "not one of x y"),
    ("string default too long", 'obj("p")\ncfg("a", Str(2), "abc")\n', "longer than 2"),
    ("field twice", 'obj("p")\ncfg("a", Bool, True)\ncfg("a", Bool, True)\n', "declared twice"),
    ("object twice", 'obj("p")\ncfg("a", Bool, True)\nobj("p")\n', "declared twice"),
    ("bad name", 'obj("p")\ncfg("A", Bool, True)\n', "bad field name"),
    ("enum redeclared", 'obj("p")\ncfg("a", Enum("e", "x y"), "x")\ncfg("b", Enum("e", "x z"), "x")\n',
     "declared twice with different words"),
    ("keyed without max", 'obj("v", keyed=U16(1, 4094))\ncfg("a", Bool, True)\n', "needs max="),
    ("parent not numbered", 'obj("s")\nobj("t", parent="s", count=4)\ncfg("a", Bool, True)\n',
     "must be an object numbered by port or 1-n"),
    ("when off a port object", 'obj("s")\ncfg("a", Bool, True, when="copper")\n', "needs an object numbered by port"),
    ("default entry out of range", 'obj("v", keyed=U16(1, 10), max=4)\ncfg("a", Bool, True)\ndefault_entry(11)\n',
     "default entry 11 outside 1-10"),
    ("default entry, no such field", 'obj("v", keyed=U16(1, 10), max=4)\ncfg("a", Bool, True)\ndefault_entry(1, b=1)\n',
     "sets no field b"),
    ("flags over 16", 'obj("s")\ncfg("a", Flags("f", " ".join("w%d" % i for i in range(17))), "none")\n',
     "more than 16 words"),
    ("flags default", 'obj("s")\ncfg("a", Flags("f", "x y"), "x,z")\n', "not of x y"),
    ("ipv4 default", 'obj("s")\ncfg("a", IPv4, "1.2.3")\n', "is not a.b.c.d"),
    ("mac default", 'obj("s")\ncfg("a", MAC(), "auto")\n', "is not a MAC"),
    ("tz default", 'obj("s")\ncfg("a", TZ(), "+05:20")\n', "is not +hh:mm"),
    ("port default", 'obj("s")\ncfg("a", Port(none=True), "3")\n', "can only be none"),
    ("message twice", 'obj("s")\nmessage("m", "a")\nmessage("m", "b")\n', "declared twice with different text"),
    ("saved config past its sector", 'obj("v", keyed=U16(1, 4094), max=64)\n' +
     "".join('cfg("n%d", Str(31), "")\n' % i for i in range(2)), "more than its sector's 4096"),
    ("a record past a save step", 'obj("s")\n' + "".join('cfg("n%d", Str(35), "")\n' % i for i in range(7)),
     "more than a save step's 256"),
    ("an object past 255 B", 'state("s")\n' + "".join('st("n%d", Str(31))\n' % i for i in range(8)),
     "over 255 for an object"),
    ("a Str past om_txt", 'obj("s")\ncfg("a", Str(48), "")\n', "more than a load's om_txt holds (47)"),
    ("flags past om_txt", 'obj("s")\ncfg("a", Flags("f", " ".join("word%d" % i for i in range(9))), "none")\n',
     "a's text can reach 53 chars"),
    ("message quote", 'obj("s")\nmessage("m", "say \\"hi\\"")\n', "printable text without"),
    ("keyed parent", 'obj("v", keyed=U16(1, 10), max=4)\ncfg("a", Bool, True)\nobj("t", parent="v", count=2)\n'
     'cfg("b", Bool, True)\n', "must be an object numbered by port or 1-n"),
    ("word parent", 'obj("w", count=Enum("e", "x y"))\ncfg("a", Bool, True)\nobj("t", parent="w", count=2)\n'
     'cfg("b", Bool, True)\n', "must be an object numbered by port or 1-n"),
    ("derived not a Str", 'obj("s", derive="f")\ncfg("a", U8(), 0, derived=True)\n', "only a Str"),
    ("derived without derive=", 'obj("s")\ncfg("a", Str(5), "", derived=True)\n', "go together"),
    ("derive= without a derived field", 'obj("s", derive="f")\ncfg("a", Str(5), "")\n', "go together"),
    ("i16 default", 'obj("s")\ncfg("a", I16(-5, 5), -6)\n', "outside -5-5"),
]:
    rc, out, _ = gen(schema, register=True)
    check(rc != 0 and want in out, name, out)

print("the registry")
rc, out, reg = gen(BASE, register=True)
check(rc == 0 and "field 1.1 port.name Str(7)" in reg and "field 1.3 port.pvid U16" in reg,
      "first IDs, in declaration order", reg)

rc, out, _ = gen(BASE.replace('cfg("pvid"', 'cfg("eee", Bool, True)\ncfg("pvid"'), registry=reg)
check(rc != 0 and "port.eee has no ID" in out, "a new field without an ID fails the build", out)

grown = BASE.replace('cfg("mtu"', 'cfg("eee", Bool, True)\ncfg("mtu"')
rc, out, reg2 = gen(grown, registry=reg, register=True)
check(rc == 0 and "field 1.4 port.eee BOOL" in reg2 and reg2.startswith(reg),
      "a field added in the middle takes the next ID; nothing renumbers", reg2)

rc, out, _ = gen(BASE.replace("U16(64, 9000)", "U32(64, 9000)"), registry=reg)
check(rc != 0 and "changed type" in out, "a type change fails (it needs a new field)", out)

rc, out, _ = gen(BASE.replace("Str(7)", "Str(15)"), registry=reg)
check(rc != 0 and "changed type" in out, "a string length change fails", out)

rc, out, _ = gen(BASE.replace("U16(64, 9000)", "U16(68, 16000)"), registry=reg)
check(rc == 0, "a range change keeps the ID", out)

rc, out, _ = gen(BASE.replace('cfg("pvid", U16(0, 4094), 1)\n', ""), registry=reg)
check(rc != 0 and "registered but gone" in out, "a removed field fails until it is retired", out)

retired = reg.replace("field 1.3 port.pvid", "retired 1.3 port.pvid")
rc, out, reg3 = gen(BASE.replace('cfg("pvid", U16(0, 4094), 1)\n', 'cfg("pvid2", U16(0, 4094), 1)\n'),
                    registry=retired, register=True)
check(rc == 0 and "field 1.4 port.pvid2" in reg3, "a retired ID is never handed out again", reg3)

rc, out, _ = gen(BASE, registry=retired)
check(rc != 0 and "was retired" in out, "a retired name can't come back", out)

print("keyed and nested objects: the index is never part of an ID")
NEST = '''obj("port", count=PORTS)
cfg("mtu", U16(64, 9000), 1500)
obj("vlan", keyed=U16(1, 4094), max=32)
cfg("name", Str(15), "")
default_entry(1, name="default")
obj("lag", count=4)
cfg("mode", Enum("m", "off static lacp"), "off")
obj("stp.port", count=PORTS)
cfg("cost", U32(0, 200000000), 0)
obj("storm", parent="port", count=Enum("t", "bcast mcast"))
cfg("rate", U32(0, 1048575), 0)
'''
rc, out, nreg = gen(NEST, register=True, out=True)
check(rc == 0 and all(x in nreg for x in (
    "obj 2 vlan", "field 2.1 vlan.name Str(15)", "obj 3 lag", "field 3.1 lag.mode Enum(m)",
    "obj 4 stp.port", "field 4.1 stp.port.cost U32", "obj 5 port.storm", "field 5.1 port.storm.rate U32")),
      "keyed, numbered and nested objects register by name, without an index", nreg)
check(not [l for l in nreg.splitlines() if not l.startswith("#") and "#" in l],
      "no '#' in a registry entry", nreg)
grown = (NEST.replace("max=32", "max=64").replace("count=4", "count=8")
         .replace('"bcast mcast"', '"bcast mcast ucast umcast"'))
rc, out, reg4 = gen(grown, registry=nreg, out=True)
check(rc == 0 and reg4 == nreg, "more VLANs, LAGs or storm types: no new ID, nothing renumbers", out)

# a path's walk takes one tree's objects of its first letter (om.c obj_first()): at most 12
LETTER = "".join('obj("s%s")\ncfg("a", Bool, True)\n' % c for c in "abcdefghijklm")
rc, out, _ = gen(LETTER.replace('obj("sm")\ncfg("a", Bool, True)\n', ""), register=True, out=True)
check(rc == 0, "12 config objects starting with 's': built", out)
rc, out, _ = gen(LETTER, register=True, out=True)
check(rc != 0 and "13 config objects start with 's', over the 12" in out, "13: the build fails", out)
rc, out, _ = gen(NEST.replace('cfg("rate"', 'cfg("unit", Bool, False)\ncfg("rate"'), registry=nreg)
check(rc != 0 and "port.storm.unit has no ID" in out, "a nested object's new field needs an ID", out)

print("several schema directories")
d1, d2 = tempfile.mkdtemp(), tempfile.mkdtemp()
open(os.path.join(d1, "a.py"), "w").write('obj("p")\ncfg("a", Bool, True)\n')
open(os.path.join(d2, "b.py"), "w").write('obj("q")\ncfg("b", Bool, True)\n')
open(os.path.join(d2, "registry.txt"), "w").write("base 200\n")
p = subprocess.run([sys.executable, GEN, "-s", d1, "-s", d2, "--register"], capture_output=True, text=True)
r1 = open(os.path.join(d1, "registry.txt")).read()
r2 = open(os.path.join(d2, "registry.txt")).read()
check(p.returncode == 0 and "obj 1 p" in r1 and "obj 200 q" in r2 and "q" not in r1,
      "each directory's objects in its own registry; base 200 starts its IDs", p.stdout + p.stderr + r1 + r2)
open(os.path.join(d2, "registry.txt"), "w").write("obj 1 q\nfield 1.1 q.b BOOL\n")
p = subprocess.run([sys.executable, GEN, "-s", d1, "-s", d2], capture_output=True, text=True)
check(p.returncode != 0 and "object ID 1 is" in p.stdout + p.stderr, "an object ID used in two registries fails",
      p.stdout + p.stderr)

print("types")
rc, out, freg = gen('obj("s")\ncfg("f", Flags("f", "a b c d e f g h"), "none")\n', register=True)
w9 = 'obj("s")\ncfg("f", Flags("f", "a b c d e f g h i"), "none")\n'
rc, out, _ = gen(w9, registry=freg)
check(rc != 0 and "changed type Flags(f) -> Flags16(f)" in out, "Flags past 8 words is a new type (a wider field)", out)
rc, out, _ = gen('obj("s")\ncfg("f", Flags("f", "a b c d e f g"), "none")\n', registry=freg)
check(rc == 0, "Flags losing a word keeps the ID", out)

print("output")
rc, out, _ = gen(BASE, registry=reg, out=True)
check(rc == 0, "generates", out)

for t in ("U64", "Scaled(1)"):
    rc, out, _ = gen('obj("s")\ncfg("x", %s, 0)\n' % t)
    check(rc != 0 and "no parser" in out, "a config field of a type om.c doesn't parse (%s): fails the build" % t, out)

print("a type only state has: printed, not parsed (OM_SET_)")
d = tempfile.mkdtemp()
with open(os.path.join(d, "a.py"), "w") as f:
    f.write('obj("s")\ncfg("ip", IPv4, "10.0.0.1")\nstate("s")\nst("gw", IPv4)\nst("off", I16())\n')
subprocess.run([sys.executable, GEN, "-s", d, "--register"], capture_output=True)
p = subprocess.run([sys.executable, GEN, "-s", d, "-o", os.path.join(d, "o")], capture_output=True, text=True)
hh = open(os.path.join(d, "o", "om_gen.h")).read() if p.returncode == 0 else ""
check("#define OM_USE_IPV4 1" in hh and "#define OM_SET_IPV4 1" in hh and
      "#define OM_USE_I16 1" in hh and "#define OM_SET_I16 0" in hh,
      "OM_USE_ for every type, OM_SET_ for a config field's", p.stdout + p.stderr)

print("the web UI's schema (--js)")
d = tempfile.mkdtemp()
with open(os.path.join(d, "a.py"), "w") as f:
    f.write('''obj("port", count=PORTS, tip="A port")
cfg("name", Str(7), "", tip="Its name", label="c_name")
cfg("on", Bool, True, when="copper")
cfg("speed", Enum("sp", "auto 1g"), "auto")
cfg("mtu", U16(64, 9000), 1500, tip="Largest frame", unit="B")
cfg("iso", PortMask(cpu=True), "OM_X")
action("test", "f")
obj("sys")
cfg("ip", IPv4, "192.168.1.1")
state("port", count=PORTS)
st("link", Enum("ln", "down up"))
st("rx", U64, when="copper")
''')
subprocess.run([sys.executable, GEN, "-s", d, "--register"], capture_output=True)
js_path = os.path.join(d, "schema.js")
p = subprocess.run([sys.executable, GEN, "-s", d, "-o", os.path.join(d, "o1"), "--js", js_path],
                   capture_output=True, text=True)
p2 = subprocess.run([sys.executable, GEN, "-s", d, "-o", os.path.join(d, "o2")], capture_output=True, text=True)
same = all(open(os.path.join(d, "o1", n)).read() == open(os.path.join(d, "o2", n)).read()
           for n in ("om_gen.h", "om_gen.c"))
check(p.returncode == 0 and same, "--js leaves om_gen.h and om_gen.c as they are", p.stdout + p.stderr)
txt = open(js_path).read() if os.path.exists(js_path) else ""
m = re.search(r"var SCHEMA = (\{.*\});", txt)
sch = json.loads(m.group(1)) if m else {}
pf = sch.get("port", {}).get("f", {})
check(sch.get("port", {}).get("i") == "P" and "t" not in sch["port"] and "sys" in sch
      and "i" not in sch["sys"] and "state.port" in sch and sch["port"].get("o") and sch["state.port"].get("o", "")[:1] == "s",
      "objects: index kind, no tip (the UI reads help), IDs (state's s<n>), state ones by \"state.\"",
      json.dumps(sch)[:300])
check(list(pf) == ["name", "on", "speed", "mtu", "iso", "test"], "fields in schema order", str(list(pf)))
check(pf.get("name") == ["s", 7, None, "", 0, "", "", "c_name"] and pf.get("on") == ["b", None, None, "on", 1]
      and pf.get("speed") == ["e", "auto 1g", None, "auto"]
      and pf.get("mtu") == ["n", 64, 9000, "1500", 0, "", "B"]
      and set(sch["port"].get("k", {})) == {"name", "on", "speed", "mtu", "iso"},
      "type, limits or words, default, when, no tip, unit, label; field IDs (not the action's)", json.dumps(pf))
check(pf.get("iso") == ["m", 1] and pf.get("test") == ["a"],
      "a C-expression default is null and trailing empties go", json.dumps(pf))
check(sch["sys"]["f"]["ip"] == ["ip", None, None, "192.168.1.1"] and
      sch["state.port"]["f"] == {"link": ["e", "down up"], "rx": ["n", None, None, None, 1]},
      "addresses as text; state fields, no default", json.dumps(sch["state.port"]))
mt = os.path.getmtime(js_path)
subprocess.run([sys.executable, GEN, "-s", d, "--js", js_path], capture_output=True)
check(os.path.getmtime(js_path) == mt, "an unchanged schema.js is not rewritten")
# a state Enum whose words are the build's (sched_gen.py --om): --js alone
# takes it without them, the tables don't
with open(os.path.join(d, "b.py"), "w") as f:
    f.write('state("sys")\nst("slot", Enum("sslotn"))\n')
subprocess.run([sys.executable, GEN, "-s", d, "--register", "--js", js_path], capture_output=True)	# its state IDs
p = subprocess.run([sys.executable, GEN, "-s", d, "--js", js_path], capture_output=True, text=True)
txt = open(js_path).read()
check(p.returncode == 0 and '"state.sys":{"f":{"slot":["e"]}' in txt, "--js: a state Enum the build declares, no words",
      p.stdout + p.stderr + txt[-200:])
p = subprocess.run([sys.executable, GEN, "-s", d, "-o", os.path.join(d, "o3")], capture_output=True, text=True)
check(p.returncode != 0 and "declared nowhere" in p.stdout + p.stderr, "-o: it is declared nowhere", p.stdout + p.stderr)

print("schema constants (const)")
for name, schema, want in [
    ("max names no const", 'obj("v", keyed=U16(1, 4094), max="MAX_V")\ncfg("a", Bool, True)\n',
     "max=MAX_V is no const() declared before it"),
    ("const name", 'const("max_v", 8)\nobj("s")\ncfg("a", Bool, True)\n', "an upper-case name"),
    ("const reserved", 'const("OM_PORTS", 8)\nobj("s")\ncfg("a", Bool, True)\n', "om_gen.h's own names"),
    ("const reserved O_", 'const("O_SYS", 8)\nobj("s")\ncfg("a", Bool, True)\n', "om_gen.h's own names"),
    ("const reserved E_", 'const("E_CFGAPPLY_IDLE", 8)\nobj("s")\ncfg("a", Bool, True)\n', "om_gen.h's own names"),
    ("const twice", 'const("MAX_V", 8)\nconst("MAX_V", 9)\nobj("s")\ncfg("a", Bool, True)\n',
     "declared twice with different values"),
]:
    rc, out, _ = gen(schema, register=True)
    check(rc != 0 and want in out, name, out)
d = tempfile.mkdtemp()
with open(os.path.join(d, "a.py"), "w") as f:
    f.write('''const("MAX_V", 8)
obj("v", keyed=U16(1, 4094), max="MAX_V")
cfg("a", Bool, True)
default_entry(1)
''')
subprocess.run([sys.executable, GEN, "-s", d, "--register"], capture_output=True)
js_path = os.path.join(d, "schema.js")
p = subprocess.run([sys.executable, GEN, "-s", d, "-o", os.path.join(d, "o"), "--js", js_path],
                   capture_output=True, text=True)
hh = open(os.path.join(d, "o", "om_gen.h")).read() if p.returncode == 0 else ""
cc = open(os.path.join(d, "o", "om_gen.c")).read() if p.returncode == 0 else ""
check(p.returncode == 0 and "#define MAX_V 8" in hh and "[MAX_V]" in hh and "{ MAX_V, 1 }" in cc,
      "a const: om_gen.h defines it, C dimensions and tables use its name", p.stdout + p.stderr)
txt = open(js_path).read() if os.path.exists(js_path) else ""
check('"i":"K8"' in txt, "schema.js has its number", txt[:200])

print("the help blob (--help-blob)")


def help_gen(schema, room):
    d = tempfile.mkdtemp()
    with open(os.path.join(d, "a.py"), "w") as f:
        f.write(schema)
    p = subprocess.run([sys.executable, GEN, "-s", d, "--register"], capture_output=True, text=True)
    blob = os.path.join(d, "help.bin")
    p = subprocess.run([sys.executable, GEN, "-s", d, "-o", os.path.join(d, "out"), "--help-blob", blob,
                        "--help-room", str(room)], capture_output=True, text=True)
    h = open(os.path.join(d, "out", "om_gen.h")).read() if p.returncode == 0 else ""
    return p.returncode, p.stdout + p.stderr, open(blob, "rb").read() if p.returncode == 0 else b"", h


def help_row(b, row):
    return struct.unpack_from("<H", b, 2 * row)[0]


def help_text(b, nobj, i):
    """The entry at offset i as om.c's help prints it (tokens expanded), and the next's offset."""
    out = b""
    while b[i]:
        c = b[i]
        out += b[help_row(b, nobj + 1 + c - 0x80):help_row(b, nobj + 2 + c - 0x80)] if c >= 0x80 else bytes([c])
        i += 1
    return out.decode(), i + 1


HS = ('obj("sys", tip="The switch: its name and its LEDs")\n'
      'cfg("name", Str(15), "", tip="The name the switch sends in LLDP and DHCP")\n'
      'cfg("mtu", U16(64, 9000), 1500, tip="The largest frame the switch takes", unit="B")\n'
      'cfg("cal", U16(), 0, tip="", unit="1/256 C")\n'
      'state("sys")\nst("up", U32(), tip="The seconds since the switch started", unit="s")\n')
rc, out, b, h = help_gen(HS, 3582)
check(rc == 0 and "help blob %d B of 3582" % len(b) in out, "a blob within its room", out)
check("#define OM_H_UNITS 3" in h, "om_gen.h: the units' tokens (OM_H_UNITS)", h)
got = []
for row, n in ((0, 4), (1, 2)):  # an object's entry, then its fields'
    i = help_row(b, row)
    for k in range(n):
        t, i = help_text(b, 2, i)
        got.append(t)
want = ["The switch: its name and its LEDs", "The name the switch sends in LLDP and DHCP",
        'unit B "The largest frame the switch takes', 'unit "1/256 C" "',
        "", 'unit s "The seconds since the switch started']
check(got == want, "the entries decode to the tips, a unit's token first", repr(got))
usage, _ = help_text(b, 2, help_row(b, 2))
check(usage.startswith("get [-c|-s|-d] PATH...\nset PATH VALUE\n") and usage.endswith("Tab completes\n"),
      "the verbs' usage after the objects'", repr(usage))
rc, out, b, h = help_gen(HS, 200)
check(rc != 0 and "the help blob's objects, verbs, config and actions are" in out and "over its 200 B" in out
      and " sys\n" in out and "the tips don't fit" in out, "the first four classes past the room fail the build, B per object", out)
# the state fields' tips go in while they fit, in schema order; the rest are left out, listed
ST = HS + "".join('st("s%d", U16(), tip="State field %d: a tip long enough to cost some room")\n' % (i, i) for i in range(12))
rc, out, b, h = help_gen(ST, 3582)
full = len(b)
base = 3582 - int(re.search(r"headroom (\d+) B", out).group(1))     # without the state tips
room = (base + full) // 2
rc, out, b, h = help_gen(ST, room)
m = re.search(r"(\d+) state tips left out \(no room\): (.*)", out)
check(rc == 0 and len(b) <= room and m and m.group(2).split()[-1] == "state.sys.s11"
      and "state.sys.up" not in m.group(2), "state tips past the room: the last left out, listed", out)
t, _ = help_text(b, 2, help_row(b, 1))          # state.sys's entry, then up's: kept
check(help_text(b, 2, _)[0] == 'unit s "The seconds since the switch started', "the first state tips kept")
check(re.search(r"objects \d+, verbs \d+, config \d+, actions \d+, state \d+, tokens \d+, table \d+; headroom \d+ B", out),
      "the bytes by class and the config tips' headroom", out)
rc, out, b, h = help_gen('obj("s", tip="' + "x" * 61 + '")\ncfg("a", Bool, True)\n', 3582)
check(rc == 0 and "warning: s's tip is 61 chars, over 60" in out, "a tip over 60 chars: a warning", out)
rc, out, b, h = help_gen('obj("s", tip=\'say "hi"\')\ncfg("a", Bool, True)\n', 3582)
check(rc != 0 and "printable text without" in out, "a tip with a quote fails", out)
print("apply order: by order=, a child at its parent's, after that order's objects")
d = tempfile.mkdtemp()
with open(os.path.join(d, "a.py"), "w") as f:
    f.write('''obj("p", count=4, order=5)
cfg("a", Bool, True)
obj("c", parent="p", count=2)
cfg("a", Bool, True)
obj("q", order=5)
cfg("a", Bool, True)
obj("z", order=1)
cfg("a", Bool, True)
''')
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import om_gen  # noqa: E402
om_gen.SCHEMA.update({"objs": [], "enums": {}, "msgs": {}, "consts": {}, "cur": None, "dir": None})
names = [o.regname for o in om_gen.load_schema([d])]
check(names == ["z", "p", "q", "p.c"], "z (1), p and q (5), then p's child", str(names))

print("\n%s" % ("TEST_OM_GEN PASSED" if not fails else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
