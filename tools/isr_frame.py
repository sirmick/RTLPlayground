#!/usr/bin/env python3
"""
isr_frame.py - how many bytes an ISR's prologue pushes, from SDCC's .asm.

  isr_frame.py FILE.asm isr_timer2 isr_serial          print the counts
  isr_frame.py FILE.asm isr_timer2=8 isr_serial=9      check them (exit 1 if not)

crash.h's SWDT_ISR(pushes) and CRASH_FROM_ISR(cause, pushes) find the
interrupted PC at SP - pushes (high byte) and SP - pushes - 1 (low): pushes
must be what the ISR's prologue really pushed when the macro runs. The count
is the run of push instructions at the top of the function.
"""
import re, sys


def pushes(asm, isr):
    lines = asm.splitlines()
    try:
        i = lines.index(f"_{isr}:")
    except ValueError:
        sys.exit(f"isr_frame: no _{isr}: in the .asm")
    n = 0
    for line in lines[i + 1:]:
        s = line.strip()
        if not s or s.startswith(";") or re.match(r"^a?r\d\s*=", s):
            continue                    # comments, the arN = 0x0N equates
        if s.startswith("push"):
            n += 1
            continue
        break
    return n


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    asm = open(argv[0]).read()
    bad = 0
    for arg in argv[1:]:
        isr, _, want = arg.partition("=")
        n = pushes(asm, isr)
        if want and int(want) != n:
            print(f"isr_frame: {isr} pushes {n}, the source says {want}", file=sys.stderr)
            bad = 1
        else:
            print(f"{isr} {n}")
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
