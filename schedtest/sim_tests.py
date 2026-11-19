#!/usr/bin/env python3
"""
sim_tests.py - schedtest in rtlsim: doc/sched/testing.md, layer 2 (B4).

  schedtest/sim_tests.py BUILDDIR [--ovs DIR] [--twice] [--only NAME] [--json FILE]

BUILDDIR is a schedtest build (make schedtest); --ovs the ovs-rtlpg
checkout whose tests/rtlsim_ctl.py and build/rtlsim are used (default
~/src/ovs-rtlpg). Each test boots the -boot image (loader, then schedtest)
in a fresh simulator and drives it over the control socket in firmware
time, so a run is deterministic: --twice runs everything twice and fails
unless every measured number is the same. Exit status 1 on any failure.

Times: the firmware logs (tick, fine units) pairs; a fine unit is 64 Timer
2 counts, 6.144 us, and sched_fine_per_tick (813) make a tick.
"""
import argparse, concurrent.futures, contextlib, glob, io, json, math, os, re, shutil, struct, subprocess, sys, tempfile, time

FINE_US = 6.144
PROMPT = r"\n> $"
EV_DEFINED = 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0020 | 0x0040 | 0x0080 | 0x0100 | 0x0200 | 0x0400 \
    | 0x0800 | 0x1000 | 0x2000 | 0x8000     # sched.h's events, schedtest's two, EV_FOREVER
MAX_WAIT = 0x7fff
LONGEST_SLEEP = 202      # ticks: sleep_c's 1000 ms, the longest finite wait in schedtest
LATE_MAX = 100           # ticks a due task may wait for its position (a long CLI_RUN)
LOG_N = 64


class Fail(Exception):
    pass


