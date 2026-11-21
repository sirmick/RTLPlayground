#!/usr/bin/env python3
"""
sim_neg.py - schedtest's negative variants: doc/sched/testing.md, layer 2b (B5).

  schedtest/sim_neg.py BASE [--ovs DIR] [--twice] [--only NAME...] [--json FILE]

BASE is the schedtest build directory without the variant suffix
(output/<MACHINE>-compact-schedtest); the variants are BASE-hang,
BASE-overrun and BASE-bad_flash (make schedtest ST_NEG=...). bad_spec and
fall_through are not built: the tests run make's generator rule, and the
compile of the step that can fall off its end, on the host, and expect them
to fail. Each test must see its variant fail exactly as intended. As in
sim_tests.py, every sim test runs in a fresh rtlsim in firmware time, so a
run is deterministic (--twice compares every number). Exit status 1 on any
failure.
"""
import argparse, concurrent.futures, contextlib, fcntl, glob, io, json, os, re, struct, subprocess, sys, tempfile, time, shutil

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim_tests import Suite, Fail, PROMPT

CRASH_ADDR = 0xbfe0
CRASH_MAGIC = 0x5c4d
CAUSE = {"watchdog": 1, "^R ^R": 2, "reset command": 3}
SWDT_TICKS = 1000
TICK_MS = 5
REC = [("magic", "H"), ("cause", "B"), ("resets", "B"), ("tick", "H"), ("slot", "B"), ("state", "B"),
       ("step_tick", "H"), ("sp", "B"), ("bank", "B"), ("pc", "H"), ("ring", "8s"), ("ring_idx", "B"),
       ("spare", "B"), ("image", "I"), ("spare2", "2s"), ("sum", "H")]
LAST_RESET = r"Last reset: [^\n]*\n"


