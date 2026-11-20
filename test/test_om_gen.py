#!/usr/bin/env python3
"""test_om_gen.py - tools/om_gen.py: the schema checks and the ID registry.

Each case writes a small schema into a temporary directory and runs the
generator on it: broken schemas must fail with their message; the registry
must give a field added anywhere the next free ID and renumber nothing; a
type change, a removed field and a missing ID must fail the build.
"""
import os
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
     "must be a numbered object"),
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
    ("message quote", 'obj("s")\nmessage("m", "say \\"hi\\"")\n', "printable text without"),
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

print("\n%s" % ("TEST_OM_GEN PASSED" if not fails else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