class Suite:
    def __init__(self, build, ovs):
        sys.path.insert(0, os.path.join(ovs, "tests"))
        import rtlsim_ctl
        self.ctl = rtlsim_ctl
        self.build, self.ovs = build, ovs
        imgs = sorted(glob.glob(os.path.join(build, "rtlplayground-boot-schedtest*.bin")), key=os.path.getmtime)
        if not imgs:
            sys.exit(f"sim_tests: no -boot schedtest image in {build} (make schedtest)")
        self.image = imgs[-1]
        hdr = open(os.path.join(build, "sched", "sched_gen.h")).read()
        enums = re.findall(r"enum \{(.*?)\};", hdr, re.S)
        self.S = {n: i for i, n in enumerate(re.findall(r"\b(S_\w+)", enums[0]))}
        tn = re.findall(r"\b(T_\w+)( = T_FIXED)?", enums[1])
        self.T, i = {}, 0
        for n, alias in tn:
            if alias:
                i = self.T["T_FIXED"]
            self.T[n] = i
            i += 1
        self.slots = self.T["T_SLOTS"]
        self.results, self.failures = {}, []
        self.covdir = tempfile.mkdtemp(prefix="sched-cov-")
        self.steps = re.findall(r"^uint8_t (\w+)\(void\) __banked;", open(os.path.join(build, "sched", "sched_gen.c")).read(), re.M)

    # ---- plumbing ----
    def sim(self):
        return self.ctl.Sim(self.image, board=os.path.join(self.ovs, "boards", "keeplink_kp_9000_9xhpml_v1_2.json"),
                            map=os.path.join(self.build, "rtlplayground.map"),
                            rtlsim=os.path.join(self.ovs, "build", "rtlsim"), timeout=120)

    def boot(self, sim):
        if sim.run_until_uart(PROMPT, max_ms=30000) is None:
            raise Fail("no prompt within 30 s of boot (the switch init takes ~11)")
        sim.uart_text()

    def cmd(self, sim, line, until=PROMPT, ms=5000):
        sim.uart_text()
        sim.uart_write(line + "\r")
        t = sim.run_until_uart(until, max_ms=ms)
        if t is None:
            raise Fail(f"{line!r}: nothing matching {until!r} within {ms} ms")
        return t

    @staticmethod
    def u(sim, name, size=2):
        return int.from_bytes(sim.peek_sym(name, size), "little")

    def u16_at(self, sim, name, index):
        space, addr, _ = sim.sym(name)
        return int.from_bytes(sim.peek(f"{space}:{addr + 2 * index:#x}", 2), "little")

    def log(self, sim, name):
        n, gmin, gmax = struct.unpack("<3H", sim.peek_sym(name, 6))
        space, addr, _ = sim.sym(name)
        raw = sim.peek(f"{space}:{addr + 6:#x}", 6 * LOG_N)
        tick = struct.unpack(f"<{LOG_N}H", raw[:2 * LOG_N])
        cnt = struct.unpack(f"<{LOG_N}H", raw[2 * LOG_N:4 * LOG_N])
        aux = struct.unpack(f"<{LOG_N}H", raw[4 * LOG_N:])
        first = max(0, n - LOG_N)
        entries = [(tick[k % LOG_N], cnt[k % LOG_N], aux[k % LOG_N]) for k in range(first, n)]
        return dict(n=n, gap_min=gmin, gap_max=gmax, entries=entries)

    def fine(self, sim):
        self.per_tick = self.u(sim, "sched_fine_per_tick")

    def span_us(self, a, b):
        """from entry a to entry b, (tick, cnt) each, in us"""
        dt = (b[0] - a[0]) & 0xffff
        return (dt * self.per_tick + b[1] - a[1]) * FINE_US

    def pass_max_us(self, sim):
        return self.u(sim, "sched_pass_max") * FINE_US

    def st_max_us(self, sim, state):
        return self.u16_at(sim, "sched_st_max", self.S[state]) * FINE_US

    def checks_clean(self, sim, what):
        bad = [c for c in sim.checks() if c[1] != "unmodelled"]
        self.check(not bad, f"{what}: the simulator's checks are clean", bad[:3])

    def check(self, cond, what, detail=None):
        tag = "ok  " if cond else "FAIL"
        print(f"    {tag} {what}" + ("" if cond or detail is None else f": {detail}"))
        if not cond:
            self.failures.append(f"{self.current}: {what}" + ("" if detail is None else f": {detail}"))

    def record(self, **kv):
        self.results.setdefault(self.current, {}).update(kv)

    def invariants(self, sim, what):
        """At a pass end: every slot's state valid, masks of defined bits only,
        no wake beyond the horizon; then sched_posted empty at the next pass's
        first step (the snapshot took it)."""
        sim.run_until_pc("_sched_m_pass_end", max_ms=1000)
        tasks = sim.read_struct("sched_task", [("state", "B"), ("mask", "H"), ("wake", "H"), ("arg", "H")], count=self.slots)
        now = self.u(sim, "sched_now")
        def wake_ok(t):
            ahead, behind = (t["wake"] - now) & 0xffff, (now - t["wake"]) & 0xffff
            limit = MAX_WAIT if t["mask"] & 0x8000 else LONGEST_SLEEP
            return not t["state"] or ahead <= limit or behind <= LATE_MAX
        bad = [(i, t) for i, t in enumerate(tasks)
               if t["state"] >= len(self.S) - 1 or t["mask"] & ~EV_DEFINED or not wake_ok(t)]
        self.check(not bad, f"{what}: slots valid at a pass end (state < S_COUNT, defined mask bits, "
                   "wake within the task's longest wait)", bad[:3])
        sim.run_until_pc("_sched_m_step_start", max_ms=1000)
        posted = self.u(sim, "sched_posted")
        self.check(posted == 0, f"{what}: sched_posted empty at the next pass's first step", hex(posted))
        return len(bad) == 0 and posted == 0

    # ---- the tests ----
    def t_wake(self, sim):
        self.boot(sim)
        sim.run(5000)
        self.fine(sim)
        pm = self.pass_max_us(sim)
        self.record(pass_max_us=round(pm, 1))
        for name, ms in (("sleep_a", 5), ("sleep_b", 37), ("sleep_c", 1000)):
            lg = self.log(sim, "st_log_" + name)
            gaps = [self.span_us(a, b) for a, b in zip(lg["entries"], lg["entries"][1:])]
            if ms * 1000 < 0xffff * FINE_US:          # the in-target min/max cover every run
                gaps += [lg["gap_min"] * FINE_US, lg["gap_max"] * FINE_US]
            late = (math.ceil(ms / 5) + 1) * 5000 + pm
            self.record(**{f"{name}_runs": lg["n"], f"{name}_gap_min_us": round(min(gaps), 1),
                           f"{name}_gap_max_us": round(max(gaps), 1)})
            self.check(min(gaps) >= ms * 1000, f"{name} ({ms} ms): never early, shortest gap {min(gaps):.0f} us")
            self.check(max(gaps) <= late, f"{name}: late by at most a pass, longest gap {max(gaps):.0f} us <= {late:.0f}")

    def t_waiter(self, sim):
        self.boot(sim)
        sim.run(3000)
        self.fine(sim)
        pm = self.pass_max_us(sim)
        posts = self.log(sim, "st_log_poster")["entries"]
        wl = self.log(sim, "st_log_waiter")["entries"]
        lat, timeouts = [], []
        for i, w in enumerate(wl):
            if w[2] == 1:
                before = [p for p in posts if 0 <= self.span_us(p, w) < 300000]
                if before:
                    lat.append(self.span_us(before[-1], w))
            elif w[2] == 2 and i:
                timeouts.append(self.span_us(wl[i - 1], w))
        self.record(event_wakes=len(lat), latency_max_us=round(max(lat), 1) if lat else None,
                    timeouts=len(timeouts), timeout_gap_min_us=round(min(timeouts), 1) if timeouts else None,
                    timeout_gap_max_us=round(max(timeouts), 1) if timeouts else None, pass_max_us=round(pm, 1))
        self.check(len(lat) >= 5, f"the waiter woke by EV_ST_POKE {len(lat)} times")
        # One pass holds because the waiter comes before the poster in run_seq:
        # the post's tail of pass N plus the head of N+1 up to the waiter run
        # each slot once. In general (another order, an ISR post) it is up to
        # ~2 passes.
        self.check(lat and max(lat) <= pm, f"post -> waiter within one pass: {max(lat):.0f} us <= {pm:.0f}")
        self.check(len(timeouts) >= 5, f"the timeout path: {len(timeouts)} timeouts")
        bound = (math.ceil(100 / 5) + 1) * 5000 + pm
        self.check(timeouts and min(timeouts) >= 100000 and max(timeouts) <= bound,
                   f"timeouts 100 ms, never early: {min(timeouts):.0f}..{max(timeouts):.0f} us (<= {bound:.0f})")

    def t_isr_ev(self, sim):
        self.boot(sim)
        sim.run(100)
        self.fine(sim)
        lats = []
        for ch in "abcdefgh":
            n0 = self.log(sim, "st_log_isr_ev")["n"]
            head = self.u(sim, "con_rx_head", 1)
            sim.uart_write(ch)
            t_byte = sim.run_until("con_rx_head", "!=", head, max_ms=100, width=1)
            t_task = sim.run_until("st_log_isr_ev", "!=", n0, max_ms=100, width=2)
            if t_byte is None or t_task is None:
                raise Fail(f"byte {ch!r}: ISR at {t_byte}, task at {t_task}")
            lats.append(t_task - t_byte)
            sim.run(20)
        lg = self.log(sim, "st_log_isr_ev")
        heads = [e[2] for e in lg["entries"][-8:]]
        self.record(latency_us=lats, heads=heads)
        self.check(lg["n"] >= 8, f"isr_ev ran for each byte ({lg['n']} runs)")
        self.check(max(lats) <= 2000, f"UART byte -> ISR -> EV_CON_RX -> isr_ev within 2 ms: {max(lats)} us")
        self.check(heads == sorted(heads), "and saw the ring head move forward", heads)

    def t_hot(self, sim):
        self.boot(sim)
        self.cmd(sim, "hot on")
        sim.run(2000)
        self.fine(sim)
        lg = self.log(sim, "st_log_hot")
        gaps = [self.span_us(a, b) for a, b in zip(lg["entries"], lg["entries"][1:])] + [lg["gap_max"] * FINE_US]
        longest = max(self.u16_at(sim, "sched_st_max", i) for i in range(1, len(self.S) - 1)) * FINE_US
        seq = open(os.path.join(self.build, "sched", "sequence.txt")).read()
        spacing = int(re.search(r"^hot\s+high\s+\d+\s+(\d+)", seq, re.M).group(1))
        per_pos = 60      # us: a position's own cost, dispatch and measurement around a step
        bound = spacing * (longest + per_pos)
        self.cmd(sim, "hot off")
        self.record(runs=lg["n"], gap_max_us=round(max(gaps), 1), spacing=spacing, longest_step_us=round(longest, 1))
        self.check(lg["n"] > 1000, f"hot ran {lg['n']} times in 2 s")
        self.check(max(gaps) <= bound, f"worst gap {max(gaps):.0f} us <= spacing {spacing} x (longest step {longest:.0f} + {per_pos}) us = {bound:.0f}")

    def t_jobs(self, sim):
        self.boot(sim)
        self.cmd(sim, "jobs")
        sim.run(300)
        dyn = self.slots - self.T["T_FIXED"]
        claimed, full, done = (self.u(sim, n) for n in ("st_jobs_claimed", "st_jobs_full", "st_jobs_done"))
        tasks = sim.read_struct("sched_task", [("state", "B"), ("mask", "H"), ("wake", "H"), ("arg", "H")], count=self.slots)
        self.record(claimed=claimed, full=full, done=done)
        self.check(claimed == dyn and full == 1, f"claims take the {dyn} dynamic slots, then SCHED_NONE ({claimed} claimed, {full} full)")
        self.check(done == dyn, f"every job ran its 5 steps and ended ({done})")
        self.check(all(t["state"] == 0 for t in tasks[self.T["T_FIXED"]:]), "all dynamic slots free again")
        self.check(all(t["state"] for t in tasks[:self.T["T_FIXED"]]), "fixed slots untouched")
        self.cmd(sim, "jobs")
        sim.run(300)
        self.check(self.u(sim, "st_jobs_done") == 2 * dyn, "and a second round reuses them")

    def t_printer(self, sim):
        self.boot(sim)
        self.cmd(sim, "health clear")
        sim.uart_text()
        sim.uart_write("print 3000\r")
        t0 = time.time()
        text, done = "", False
        for _ in range(40):
            if sim.run_until_uart(r"print done\n", max_ms=1000) is not None:
                done = True
            text += sim.uart_text()
            if done:
                break
        self.check(done, "print 3000 finished")
        # the first line comes after the prompt ("> P0 ..."), the rest on their own
        lines = [m.group(0) for m in re.finditer(r"P\d+ [^\n]*", text)]
        want = [f"P{i} the quick brown fox jumps" for i in range(3000)]
        self.fine(sim)
        pr = self.st_max_us(sim, "S_PR_LINE")
        con = self.st_max_us(sim, "S_CON_EDIT")
        self.record(lines=len(lines), pr_line_max_us=round(pr, 1), runs=self.u16_at(sim, "sched_st_runs", self.S["S_PR_LINE"]))
        self.check(lines == want, f"3000 lines, in order, none lost or garbled ({len(lines)} seen)")
        self.check(self.u(sim, "st_print_done") == 3000, "st_print_done 3000")
        self.check(pr < 5000 and con < 5000, f"no printer or console step over 5 ms (PR_LINE {pr:.0f} us, CON_EDIT {con:.0f} us)")

    def t_flash(self, sim):
        self.boot(sim)
        self.cmd(sim, "health clear")
        self.cmd(sim, "flash", until=r"flash: (ok|verify FAILED|no free slot)\n", ms=3000)
        self.fine(sim)
        st, bad = self.u(sim, "st_fl_state", 1), self.u(sim, "st_fl_bad")
        mx = {s: self.st_max_us(sim, s) for s in ("S_FL_ERASE", "S_FL_PROG", "S_FL_VERIFY", "S_FL_DONE")}
        self.record(state=st, bad=bad, **{k + "_max_us": round(v, 1) for k, v in mx.items()})
        self.check(st == 2 and bad == 0, f"erase, program, verify of the sector at 0x40000: ok (state {st}, bad {bad})")
        self.check(mx["S_FL_PROG"] < 5000 and mx["S_FL_VERIFY"] < 5000,
                   f"program and verify steps under 5 ms ({mx['S_FL_PROG']:.0f}, {mx['S_FL_VERIFY']:.0f} us)")
        self.check(mx["S_FL_ERASE"] < 45000, f"the erase, the sanctioned exception: {mx['S_FL_ERASE']:.0f} us")
        self.checks_clean(sim, "flash")

    def t_slow(self, sim):
        self.boot(sim)
        self.cmd(sim, "hang", until=r"hang: build with ST_NEG=hang\n")   # SL_HANG, harmless here
        self.cmd(sim, "health clear")
        self.cmd(sim, "slow")
        sim.run(100)
        self.fine(sim)
        ov, st = self.u(sim, "sched_overruns"), self.u(sim, "sched_overrun_state", 1)
        mx = self.st_max_us(sim, "S_SL_SLOW")
        self.record(overruns=ov, state=st, sl_slow_us=round(mx, 1))
        self.check(ov == 1 and st == self.S["S_SL_SLOW"], f"one overrun, of SL_SLOW (overruns {ov}, state {st})")
        self.check(29500 <= mx < 31000, f"its length measured (slow's default 30 ms): {mx:.0f} us")

    def paste(self, sim, what):
        """500 characters + CR at the UART's rate; (bytes con took, sum ok, overruns)"""
        n0, s0, o0 = self.u(sim, "st_con_rx_n"), self.u(sim, "st_con_rx_sum"), self.u(sim, "con_rx_overruns")
        text = "".join(chr(33 + (i * 7) % 94) for i in range(500))
        sim.uart_write(text + "\r")
        sim.run(500)
        n1, s1, o1 = self.u(sim, "st_con_rx_n"), self.u(sim, "st_con_rx_sum"), self.u(sim, "con_rx_overruns")
        want = (s0 + sum(map(ord, text)) + 13) & 0xffff
        self.record(**{f"{what}_bytes": n1 - n0, f"{what}_overruns": o1 - o0})
        return n1 - n0, s1 == want, o1 - o0

    def t_paste(self, sim):
        """The 256 B RX ring (decision 19): a paste is safe alone and across a
        20 ms step (~230 B arrive meanwhile). A 40 ms erase across a paste may
        lose bytes: accepted, recorded, not asserted."""
        self.boot(sim)
        n, ok, ov = self.paste(sim, "quiet")
        self.check(n == 501 and ok and ov == 0, f"a quiet paste: all 501 bytes taken, none lost ({n}, overruns {ov})")
        self.cmd(sim, "slow 20")
        sim.run_until_pc("_sl_slow", max_ms=100)
        n, ok, ov = self.paste(sim, "step20")
        self.fine(sim)
        mx = self.st_max_us(sim, "S_SL_SLOW")
        self.record(step20_us=round(mx, 1))
        self.check(n == 501 and ok and ov == 0, f"across a {mx / 1000:.1f} ms step: all 501 bytes taken, none lost ({n}, overruns {ov})")
        self.check(19500 <= mx <= 21000, f"that step was ~20 ms ({mx:.0f} us)")
        sim.uart_write("flash\r")
        sim.run_until_pc("_fl_erase", max_ms=200)
        n, ok, ov = self.paste(sim, "erase")
        print(f"    note: a paste across the 40 ms erase: {n} of 501 bytes taken, {ov} overruns (accepted, decision 19)")

    def t_invariants(self, sim):
        self.boot(sim)
        sim.uart_write("jobs\r")
        sim.run(5)
        sim.uart_write("print 50\r")
        ok = sum(self.invariants(sim, f"pass {i}") for i in range(30))
        self.record(passes_checked=30, ok=ok)

    def t_stack(self, sim):
        self.boot(sim)
        self.cmd(sim, "deep 9")
        for i in range(10):              # serial traffic both ways under the deep chain
            sim.uart_write("?\r")          # the usage line
            sim.run(150)
        sim.uart_write("deep 0\r")
        sim.run(1200)                    # sleep_c rescans the paint each second
        hw, dsp, runs = self.u(sim, "st_stack_hw", 1), self.u(sim, "st_deep_sp", 1), self.u(sim, "st_deep_runs")
        paint = sim.peek("i:0x80", 0x80)
        raw_hw = max([0x80 + i for i, b in enumerate(paint) if b != 0xa5 and 0x80 + i >= self.u(sim, "st_stack_paint_from", 1)] or [0])
        self.record(stack_hw=hw, raw_hw=raw_hw, deep_sp=dsp, deep_runs=runs)
        self.check(runs >= 30, f"deep ran {runs} times under the console traffic (~30 ms a round: 20 ms sleep + its spin)")
        self.check(raw_hw == hw, f"st_stack_hw {hw:#x} agrees with the raw paint {raw_hw:#x}")
        self.check(0xff - hw >= 30, f"stack headroom {0xff - hw} B >= 30 (high-water {hw:#x}, deep's bottom SP {dsp:#x})")

    def t_horizon(self, sim):
        """170 s with no input: a wait without timeout (con, cli, ...) reaches its
        horizon (0x7fff ticks, ~164 s) and is pushed on, without running."""
        self.boot(sim)
        sim.run(100)
        runs0 = self.u16_at(sim, "sched_st_runs", self.S["S_CON_EDIT"])
        t0 = sim.read_struct("sched_task", [("state", "B"), ("mask", "H"), ("wake", "H"), ("arg", "H")], count=1)
        sim.run(170000)
        t1 = sim.read_struct("sched_task", [("state", "B"), ("mask", "H"), ("wake", "H"), ("arg", "H")], count=1)
        now = self.u(sim, "sched_now")
        runs1 = self.u16_at(sim, "sched_st_runs", self.S["S_CON_EDIT"])
        ahead = (t1["wake"] - now) & 0xffff
        self.record(con_runs=runs1 - runs0, wake_before=t0["wake"], wake_after=t1["wake"])
        self.check(t1["mask"] & 0x8000 and t1["wake"] != t0["wake"] and 0 < ahead <= MAX_WAIT,
                   f"con's horizon was pushed on (wake {t0['wake']:#x} -> {t1['wake']:#x}, {ahead} ticks ahead)")
        self.check(runs1 == runs0, f"and con did not run for it ({runs1 - runs0} runs)")

    def t_soak(self, sim):
        """10 minutes of firmware time, everything but hot (whose yield never
        lets the scheduler idle) running, stimuli every 10 s; the 16-bit tick
        wraps twice. Sampled at a pass end each time."""
        self.boot(sim)
        stim = ["jobs", "print 100", "flash", "slow", "?", None]
        flashes, inv = 0, 0
        for i in range(60):
            s = stim[i % len(stim)]
            if s:
                sim.uart_write(s + "\r")
                flashes += s == "flash"
            sim.run(10000 - 20)
            inv += self.invariants(sim, f"t={(i + 1) * 10} s") if i % 6 == 5 else 1
        self.fine(sim)
        text = sim.uart_text()
        ok = text.count("flash: ok")
        st_ok = [self.st_max_us(sim, s) < 20000 for s in self.S if s not in
                 ("S_NONE", "S_COUNT", "S_SL_SLOW", "S_FL_ERASE", "S_CLI_RUN", "S_SL_HANG",
                  "S_POSTER_STEP")]       # the poster's one loader_confirm(), a sector erase, at 15 s
        gaps = {n: self.log(sim, "st_log_" + n) for n in ("sleep_a", "sleep_b")}
        crash = self.u(sim, "crash_rec", 2)
        rx_ov = self.u(sim, "con_rx_overruns")
        hw = self.u(sim, "st_stack_hw", 1)
        self.record(flash_ok=ok, jobs_done=self.u(sim, "st_jobs_done"), overruns=self.u(sim, "sched_overruns"),
                    passes_hi=self.u(sim, "sched_passes_hi"), passes_lo=self.u(sim, "sched_passes_lo"),
                    stack_hw=hw, sleep_a_gap_min=gaps["sleep_a"]["gap_min"], sleep_b_gap_min=gaps["sleep_b"]["gap_min"])
        self.check(ok == flashes, f"every flash run verified ({ok} of {flashes})")
        self.check(self.u(sim, "st_jobs_done") == self.u(sim, "st_jobs_claimed"), "every claimed job ended")
        self.check(all(st_ok), "no step over 20 ms but the sanctioned ones (SL_SLOW, FL_ERASE, CLI_RUN printing, the loader confirm)")
        self.check(gaps["sleep_a"]["gap_min"] * FINE_US >= 5000 and gaps["sleep_b"]["gap_min"] * FINE_US >= 37000,
                   "sleep_a/b never early across the whole soak (and both tick wraps)")
        self.check(crash != 0x5c4d and "Last reset" not in text, "no watchdog reset")
        self.check(rx_ov == 0, f"no RX overruns ({rx_ov})")
        self.check(0xff - hw >= 30, f"stack headroom {0xff - hw} B")
        self.checks_clean(sim, "soak")

    def t_probes(self, sim):
        """silicon.py's console commands: peek prints what the simulator reads;
        flashtime times and verifies its programs; erasepeek's busy row is
        rtlsim's 0x00 and its idle row the bank's code, and its reads are the
        one flash-busy finding (the sanctioned bank-window read, a movc from
        flash_erase_peek in the common area), so rtlsim exits 6."""
        self.boot(sim)
        self.fine(sim)
        a1, a2 = sim.sym("sched_fine_per_tick")[1], sim.sym("st_stack_paint_from")[1]
        self.cmd(sim, f"peek {a1} 2 {a2} 1")
        rows = re.findall(r"^x ([0-9a-f]*)$", sim.uart_text(), re.M)
        want = [sim.peek(f"x:{a1:#x}", 2).hex(), sim.peek(f"x:{a2:#x}", 1).hex()]
        self.check(rows == want, f"peek A N A N: a row each, as the simulator reads them ({rows})", want)
        self.cmd(sim, "flashtime", until=r"flashtime done\n> $", ms=3000)
        ft = struct.unpack("<12H", sim.peek_sym("st_ft", 24))
        t = [self.span_us(ft[i:i + 2], ft[i + 2:i + 4]) for i in (0, 4, 8)]
        bad = self.u(sim, "st_ft_bad")
        self.record(ft_erase_us=round(t[0], 1), ft_write4_us=round(t[1], 1), ft_write256_us=round(t[2], 1), ft_bad=bad)
        self.check(bad == 0 and 39900 <= t[0] <= 40500 and t[1] < 1000 and t[2] < 10000,
                   f"flashtime: erase {t[0]:.0f} us, 4 B {t[1]:.0f} us, 256 B {t[2]:.0f} us, {bad} bad")
        self.checks_clean(sim, "flashtime")
        self.cmd(sim, "erasepeek", until=r"erasepeek done\n> $", ms=3000)
        peek, bank = sim.peek_sym("flash_peek", 128), self.u(sim, "st_pk_bank", 1)
        app = open(self.image.replace("rtlplayground-boot-", "rtlplayground-"), "rb").read()
        code = bytes(app[0x4000 + (bank - 1) * 0xC000 + 0x300 * i] for i in range(64))
        bad = [c for c in sim.checks() if c[1] != "unmodelled"]
        funcs = sorted((int(a, 16), n) for a, n in
                       re.findall(r"^C:\s+([0-9A-F]{8})\s+(_\w+)", open(os.path.join(self.build, "rtlplayground.map")).read(), re.M))
        m = bad and re.search(r"movc of 4000 \(flash \w+\) at pc=([0-9a-f]+) psbank=([0-9a-f]+)", bad[0][2])
        fn = m and [n for a, n in funcs if a <= int(m.group(1), 16)][-1]
        self.record(peek_bank=bank, peek_checks=[c[1] for c in bad], peek_fn=fn)
        self.check(peek[:64] == bytes(64), "erasepeek: the busy row reads 0x00 (rtlsim's model of the 8x)", peek[:64].hex())
        self.check(peek[64:] == code, f"erasepeek: the idle row is bank {bank}'s code", peek[64:].hex())
        self.check(len(bad) == 1 and bad[0][1] == "flash-busy" and m and int(m.group(2), 16) == bank
                   and fn == "_flash_erase_peek",
                   f"erasepeek: exactly one finding, flash-busy, a movc from the window in {fn}", bad)

    TESTS = ["wake", "waiter", "isr_ev", "hot", "jobs", "printer", "flash", "slow", "paste", "invariants", "stack",
             "horizon", "soak", "probes"]

    def run_one(self, name):
        """one test in a fresh simulator; what it printed"""
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            self.current = name
            t0 = time.time()
            print(f"  {name}")
            try:
                with self.sim() as sim:
                    getattr(self, "t_" + name)(sim)
                    if name not in ("flash", "soak", "probes"):
                        self.checks_clean(sim, name)
                    sim.cov_dump(os.path.join(self.covdir, name + ".cov"))
                if name == "probes":
                    self.check(sim.p.returncode == 6, f"rtlsim exits 6, the flash busy rule ({sim.p.returncode})")
            except (Fail, self.ctl.SimError) as e:
                self.failures.append(f"{name}: {e}")
                print(f"    FAIL {e}")
            print(f"    ({time.time() - t0:.1f} s wall)")
        return buf.getvalue()

    def start(self, pool, only=None):
        """submit the tests, each a process of its own (they share nothing
        but the coverage directory)"""
        self.only = only
        return [(n, pool.submit(_one, self.build, self.ovs, self.covdir, n))
                for n in self.TESTS if not only or n in only]

    def collect(self, futures):
        """in the tests' order: their output, results, failures; then coverage"""
        for name, f in futures:
            out, res, fails = f.result()
            sys.stdout.write(out)
            self.results.update(res)
            self.failures += fails
        if not self.only:
            self.current = "coverage"
            print("  coverage")
            self.coverage()
        return self.results

    def run(self, only=None, jobs=1):
        with concurrent.futures.ProcessPoolExecutor(jobs) as pool:
            return self.collect(self.start(pool, only))

    def coverage(self):
        """Over all the tests' dumps: every state's step function ran, and
        every line of sched.c (the application: region 'loaded')."""
        merged = {}
        for f in sorted(glob.glob(os.path.join(self.covdir, "*.cov"))):
            data = open(f, "rb").read()
            p = 8
            while p < len(data):
                key, bits = data[p:p + 4], data[p + 4:p + 4 + 8192]
                old = merged.get(key, bytes(8192))
                merged[key] = bytes(a | b for a, b in zip(old, bits))
                p += 4 + 8192
        path = os.path.join(self.covdir, "all.cov.merged")
        with open(path, "wb") as f:
            f.write(b"RTLCOV1\n" + b"".join(k + v for k, v in sorted(merged.items())))
        tool = [sys.executable, os.path.join(self.ovs, "tools", "cov_report.py"), path,
                "--map", os.path.join(self.build, "rtlplayground.map"), "--region", "loaded"]
        r = subprocess.run(tool + ["--require-functions"] + self.steps, capture_output=True, text=True)
        self.check(r.returncode == 0, f"every state's step function ran ({len(self.steps)})", (r.stdout + r.stderr).strip()[-400:])
        r = subprocess.run(tool + ["--require-lines", "sched.c"], capture_output=True, text=True)
        last = (r.stdout + r.stderr).strip().splitlines()
        self.record(sched_c=last[-1] if last else "")
        self.check(r.returncode == 0, f"every line of sched.c ran: {last[-1] if last else ''}", (r.stdout + r.stderr).strip()[-400:])
        for f in ("sched_bank.c", "crash.c", "con_ring.c"):     # not asserted: they have by-design dead lines
            r = subprocess.run(tool + ["--require-lines", f], capture_output=True, text=True)
            out = (r.stdout + r.stderr).strip().splitlines()
            summary = out[-1] if out else ""
            missed = [l.split(": didn't run")[0] for l in out if "didn't run" in l]
            self.record(**{f: summary})
            print(f"    info {summary}" + (f" (missed {', '.join(missed)})" if missed else ""))
        shutil.rmtree(self.covdir, ignore_errors=True)