class Neg(Suite):
    def __init__(self, base, variant, ovs):
        super().__init__(f"{base}-{variant}", ovs)
        self.variant = variant
        self.funcs = sorted((int(a, 16), n) for a, n in
                            re.findall(r"^C:\s+([0-9A-F]{8})\s+(_\w+)", open(os.path.join(self.build, "rtlplayground.map")).read(), re.M))

    def where(self, bank, pc):
        """the function holding pc (banked above 0x4000) in the .map"""
        key = pc if pc < 0x4000 else bank << 16 | pc
        name = None
        for a, n in self.funcs:
            if a > key:
                break
            name = n
        return name

    def rec(self, sim):
        raw = sim.peek(f"x:{CRASH_ADDR:#x}", 32)
        r = struct.unpack("<" + "".join(f for _, f in REC), raw)
        r = dict(zip([f for f, _ in REC], r))
        r["sum_ok"] = r["sum"] == sum(raw[:30]) & 0xffff
        r["valid"] = r["magic"] == CRASH_MAGIC and r["sum_ok"]
        return r

    def to_crash_boot(self, sim, what):
        """from a reset on: the loader, then schedtest's crash_boot; the UART
        text on the way"""
        if sim.run_until_pc("_crash_boot", max_ms=30000) is None:
            raise Fail(f"{what}: no boot within 30 s")
        text = sim.uart_text()
        self.check("RTLPlayground loader" in text and "Starting application" in text,
                   f"{what}: the reset went through the loader")
        return text

    def boot_line(self, sim):
        if sim.run_until_uart(PROMPT, max_ms=30000) is None:
            raise Fail("no prompt after crash_boot")
        m = re.search(LAST_RESET, sim.uart_text())
        return m.group(0).rstrip("\n") if m else None

    def say(self, sim, line):
        """a command's output"""
        self.cmd(sim, line)
        return sim.uart_text()

    def plausible_sp(self, sim, r, what):
        lo = int(re.search(r"^\s+([0-9A-F]{8})\s+__start__stack\b", open(os.path.join(self.build, "rtlplayground.map")).read(), re.M).group(1), 16)
        self.check(lo <= r["sp"] <= 0xff, f"{what}: SP {r['sp']:#x} on the stack ({lo:#x}..0xff)")

    def after_print(self, sim, what, prior):
        r = self.rec(sim)
        self.check(r["magic"] != CRASH_MAGIC, f"{what}: the record is cleared after printing (magic {r['magic']:#x})")
        last = self.u(sim, "crash_last", 2)
        self.check(last == CRASH_MAGIC, f"{what}: kept in crash_last for health")
        p = self.u(sim, "crash_prior", 1)
        self.check(p == prior, f"{what}: crash_prior {p} (want {prior})")

    # ---- hang: the software watchdog ----
    def t_hang(self, sim):
        self.boot(sim)
        self.check(self.u(sim, "crash_last", 2) != CRASH_MAGIC, "a power-on boot finds no record")
        _, a, size = sim.sym("_sl_hang")
        bank = int(sim.sym("_sl_hang")[0][1:])
        for n in (1, 2):
            sim.uart_write("hang\r")
            t0 = sim.run_until_pc("_sl_hang", max_ms=1000)
            c = self.u(sim, "swdt_cnt")         # ticks left since the kick at the pass's start
            t1 = sim.run_until_pc("_crash_die", max_ms=8000)
            if t0 is None or t1 is None:
                raise Fail(f"hang {n}: SL_HANG at {t0}, the watchdog at {t1}")
            dt = (t1 - t0) / 1000
            self.to_crash_boot(sim, f"hang {n}")
            r = self.rec(sim)
            age = (r["tick"] - r["step_tick"]) & 0xffff
            self.record(**{f"hang{n}_ms": round(dt, 3), f"hang{n}_left": c, f"hang{n}_pc": r["pc"],
                           f"hang{n}_sp": r["sp"], f"hang{n}_age": age, f"hang{n}_resets": r["resets"]})
            self.check(SWDT_TICKS - 1 <= c <= SWDT_TICKS, f"hang {n}: SL_HANG began {SWDT_TICKS - c} tick(s) after the kick")
            self.check((c - 1) * TICK_MS < dt <= c * TICK_MS,
                       f"hang {n}: the watchdog fired at the {c}th tick, {dt:.3f} ms after SL_HANG began (5 s - a tick at most)")
            self.check(r["valid"], f"hang {n}: a valid record at 0xbfe0 after the loader (magic {r['magic']:#x}, sum ok {r['sum_ok']})")
            self.check(r["cause"] == CAUSE["watchdog"], f"hang {n}: cause watchdog ({r['cause']})")
            self.check(r["slot"] == self.T["T_SL"] and r["state"] == self.S["S_SL_HANG"],
                       f"hang {n}: slot sl, state SL_HANG ({r['slot']}, {r['state']})")
            # Exact in the sim. On silicon a tick can land between sched.c's
            # sched_now = sched_ticks() and CRASH_NOTE (c = 999, age = 1000):
            # silicon.py accepts age in {c, c + 1}.
            self.check(age == c, f"hang {n}: the step's age at the reset {age} ticks")
            self.check(a <= r["pc"] < a + size and r["bank"] == bank,
                       f"hang {n}: PC {r['pc']:#06x} bank {r['bank']} inside sl_hang ({a:#06x}+{size}, bank {bank})")
            self.plausible_sp(sim, r, f"hang {n}")
            self.check(r["resets"] == n - 1, f"hang {n}: resets before it {r['resets']}")
            line = self.boot_line(sim)
            want = (rf"Last reset: watchdog in sl/SL_HANG after {age * TICK_MS} ms, PC 0x{r['pc']:04x} bank {bank}, "
                    r"states( \w+){8}$")
            self.check(line and re.match(want, line), f"hang {n}: the boot printed it", line)
            self.after_print(sim, f"hang {n}", n)
        h = self.say(sim, "health")
        self.check(f"last reset: watchdog in sl/SL_HANG after {age * TICK_MS} ms" in h, "health shows it")

    # ---- ^R ^R and the reset command ----
    def t_ctrl_r(self, sim):
        self.boot(sim)
        sim.uart_write(b"\x12\x12")
        if sim.run_until_pc("_crash_die", max_ms=100) is None:
            raise Fail("^R ^R: no crash_die within 100 ms")
        self.to_crash_boot(sim, "^R ^R")
        r = self.rec(sim)
        fn = self.where(r["bank"], r["pc"])
        self.record(ctrl_r_pc=r["pc"], ctrl_r_bank=r["bank"], ctrl_r_fn=fn, ctrl_r_sp=r["sp"])
        self.check(r["valid"] and r["cause"] == CAUSE["^R ^R"] and r["resets"] == 0,
                   f"^R ^R: a valid record, cause ^R ^R ({r['cause']}), resets 0")
        self.check(r["pc"] and fn and r["bank"] <= 3, f"^R ^R: the interrupted PC {r['pc']:#06x} bank {r['bank']} is in {fn}")
        self.plausible_sp(sim, r, "^R ^R")
        line = self.boot_line(sim)
        self.check(line and line.startswith("Last reset: ^R ^R in ") and f"PC 0x{r['pc']:04x}" in line,
                   "^R ^R: the boot printed it", line)
        self.after_print(sim, "^R ^R", 1)

        sim.uart_write("reset\r")
        self.to_crash_boot(sim, "reset")
        r = self.rec(sim)
        self.record(reset_sp=r["sp"], reset_bank=r["bank"])
        self.check(r["valid"] and r["cause"] == CAUSE["reset command"] and r["resets"] == 1,
                   f"reset: a valid record, cause reset command ({r['cause']}), resets {r['resets']}")
        self.check(r["pc"] == 0 and r["slot"] == self.T["T_CLI"] and r["state"] == self.S["S_CLI_RUN"],
                   f"reset: PC 0 (not from an ISR), in cli/CLI_RUN ({r['slot']}, {r['state']})")
        self.plausible_sp(sim, r, "reset")
        line = self.boot_line(sim)
        # "after 0 ms" here; silicon.py also takes 5 ms (a tick before CRASH_NOTE)
        self.check(line and re.match(r"Last reset: reset command in cli/CLI_RUN after 0 ms bank \d, states", line),
                   "reset: the boot printed it, without a PC", line)
        self.after_print(sim, "reset", 2)

    # ---- a power cut leaves nothing ----
    def t_powercut(self, sim):
        """A valid record in xdata (the watchdog's, at crash_boot), then the
        power goes instead of a reset: the next boot finds none."""
        self.boot(sim)
        sim.uart_write("hang\r")
        self.to_crash_boot(sim, "hang")
        self.check(self.rec(sim)["valid"], "a valid record before the cut")
        sim.powercut()
        self.boot(sim)
        r = self.rec(sim)
        text = sim.uart_text()
        self.check(not r["valid"], f"after the power cut the record is not valid (magic {r['magic']:#x}, sum ok {r['sum_ok']})")
        self.check(self.u(sim, "crash_last", 2) != CRASH_MAGIC and self.u(sim, "crash_prior", 1) == 0,
                   "nothing kept, crash_prior 0")
        h = self.say(sim, "health")
        self.check("last reset" not in h and "Last reset" not in text, "and nothing printed")

    # ---- overrun ----
    def t_overrun(self, sim):
        self.boot(sim)
        sim.run(2500)
        self.fine(sim)
        ov, st = self.u(sim, "sched_overruns"), self.u(sim, "sched_overrun_state", 1)
        mx = self.st_max_us(sim, "S_SL_SLOW")
        h = self.say(sim, "health")
        m = re.search(r"^SL_SLOW\t(\d+)\t(\d+)ms$", h, re.M)	# over ~45 ms health prints ms
        self.record(overruns=ov, state=st, sl_slow_us=round(mx, 1), health_ms=m and int(m.group(2)))
        self.check(ov == 1 and st == self.S["S_SL_SLOW"], f"one overrun, of SL_SLOW (overruns {ov}, state {st})")
        self.check(54500 <= mx < 56000, f"the step measured ~55 ms: {mx:.0f} us")
        self.check(re.search(r"^overruns 1 last SL_SLOW,", h, re.M), "health names it: overruns 1 last SL_SLOW")
        # its runs are the last second's (decision 26): 1, or 0 once that second has rolled; once is overruns 1
        self.check(m and m.group(1) in ("0", "1") and 54 <= int(m.group(2)) <= 56, "and lists SL_SLOW at ~55 ms, 0-1 runs/s",
                   m and m.group(0))

    # ---- bad_flash ----
    def t_bad_flash(self, sim):
        self.boot(sim)
        sim.uart_write("badflash\r")
        if sim.run_until_pc("_fl_erase", max_ms=200) is None:
            raise Fail("badflash: FL_ERASE never ran")
        bad = []
        for _ in range(50):
            sim.run(1)
            bad = [c for c in sim.checks() if c[1] != "unmodelled"]
            if bad:
                break
        sim.run(50)                       # the erase's 40 ms are over: anything more would show
        bad = [c for c in sim.checks() if c[1] != "unmodelled"]
        m = bad and re.search(r"instruction fetch of \w+ \(flash \w+\) at pc=([0-9a-f]+) psbank=([0-9a-f]+) sp=([0-9a-f]+), t=",
                              bad[0][2])
        fn = m and self.where(int(m.group(2), 16), int(m.group(1), 16))
        self.record(checks=[c[1] for c in bad], fetch_fn=fn, first=bad[0][2] if bad else None)
        self.check(len(bad) == 1 and bad[0][1] == "flash-busy", "exactly one check, flash-busy", bad)
        self.check(m and fn == "_fl_erase", f"a fetch from a bank while busy, with PC/PSBANK/SP/time, in {fn} "
                   "(the step that returned with the erase running)", bad[0][2] if bad else None)

    def make_fails(self, variant, target, msg, what):
        """make of one target of the variant's build fails (2), with msg"""
        mk = re.search(r"-compact", self.build)
        machine = os.path.basename(self.build).split("-compact")[0]
        target = os.path.join(os.path.dirname(self.build), f"{machine}{'-compact' if mk else ''}-schedtest-{variant}",
                              target)
        # --twice runs both suites at once, and their makes would race: in
        # one build directory on html_min (rm -rf, then write) and
        # html_min.list's .tmp (a missing favicon.ico or .tmp, 11 of 40
        # makes from fresh), and in the source tree, whatever the variant,
        # on html_data.c/.h, .layout and the version header. These are the
        # suite's only makes (sim_tests.py runs none): one at a time, under
        # one lock for the whole output directory.
        os.makedirs(os.path.dirname(self.build), exist_ok=True)
        with open(os.path.join(os.path.dirname(self.build), ".sim_neg.lock"), "w") as lk:
            fcntl.flock(lk, fcntl.LOCK_EX)
            r = subprocess.run(["make", "-s", "SCHEDTEST=1", f"ST_NEG={variant}", f"MACHINE={machine}"]
                               + (["LAYOUT=compact"] if mk else []) + [target], capture_output=True, text=True)
        self.record(exit=r.returncode, message=msg in r.stdout + r.stderr)
        self.check(r.returncode == 2, f"make fails ({r.returncode})")
        self.check(msg in r.stdout + r.stderr, what, (r.stdout + r.stderr).strip()[-300:])

    def t_bad_spec(self):
        self.make_fails("bad_spec", os.path.join("sched", "sched_gen.h"),
                        "sched_gen: error: schedtest/bad_spec.c:10: state BAD_LOST is reached by no return/STEP_*/task_claim",
                        "with the generator's message")

    def t_fall_through(self):
        self.make_fails("fall_through", os.path.join("schedtest", "fall_through.rel"),
                        "schedtest/fall_through.c:12: error 59: function 'ft_step' must return value",
                        "with SDCC's error: a step that can fall off its end (decision 22)")

    # variant, test, checker clean, coverage
    TESTS = [("hang", "hang"), ("hang", "ctrl_r"), ("hang", "powercut"), ("overrun", "overrun"),
             ("bad_flash", "bad_flash"), (None, "bad_spec"), (None, "fall_through")]


