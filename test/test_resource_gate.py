#!/usr/bin/env python3
"""
Host tests for tools/resource_gate.py (doc/sched/memory.md).

Fixture builds (a .map with the areas, a .mem map, .asm files) in temporary
directories: an unchanged build passes; a deliberately spilling function
(gate/spill.asm: SDCC 4.6's output for gate/spill.c, a non-leaf with 32-bit
locals and a second parameter) fails with its DSEG symbols named; so do a
common-area overflow, a short stack and internal RAM growth without a
baseline update; a bank over 47 KiB only warns.

Then the real builds, if they exist: the 8x schedtest and firmware builds
against the committed baselines. A missing build is reported, not failed.
"""
import contextlib, io, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import resource_gate                                    # noqa: E402

fails = checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    print(("  ok:   " if cond else "  FAIL: ") + what)
    if not cond:
        fails += 1


def area(name, addr, size):
    return (f"Area                                    Addr        Size        Decimal Bytes (Attributes)\n"
            f"--------------------------------        ----        ----        ------- ----- ------------\n"
            f"{name:<36}{addr:08X}    {size:08X} =  {size:10d}. bytes (REL,CON,CODE)\n\n")


def write_build(d, data=105, overlay=8, bits=3, stack=131, common_end=0x396a, banks=(46188, 46202, 45565)):
    with open(os.path.join(d, "rtlplayground.map"), "w") as f:
        f.write(area("HOME", 0, 0x3e1) + area("CSEG", 0x43d, 0x2ef5) + area("XINIT", common_end - 0x16, 0x16))
        for b, used in zip((1, 2, 3), banks):
            f.write(area(f"BANK{b}", 0x4000 + b * 0x10000, used))
    # the .mem map: register bank 0, data, bits, overlay, then the stack to the top
    cells = ["0"] * 8 + ["a"] * data + ["B"] * bits + ["Q"] * overlay
    cells += ["S"] * stack
    cells = (cells + [" "] * 256)[:256]
    rows = [f"0x{r:x}0:|" + "|".join(cells[r * 16:r * 16 + 16]) + "|" for r in range(16)]
    with open(os.path.join(d, "rtlplayground.mem"), "w") as f:
        f.write("Internal RAM layout:\n      0 1 2 3 4 5 6 7 8 9 A B C D E F\n" + "\n".join(rows) + "\n")
        f.write(f"\n16 bit mode initial stack starts at: 0x7d (sp set to 0x7c) with {stack} bytes available.\n")
    with open(os.path.join(d, "main.asm"), "w") as f:
        f.write("\t.area DSEG    (DATA)\n_reg_bit_set_PARM_2:\n\t.ds 1\n_parse_storm_sloc0_1_0:\n\t.ds 4\n"
                "\t.area OSEG    (OVR,DATA)\n")


def run(builddir, baseline, *extra):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = resource_gate.main([builddir, "--baseline", baseline, *extra])
    return rc, out.getvalue()


def test_fixtures():
    with tempfile.TemporaryDirectory() as t:
        base = os.path.join(t, "base.json")
        b = os.path.join(t, "build")
        os.mkdir(b)
        write_build(b)
        rc, _ = run(b, base, "--update")
        rc, out = run(b, base)
        check(rc == 0 and "PASS" in out and "none new" in out, "the baseline build passes")

        # a deliberately spilling function: 8 B of DSEG
        shutil.copy(os.path.join(HERE, "gate", "spill.asm"), b)
        write_build(b, data=113)
        rc, out = run(b, base)
        check(rc == 1, "a spilling function fails the gate")
        check("FAIL internal RAM grew: data 105 -> 113 B" in out, "and says how much")
        check("+ spill.asm _spilling_step_PARM_2: 2  [PARM]" in out
              and "+ spill.asm _spilling_step_x: 4  [local/static]" in out
              and "+ spill.asm _spilling_step_a: 2  [local/static]" in out, "naming each new DSEG symbol")
        check("--update" in out, "and how to update the baseline if it is meant")
        rc, _ = run(b, base, "--update")
        rc, out = run(b, base)
        check(rc == 0, "the baseline updated in the same commit: passes")

        # a second local of a name the module already has
        write_build(b, data=117)
        with open(os.path.join(b, "main.asm"), "a") as f:
            f.write("\t.area DSEG    (DATA)\n_parse_storm_sloc0_2_0:\n\t.ds 4\n\t.area OSEG    (OVR,DATA)\n")
        rc, out = run(b, base)
        check(rc == 1 and "+ main.asm _parse_storm_sloc0: 4  [sloc]" in out, "a repeated name is still listed")
        with open(os.path.join(b, "main.asm")) as f:
            txt = f.read()
        with open(os.path.join(b, "main.asm"), "w") as f:
            f.write(txt[:txt.rindex("\t.area DSEG")])
        os.remove(os.path.join(b, "spill.asm"))

        write_build(b)
        run(b, base, "--update")
        write_build(b, common_end=0x3f92)
        rc, out = run(b, base)
        check(rc == 1 and "FAIL the common area ends at 0x3f92, 18 B above 0x3f80" in out,
              "a common-area overflow fails, with the address")

        write_build(b, stack=110, data=105)
        rc, out = run(b, base)
        check(rc == 1 and "FAIL the stack is 110 B, under 120 B" in out, "a short stack fails")

        write_build(b, overlay=10)
        rc, out = run(b, base)
        check(rc == 1 and "overlay 8 -> 10 B" in out, "overlay growth fails")

        write_build(b, banks=(48300, 46202, 45565))
        rc, out = run(b, base)
        check(rc == 0 and "WARN bank 1 holds 48300 B, over 47 KiB (852 B free)" in out, "a full bank only warns")

        write_build(b, data=100)
        rc, out = run(b, base)
        check(rc == 0 and "can be lowered with --update" in out, "less than the baseline passes, with a note")


def test_builds():
    out = os.path.join(ROOT, "output")
    for build, base in (("KEEPLINK_KP_9000_9XHPML_V1_2-compact-schedtest", "gate-schedtest.json"),
                        ("KEEPLINK_KP_9000_9XHPML_V1_2-compact", "gate-8x.json")):
        d = os.path.join(out, build)
        if not os.path.exists(os.path.join(d, "rtlplayground.mem")):
            print(f"  skip: no build in output/{build} (make schedtest / make ... LAYOUT=compact LOADER=1)")
            continue
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "resource_gate.py"), d,
                            "--baseline", os.path.join(ROOT, "doc", "sched", "baseline", base)],
                           capture_output=True, text=True)
        print(r.stdout, end="")
        check(r.returncode == 0, f"output/{build} passes the gate against {base}")


test_fixtures()
test_builds()
print(f"resource gate: {checks} checks, {fails} failed")
sys.exit(1 if fails else 0)