def _one(build, ovs, covdir, name):
    s = Suite(build, ovs)
    shutil.rmtree(s.covdir, ignore_errors=True)
    s.covdir = covdir
    out = s.run_one(name)
    return out, s.results, s.failures


def main(argv):
    ap = argparse.ArgumentParser(description="schedtest in rtlsim (B4)")
    ap.add_argument("build")
    ap.add_argument("--ovs", default=os.path.expanduser("~/src/ovs-rtlpg"))
    ap.add_argument("--twice", action="store_true", help="run twice and compare every number")
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--json", help="write the measured numbers here")
    ap.add_argument("--jobs", type=int, default=os.cpu_count(), help="tests at once (default: the CPUs)")
    a = ap.parse_args(argv)
    t0 = time.time()
    s = Suite(a.build, a.ovs)
    print(f"sim_tests: {s.image}")
    with concurrent.futures.ProcessPoolExecutor(a.jobs) as pool:
        f1 = s.start(pool, a.only)
        if a.twice:                     # both runs at once: fresh simulators each
            s2 = Suite(a.build, a.ovs)
            f2 = s2.start(pool, a.only)
        res = s.collect(f1)
        fails = list(s.failures)
        if a.twice:
            print("second run:")
            res2 = s2.collect(f2)
            fails += [f"(second run) {f}" for f in s2.failures]
    if a.twice:
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
    print(f"sim_tests: {len(fails)} failures, {time.time() - t0:.1f} s wall")
    for f in fails:
        print(f"  FAIL {f}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