def run(base, ovs, only):
    suites, results, failures = {}, {}, []
    covdir = tempfile.mkdtemp(prefix="sched-neg-cov-")
    for variant, name in Neg.TESTS:
        if only and name not in only:
            continue
        v = variant or "hang"            # bad_spec, fall_through need no image: any suite will do
        s = suites.get(v)
        if s is None:
            s = suites[v] = Neg(base, v, ovs)
        s.current = name
        t0 = time.time()
        print(f"  {name}" + (f" ({os.path.basename(s.image)})" if variant else ""))
        try:
            if variant is None:
                getattr(s, "t_" + name)()
            else:
                with s.sim() as sim:
                    getattr(s, "t_" + name)(sim)
                    if name != "bad_flash":
                        s.checks_clean(sim, name)
                    if variant == "hang":
                        sim.cov_dump(os.path.join(covdir, name + ".cov"))
                if name == "bad_flash":
                    s.check(sim.p.returncode == 6, f"rtlsim exits 6, the flash busy rule ({sim.p.returncode})")
                    s.record(exit=sim.p.returncode)
        except (Fail, s.ctl.SimError) as e:
            s.failures.append(f"{name}: {e}")
            print(f"    FAIL {e}")
        print(f"    ({time.time() - t0:.1f} s wall)")
    if "hang" in suites and not only:
        coverage(suites["hang"], covdir)
    shutil.rmtree(covdir, ignore_errors=True)
    for s in suites.values():
        results.update(s.results)
        failures += s.failures
    return results, failures


