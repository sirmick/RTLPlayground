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

print("output")
rc, out, _ = gen(BASE, registry=reg, out=True)
check(rc == 0, "generates", out)

print("\n%s" % ("TEST_OM_GEN PASSED" if not fails else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
