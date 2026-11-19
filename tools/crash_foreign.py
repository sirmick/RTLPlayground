#!/usr/bin/env python3
"""
crash_foreign.py - a crash record another image left in xdata is ignored at
boot (E0; silicon.md, phase DB), in rtlsim.

  tools/crash_foreign.py FW_BOOT_IMAGE FW_MAP SCHEDTEST_BOOT_IMAGE [--ovs DIR] [--board FILE]

C1: schedtest is reset with ^R ^R, which leaves a valid record at 0xbfe0.
    That record is put in the firmware's xdata, and the firmware chip-reset
    (xdata kept, as on silicon after a web update): its boot prints no
    "Last reset:" and keeps nothing (crash_last).
C2: the firmware's own record (^R ^R on it) is still printed at the next
    boot, and kept.
Exit status 1 if either fails (C1 is expected to fail before E0: the boot
decoded the foreign record with its own slot and state names).
"""
import argparse, os, sys

CRASH_ADDR, CRASH_MAGIC = 0xbfe0, 0x5c4d
PROMPT = r"\n> "


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("fw")
    ap.add_argument("fw_map")
    ap.add_argument("schedtest")
    ap.add_argument("--ovs", default=os.path.expanduser("~/src/ovs-rtlpg"))
    ap.add_argument("--board", default=None)
    a = ap.parse_args(argv)
    board = a.board or os.path.join(a.ovs, "boards", "keeplink_kp_9000_9xhpml_v1_2.json")
    sys.path.insert(0, os.path.join(a.ovs, "tests"))
    from rtlsim_ctl import Sim

    def ctrl_r(s):
        """^R ^R, then the record the reset left"""
        s.uart_write(b"\x12\x12")
        s.run(100)
        return s.peek(f"x:{CRASH_ADDR:#x}", 32)

    def boot(s):
        if s.run_until_uart(PROMPT, max_ms=60000) is None:
            raise SystemExit("no prompt")
        s.run(50)
        return s.uart_text()

    fails = []
    with Sim(image=a.schedtest, board=board) as s:
        boot(s)
        rec = ctrl_r(s)
    magic = rec[0] | rec[1] << 8
    print(f"schedtest's record: magic {magic:#06x}, cause {rec[2]}, image {rec[27]:02x}{rec[26]:02x}{rec[25]:02x}{rec[24]:02x}")
    if magic != CRASH_MAGIC:
        raise SystemExit("schedtest left no record")

    with Sim(image=a.fw, board=board, map=a.fw_map) as s:
        boot(s)
        s.poke(f"x:{CRASH_ADDR:#x}", rec)
        s.reset()
        out = boot(s)
        kept = s.peek("x:_crash_last", 2)
        ok = "Last reset:" not in out and (kept[0] | kept[1] << 8) != CRASH_MAGIC
        line = next((l for l in out.splitlines() if "Last reset:" in l), "")
        print(f"C1 schedtest's record at the firmware's boot: {'ignored: PASS' if ok else 'FAIL'} {line}")
        if not ok:
            fails.append("C1")

        own = ctrl_r(s)
        out = boot(s)
        kept = s.peek("x:_crash_last", 2)
        line = next((l for l in out.splitlines() if "Last reset:" in l), "")
        ok = line.startswith("Last reset: ^R ^R in ") and (kept[0] | kept[1] << 8) == CRASH_MAGIC
        print(f"C2 the firmware's own record: {'printed: PASS' if ok else 'FAIL'} {line}")
        if not ok:
            fails.append("C2")
    print("crash_foreign: " + ("FAIL " + " ".join(fails) if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