def coverage(s, covdir):
    """crash.c over the hang variant's tests: every line ran (crash_die's
    ISR and main-code paths, crash_reset, crash_sum); crash_boot's lines in
    sched_health.c reported."""
    s.current = "coverage"
    print("  coverage")
    merged = {}
    for f in sorted(glob.glob(os.path.join(covdir, "*.cov"))):
        data = open(f, "rb").read()
        p = 8
        while p < len(data):
            key, bits = data[p:p + 4], data[p + 4:p + 4 + 8192]
            merged[key] = bytes(a | b for a, b in zip(merged.get(key, bytes(8192)), bits))
            p += 4 + 8192
    path = os.path.join(covdir, "all.cov.merged")
    with open(path, "wb") as f:
        f.write(b"RTLCOV1\n" + b"".join(k + v for k, v in sorted(merged.items())))
    tool = [sys.executable, os.path.join(s.ovs, "tools", "cov_report.py"), path,
            "--map", os.path.join(s.build, "rtlplayground.map"), "--region", "loaded"]
    r = subprocess.run(tool + ["--require-lines", "crash.c"], capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip().splitlines()
    s.record(crash_c=out[-1] if out else "")
    s.check(r.returncode == 0, f"every line of crash.c ran: {out[-1] if out else ''}", (r.stdout + r.stderr).strip()[-400:])
    r = subprocess.run(tool + ["--require-functions", "_crash_boot", "_crash_die", "_crash_reset", "_crash_sum"],
                       capture_output=True, text=True)
    s.check(r.returncode == 0, "crash_boot, crash_die, crash_reset, crash_sum ran", (r.stdout + r.stderr).strip()[-400:])


def _run(base, ovs, only):
    """run(), its output captured"""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        res, fails = run(base, ovs, only)
    return buf.getvalue(), res, fails


def main(argv):
    ap = argparse.ArgumentParser(description="schedtest's negative variants in rtlsim (B5)")
    ap.add_argument("base", help="output/<MACHINE>-compact-schedtest; the variants are BASE-<ST_NEG>")
    ap.add_argument("--ovs", default=os.path.expanduser("~/src/ovs-rtlpg"))
    ap.add_argument("--twice", action="store_true", help="run twice and compare every number")
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--json", help="write the measured numbers here")
    a = ap.parse_args(argv)
    t0 = time.time()
    print(f"sim_neg: {a.base}-{{hang,overrun,bad_flash,bad_spec,fall_through}}")
    with concurrent.futures.ProcessPoolExecutor(2) as pool:     # --twice: both runs at once
        runs = [pool.submit(_run, a.base, a.ovs, a.only) for _ in range(2 if a.twice else 1)]
        out, res, fails = runs[0].result()
        sys.stdout.write(out)
        if a.twice:
            out, res2, fails2 = runs[1].result()
            print("second run:")
            sys.stdout.write(out)
            fails += [f"(second run) {f}" for f in fails2]
        same = json.dumps(res, sort_keys=True) == json.dumps(res2, sort_keys=True)
        print(f"  deterministic: {'yes, every number the same' if same else 'NO'}")
        if not same:
            fails.append("the two runs differ")
            for k in res:
                if res[k] != res2.get(k):
                    print(f"    {k}: {res[k]} != {res2.get(k)}")
    if a.json:
        with open(a.json, "w") as f:
            json.dump(res, f, indent=1, sort_keys=True)
    print(f"sim_neg: {len(fails)} failures, {time.time() - t0:.1f} s wall")
    for f in fails:
        print(f"  FAIL {f}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
