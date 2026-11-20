#!/usr/bin/env python3
"""test_check_cross_bank.py - tools/check_cross_bank.py on made-up linked
listings: a __code address (a string literal, a named __code const) handed
to a function in another bank fails the check; in the same bank, to a
resident function, or with another call between, it passes.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "tools", "check_cross_bank.py")
fails = 0


def check(cond, name, detail=""):
    global fails
    print(("PASS  " if cond else "FAIL  ") + name)
    if not cond:
        fails += 1
        if detail:
            print("      " + detail.strip().replace("\n", "\n      "))


def run(load, callee_at, data_at=0x34100, between=""):
    """a handler at 0x34000 (bank 3) loads `load` (___str_0 at data_at, or
    _msgs, a __code const there) into dptr and names _om_fail (at
    callee_at) as a banked call's target"""
    d = tempfile.mkdtemp()
    with open(os.path.join(d, "rtlplayground.map"), "w") as f:
        f.write("C:   %08X  _om_fail                           om\n" % callee_at)
        f.write("C:   %08X  _handler                           h\n" % 0x34000)
        f.write("C:   %08X  _msgs                              h\n" % data_at)
    with open(os.path.join(d, "om.rst"), "w") as f:
        f.write("                                      1 ;\tfunction om_fail\n"
                "      %06X                          2 _om_fail:\n" % callee_at)
    with open(os.path.join(d, "h.rst"), "w") as f:
        f.write("                                      1 ;\tfunction handler\n"
                "      034000                          2 _handler:\n"
                "      034000 90 41 00         [24]    3 \tmov\tdptr,#%s\n" % load
                + between +
                "      034003 78 81            [12]    4 \tmov\tr0,#_om_fail\n"
                "      034005 79 D9            [12]    5 \tmov\tr1,#(_om_fail >> 8)\n"
                "      034007 7A 03            [12]    6 \tmov\tr2,#(_om_fail >> 16)\n"
                "      034009 12 00 00         [24]    7 \tlcall\t__sdcc_banked_call\n"
                "      %06X                         20 ___str_0:\n" % data_at)
    r = subprocess.run([sys.executable, TOOL, d], capture_output=True, text=True)
    return r.returncode, r.stdout


st, out = run("___str_0", 0x14000)
check(st != 0 and "HANDED TO A FUNCTION IN ANOTHER BANK" in out and "___str_0 (bank 3) to _om_fail (bank 1)" in out,
      "a string literal handed to a function in another bank fails", out)
st, out = run("_msgs", 0x14000)
check(st != 0 and "_msgs (bank 3) to _om_fail (bank 1)" in out,
      "a named __code const handed to a function in another bank fails", out)
st, out = run("___str_0", 0x34800)
check(st == 0 and "OK: none handed" in out, "the same bank passes", out)
st, out = run("___str_0", 0x2000)
check(st == 0 and "OK: none handed" in out, "a resident function passes", out)
st, out = run("___str_0", 0x14000,
              between="      034003 12 00 00         [24]    3 \tlcall\t_strcpy\n")
check("HANDED" not in out, "another call between: not seen (documented)", out)

print("\n%s" % ("ALL PASS" if not fails else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
