#!/usr/bin/env python3
"""
silicon.py - schedtest on silicon (C): layer 2's scenario (sim_tests.py) and
layer 2b's hang tests (sim_neg.py), driven from the console alone.

  python3 silicon.py [--port /dev/ttyUSB0] [--only TEST...] [--json FILE]
  python3 silicon.py report [--port ...]        the report, decoded, once
  python3 silicon.py --sim [--ovs DIR] [...]    the same against rtlsim, through a pty

Run the copy in the build directory (output/<M>-compact-schedtest[-hang]/
silicon.py): the build embeds its constants (symbol addresses from the .map,
the state and slot numbers, sl_hang's address, the bank window's bytes, the
version) into it, `make schedtest` does. The copy picks its mode from its
build: the base image runs the layer 2 scenario, the hang image the hang
tests. Python 3 stdlib only; the port is opened raw at 115200 (termios).

There are no peeks on silicon: the firmware's `peek A N [A N..]` prints xdata
in hex, all of a line's ranges in one step, so a line is a snapshot; this
script decodes what it prints the way the simulator's tests decode a peek.
Bounds are sim_tests.py's, with silicon's own pass length; the simulator's
numbers (SIM, from the last sim run) are printed alongside.

Exit status 1 on any FAIL.
"""
import argparse, json, math, os, re, select, struct, sys, termios, time

BUILD = None            # filled in by --embed

FINE_US = 6.144
LOG_N = 64
LOG_SIZE = 6 + 6 * LOG_N
TASK_SIZE = 7           # state, mask, wake, arg
PROMPT = r"(?:^|\n)> "     # anything may follow (deep prints a line each run)
LINE_MAX = 63           # schedtest's line editor
LAST_RESET = r"Last reset: [^\n]*\n"
TICK_MS = 5

# The simulator's numbers for the same image (sim_tests.py --json, sim_neg.py --json)
SIM = {}


def sizes(b):
    sc, slots = len(b["states"]), b["slots"]
    u8 = ("sched_overrun_state", "st_stack_hw", "st_stack_paint_from", "st_sp_step_max", "st_deep_sp",
          "st_fl_state", "st_pk_bank")
    s = {n: 2 for n in b["syms"]}
    s.update({n: 1 for n in u8})
    s.update({n: LOG_SIZE for n in b["syms"] if n.startswith("st_log_")})
    s.update(ticks=4, sched_st_runs=2 * sc, sched_st_max=2 * sc, sched_task=TASK_SIZE * slots,
             st_ft=24, flash_peek=128)
    return s


# ---- the console ----
class Console:
    def __init__(self, port, baud=115200, log=None):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        a = termios.tcgetattr(self.fd)
        a[0] = 0                                   # iflag: raw
        a[1] = 0                                   # oflag
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[3] = 0                                   # lflag: no echo, no canon
        speed = getattr(termios, f"B{baud}")
        a[4] = a[5] = speed
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        self.buf = ""
        self.log = log

    def write(self, data):
        if isinstance(data, str):
            data = data.encode("latin-1")
        while data:
            try:
                n = os.write(self.fd, data)
                data = data[n:]
            except BlockingIOError:
                select.select([], [self.fd], [], 1)
        termios.tcdrain(self.fd)

    def _fill(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return False
        try:
            d = os.read(self.fd, 65536)
        except BlockingIOError:
            return False
        t = d.decode("latin-1").replace("\r", "")
        if self.log:
            self.log.write(t)
            self.log.flush()
        self.buf += t
        return True

    def until(self, regex, timeout):
        """the text up to and including the first match, or None at the timeout"""
        end = time.time() + timeout
        rx = re.compile(regex)
        while True:
            m = rx.search(self.buf)
            if m:
                text, self.buf = self.buf[:m.end()], self.buf[m.end():]
                return text
            left = end - time.time()
            if left <= 0:
                return None
            self._fill(min(left, 0.2))

    def drain(self, quiet=0.3, most=1.0):
        """drop what's pending: until the line is quiet, or for `most` seconds"""
        end = time.time() + most
        while self._fill(quiet) and time.time() < end:
            pass
        t, self.buf = self.buf, ""
        return t

    def cmd(self, line, until=PROMPT, timeout=10):
        self.drain(0.05)
        self.write(line + "\r")
        t = self.until(until, timeout)
        if t is None:
            raise Fail(f"{line!r}: nothing matching {until!r} within {timeout} s (got {self.buf[-200:]!r})")
        return t


class Fail(Exception):
    pass


# ---- the tests ----
class Silicon:
    def __init__(self, con, build):
        self.con, self.b = con, build
        self.S = {n: i for i, n in enumerate(build["states"])}
        self.T = build["T"]
        self.size = sizes(build)
        self.results, self.failures = {}, []
        self.current = None

    def check(self, cond, what, detail=None):
        tag = "PASS" if cond else "FAIL"
        print(f"    {tag} {what}" + ("" if cond or detail is None else f": {detail}"))
        if not cond:
            self.failures.append(f"{self.current}: {what}" + ("" if detail is None else f": {detail}"))
        return cond

    def info(self, what):
        print(f"    info {what}")

    def record(self, **kv):
        self.results.setdefault(self.current, {}).update(kv)

    def sim(self, key):
        """the simulator's number for this test, as ' (sim X)'"""
        v = SIM.get(self.current, {}).get(key)
        return "" if v is None else f" (sim {v})"

    # -- peeks
    def peek(self, *names):
        """one `peek` line, a snapshot: {name: bytes}"""
        want = sorted((self.b["syms"][n], self.size[n], n) for n in names)
        ranges = []
        for a, n, _ in want:
            if ranges and a - (ranges[-1][0] + ranges[-1][1]) <= 64:
                ranges[-1][1] = max(ranges[-1][1], a + n - ranges[-1][0])
            else:
                ranges.append([a, n])
        line = "peek " + " ".join(f"{a} {n}" for a, n in ranges)
        if len(line) > LINE_MAX:
            raise Fail(f"peek {names}: {len(line)} characters, over the line editor's {LINE_MAX}")
        self.peek_line = line + "\r"         # what the console took for it
        t = self.con.cmd(line, timeout=10)
        rows = re.findall(r"^x ([0-9a-f]*)$", t, re.M)
        if len(rows) != len(ranges) or any(len(r) != 2 * n for r, (_, n) in zip(rows, ranges)):
            raise Fail(f"{line!r}: {len(rows)} rows for {len(ranges)} ranges: {t[-300:]!r}")
        out = {}
        for a, n, name in want:
            for (ra, rn), row in zip(ranges, rows):
                if ra <= a < ra + rn:
                    out[name] = bytes.fromhex(row)[a - ra:a - ra + n]
        return out

    @staticmethod
    def u(raw):
        return int.from_bytes(raw, "little")

    def u16s(self, raw):
        return struct.unpack(f"<{len(raw) // 2}H", raw)

    @staticmethod
    def log(raw):
        n, gmin, gmax = struct.unpack("<3H", raw[:6])
        tick = struct.unpack(f"<{LOG_N}H", raw[6:6 + 2 * LOG_N])
        cnt = struct.unpack(f"<{LOG_N}H", raw[6 + 2 * LOG_N:6 + 4 * LOG_N])
        aux = struct.unpack(f"<{LOG_N}H", raw[6 + 4 * LOG_N:])
        first = max(0, n - LOG_N)
        return dict(n=n, gap_min=gmin, gap_max=gmax,
                    entries=[(tick[k % LOG_N], cnt[k % LOG_N], aux[k % LOG_N]) for k in range(first, n)])

    def span_us(self, a, b):
        return (((b[0] - a[0]) & 0xffff) * self.per_tick + b[1] - a[1]) * FINE_US

    def st_max_us(self, raw, state):
        return self.u16s(raw)[self.S[state]] * FINE_US

    def fine(self):
        self.per_tick = self.u(self.peek("sched_fine_per_tick")["sched_fine_per_tick"])

    def clear(self):
        self.con.cmd("clear")

    def wait(self, s):
        """wall time; the console's output meanwhile is dropped"""
        end = time.time() + s
        while time.time() < end:
            self.con._fill(min(0.2, max(0, end - time.time())))
        self.con.buf = ""

    def sync(self):
        for _ in range(3):
            self.con.drain(0.2)
            self.con.write("\r")
            if self.con.until(PROMPT, 3) is not None:
                if self.b["variant"] != "hang":
                    self.fine()
                return
        raise Fail("no prompt: is the schedtest image running?")

    # -- layer 2 (sim_tests.py)
    def t_wake(self):
        self.clear()
        self.wait(5)
        p = self.peek("sched_pass_max", "st_log_sleep_a", "st_log_sleep_b", "st_log_sleep_c")
        pm = self.u(p["sched_pass_max"]) * FINE_US
        self.record(pass_max_us=round(pm, 1))
        self.info(f"longest pass {pm:.0f} us{self.sim('pass_max_us')}")
        for name, ms in (("sleep_a", 5), ("sleep_b", 37), ("sleep_c", 1000)):
            lg = self.log(p["st_log_" + name])
            gaps = [self.span_us(a, b) for a, b in zip(lg["entries"], lg["entries"][1:])]
            if ms * 1000 < 0xffff * FINE_US:
                gaps += [lg["gap_min"] * FINE_US, lg["gap_max"] * FINE_US]
            if not gaps:
                self.check(False, f"{name}: no runs logged")
                continue
            late = (math.ceil(ms / 5) + 1) * 5000 + pm
            self.record(**{f"{name}_runs": lg["n"], f"{name}_gap_min_us": round(min(gaps), 1),
                           f"{name}_gap_max_us": round(max(gaps), 1)})
            self.check(min(gaps) >= ms * 1000, f"{name} ({ms} ms): never early, shortest gap {min(gaps):.0f} us"
                       + self.sim(f"{name}_gap_min_us"))
            self.check(max(gaps) <= late, f"{name}: late by at most a pass, longest gap {max(gaps):.0f} us <= {late:.0f}"
                       + self.sim(f"{name}_gap_max_us"))

    def t_waiter(self):
        self.clear()
        self.wait(3)
        p = self.peek("sched_pass_max", "st_log_waiter", "st_log_poster")
        pm = self.u(p["sched_pass_max"]) * FINE_US
        posts, wl = self.log(p["st_log_poster"])["entries"], self.log(p["st_log_waiter"])["entries"]
        lat, timeouts = [], []
        for i, w in enumerate(wl):
            if w[2] == 1:
                before = [q for q in posts if 0 <= self.span_us(q, w) < 300000]
                if before:
                    lat.append(self.span_us(before[-1], w))
            elif w[2] == 2 and i:
                timeouts.append(self.span_us(wl[i - 1], w))
        self.record(event_wakes=len(lat), latency_max_us=round(max(lat), 1) if lat else None, timeouts=len(timeouts),
                    timeout_gap_min_us=round(min(timeouts), 1) if timeouts else None,
                    timeout_gap_max_us=round(max(timeouts), 1) if timeouts else None, pass_max_us=round(pm, 1))
        self.check(len(lat) >= 5, f"the waiter woke by EV_ST_POKE {len(lat)} times")
        # one pass: the waiter is before the poster in run_seq (sim_tests.py)
        self.check(lat and max(lat) <= pm, f"post -> waiter within one pass: {max(lat or [0]):.0f} us <= {pm:.0f}"
                   + self.sim("latency_max_us"))
        self.check(len(timeouts) >= 5, f"the timeout path: {len(timeouts)} timeouts")
        bound = (math.ceil(100 / 5) + 1) * 5000 + pm
        self.check(timeouts and min(timeouts) >= 100000 and max(timeouts) <= bound,
                   f"timeouts 100 ms, never early: {min(timeouts or [0]):.0f}..{max(timeouts or [0]):.0f} us (<= {bound:.0f})"
                   + self.sim("timeout_gap_max_us"))

    def t_isr_ev(self):
        """The sim times UART byte -> ISR -> task (<= 2 ms); from the host only
        the runs and the ring head can be seen."""
        n0 = self.log(self.peek("st_log_isr_ev")["st_log_isr_ev"])["n"]
        for ch in "abcdefgh":
            self.con.write(ch)
            time.sleep(0.02)
        self.con.write("\b" * 8 + "\r")
        self.con.until(PROMPT, 3)
        lg = self.log(self.peek("st_log_isr_ev")["st_log_isr_ev"])
        runs = (lg["n"] - n0) & 0xffff
        heads = [e[2] for e in lg["entries"][-runs:]] if runs else []
        self.record(runs=runs)
        self.check(runs >= 8, f"isr_ev ran for each byte ({runs} runs for 8 bytes + backspaces)")
        self.check(all(((b - a) & 0xff) < 128 for a, b in zip(heads, heads[1:])), "and saw the ring head move forward", heads)
        self.info("the ISR -> task latency needs the sim's clock (<= 2 ms there)" + self.sim("latency_us"))

    def t_hot(self):
        self.clear()
        self.con.cmd("hot on")
        self.wait(2)
        self.con.cmd("hot off")
        self.fine()
        p = self.peek("sched_st_max", "st_log_hot")
        lg = self.log(p["st_log_hot"])
        gaps = [self.span_us(a, b) for a, b in zip(lg["entries"], lg["entries"][1:])] + [lg["gap_max"] * FINE_US]
        mx = self.u16s(p["sched_st_max"])
        longest = max(mx[1:len(self.S) - 1]) * FINE_US
        spacing, per_pos = self.b["hot_spacing"], 60
        bound = spacing * (longest + per_pos)
        self.record(runs=lg["n"], gap_max_us=round(max(gaps), 1), longest_step_us=round(longest, 1))
        self.check(lg["n"] > 1000, f"hot ran {lg['n']} times in 2 s" + self.sim("runs"))
        self.check(max(gaps) <= bound, f"worst gap {max(gaps):.0f} us <= spacing {spacing} x (longest step "
                   f"{longest:.0f} + {per_pos}) us = {bound:.0f}" + self.sim("gap_max_us"))

    def t_jobs(self):
        names = ("st_jobs_claimed", "st_jobs_full", "st_jobs_done")
        p0 = self.peek(*names)
        dyn = self.b["slots"] - self.T["T_FIXED"]
        for rnd in (1, 2):
            self.con.cmd("jobs")
            self.wait(0.3)
            p = self.peek(*names, "sched_task")
            c, f, d = ((self.u(p[n]) - self.u(p0[n])) & 0xffff for n in names)
            tasks = [p["sched_task"][i * TASK_SIZE] for i in range(self.b["slots"])]
            self.record(**{f"claimed{rnd}": c, f"full{rnd}": f, f"done{rnd}": d})
            self.check(c == rnd * dyn and f == rnd, f"round {rnd}: claims take the {dyn} dynamic slots, then SCHED_NONE "
                       f"({c} claimed, {f} full)")
            self.check(d == rnd * dyn, f"round {rnd}: every job ran its 5 steps and ended ({d})")
            self.check(all(s == 0 for s in tasks[self.T["T_FIXED"]:]) and all(tasks[:self.T["T_FIXED"]]),
                       f"round {rnd}: dynamic slots free again, fixed slots untouched", tasks)

    def t_printer(self):
        self.clear()
        self.con.drain(0.05)
        t0 = time.time()
        self.con.write("print 3000\r")
        text = self.con.until(r"print done\n", 60)
        wall = time.time() - t0
        self.check(text is not None, "print 3000 finished")
        text = text or ""
        self.con.until(PROMPT, 3)
        lines = re.findall(r"P\d+ [^\n]*", text)
        want = [f"P{i} the quick brown fox jumps" for i in range(3000)]
        self.fine()
        p = self.peek("st_print_done", "sched_st_max")
        pr, con = self.st_max_us(p["sched_st_max"], "S_PR_LINE"), self.st_max_us(p["sched_st_max"], "S_CON_EDIT")
        bad = [i for i, (a, b) in enumerate(zip(lines, want)) if a != b][:3]
        self.record(lines=len(lines), pr_line_max_us=round(pr, 1), wall_s=round(wall, 2))
        self.check(lines == want, f"3000 lines, in order, none lost or garbled ({len(lines)} seen, {wall:.1f} s)",
                   [(i, lines[i]) for i in bad] or None)
        self.check(self.u(p["st_print_done"]) == 3000, f"st_print_done {self.u(p['st_print_done'])}")
        self.check(pr < 5000 and con < 5000, f"no printer or console step over 5 ms (PR_LINE {pr:.0f} us, "
                   f"CON_EDIT {con:.0f} us)" + self.sim("pr_line_max_us"))

    def t_flash(self):
        self.clear()
        self.con.cmd("flash", until=r"flash: (ok|verify FAILED|no free slot)\n", timeout=10)
        self.con.until(PROMPT, 3)
        self.fine()
        p = self.peek("st_fl_state", "st_fl_bad", "sched_st_max")
        st, bad = self.u(p["st_fl_state"]), self.u(p["st_fl_bad"])
        mx = {s: self.st_max_us(p["sched_st_max"], s) for s in ("S_FL_ERASE", "S_FL_PROG", "S_FL_VERIFY", "S_FL_DONE")}
        self.record(state=st, bad=bad, **{k + "_max_us": round(v, 1) for k, v in mx.items()})
        self.check(st == 2 and bad == 0, f"erase, program, verify of the upload sector: ok (state {st}, bad {bad})")
        self.check(mx["S_FL_PROG"] < 5000 and mx["S_FL_VERIFY"] < 5000,
                   f"program and verify steps under 5 ms ({mx['S_FL_PROG']:.0f}, {mx['S_FL_VERIFY']:.0f} us)"
                   + self.sim("S_FL_PROG_max_us"))
        self.check(mx["S_FL_ERASE"] < 45000, f"the erase, the sanctioned exception: {mx['S_FL_ERASE']:.0f} us < 45000"
                   + self.sim("S_FL_ERASE_max_us"))

    def slow(self, ms, lo, hi):
        self.clear()
        self.con.cmd(f"slow {ms}")
        self.wait(0.2)
        self.fine()
        p = self.peek("sched_overruns", "sched_overrun_state", "sched_st_max")
        ov, st = self.u(p["sched_overruns"]), self.u(p["sched_overrun_state"])
        mx = self.st_max_us(p["sched_st_max"], "S_SL_SLOW")
        return ov, st, mx

    def t_slow(self):
        ov, st, mx = self.slow(30, 29500, 31000)
        self.record(overruns=ov, state=st, sl_slow_us=round(mx, 1))
        self.check(ov == 1 and st == self.S["S_SL_SLOW"], f"slow 30: one overrun, of SL_SLOW (overruns {ov}, state {st})")
        self.check(29500 <= mx < 31000, f"its length measured: {mx:.0f} us" + self.sim("sl_slow_us"))
        ov, st, mx = self.slow(20, 19500, 21000)
        self.record(overruns20=ov, sl_slow20_us=round(mx, 1))
        self.check(19500 <= mx <= 21000, f"slow 20: its length measured: {mx:.0f} us (overruns {ov}: 20 ms is the limit)")

    def paste(self, what, prefix=""):
        names = ("st_con_rx_n", "st_con_rx_sum", "con_rx_overruns")
        p0 = self.peek(*names)
        text = "".join(chr(33 + (i * 7) % 94) for i in range(500))
        self.con.drain(0.05)
        self.con.write(prefix + text + "\r")
        out = self.con.until(PROMPT, 5) or ""
        if prefix:
            out += self.con.until(PROMPT, 5) or ""
        self.wait(0.3)
        p1 = self.peek(*names)
        sent = prefix + text + "\r" + self.peek_line    # the second peek's line is counted too
        n = ((self.u(p1["st_con_rx_n"]) - self.u(p0["st_con_rx_n"])) & 0xffff) - len(self.peek_line)
        ok = self.u(p1["st_con_rx_sum"]) == (self.u(p0["st_con_rx_sum"]) + sum(map(ord, sent))) & 0xffff
        ov = (self.u(p1["con_rx_overruns"]) - self.u(p0["con_rx_overruns"])) & 0xffff
        echo = text[:LINE_MAX] in out
        self.record(**{f"{what}_bytes": n, f"{what}_overruns": ov})
        return n - len(prefix), ok, ov, echo

    def t_paste(self):
        """500 characters + CR at 115200, quiet and across slow 20 (sent
        just before the paste, so its step runs while the bytes arrive)"""
        n, ok, ov, echo = self.paste("quiet")
        self.check(n == 501 and ok and ov == 0, f"a quiet paste: all 501 bytes taken, none lost ({n}, sum ok {ok}, "
                   f"overruns {ov})")
        self.check(echo, "the line editor echoed its 63 characters")
        self.clear()
        n, ok, ov, echo = self.paste("step20", prefix="slow 20\r")
        self.fine()
        mx = self.st_max_us(self.peek("sched_st_max")["sched_st_max"], "S_SL_SLOW")
        self.record(step20_us=round(mx, 1))
        self.check(n == 501 and ok and ov == 0, f"across a {mx / 1000:.1f} ms step: all 501 bytes taken, none lost "
                   f"({n}, sum ok {ok}, overruns {ov})")
        self.check(19500 <= mx <= 21000, f"that step was ~20 ms ({mx:.0f} us)" + self.sim("step20_us"))

    def t_stack(self):
        r0 = self.u(self.peek("st_deep_runs")["st_deep_runs"])
        self.con.cmd("deep 9")
        for _ in range(10):                  # serial traffic both ways under the deep chain
            self.con.write("?\r")
            time.sleep(0.15)
        self.con.cmd("deep 0")
        self.wait(1.2)                       # sleep_c rescans the paint each second
        p = self.peek("st_stack_hw", "st_deep_sp", "st_deep_runs", "st_stack_paint_from")
        hw, dsp = self.u(p["st_stack_hw"]), self.u(p["st_deep_sp"])
        runs = (self.u(p["st_deep_runs"]) - r0) & 0xffff
        self.record(stack_hw=hw, deep_sp=dsp, deep_runs=runs)
        self.check(runs >= 30, f"deep ran {runs} times under the console traffic" + self.sim("deep_runs"))
        self.check(0xff - hw >= 30, f"stack headroom {0xff - hw} B >= 30 (high-water {hw:#x}, deep's bottom SP {dsp:#x})"
                   + self.sim("stack_hw"))

    def t_flashtime(self):
        self.con.cmd("flashtime", until=r"flashtime done\n> ", timeout=10)
        self.fine()
        p = self.peek("st_ft", "st_ft_bad")
        ft = self.u16s(p["st_ft"])
        t = [self.span_us(ft[i:i + 2], ft[i + 2:i + 4]) for i in (0, 4, 8)]
        bad = self.u(p["st_ft_bad"])
        self.record(erase_us=round(t[0], 1), write4_us=round(t[1], 1), write256_us=round(t[2], 1), bad=bad)
        self.info(f"sector erase {t[0]:.0f} us, 4-byte program {t[1]:.0f} us, 256-byte program {t[2]:.0f} us"
                  " (rtlsim's model: erase 40 ms, a 4-byte program ~70 us)")
        self.check(bad == 0, f"read back: {bad} bytes wrong")
        self.check(t[0] < 400000, f"the erase within the datasheet's 400 ms: {t[0]:.0f} us")

    def t_erasepeek(self):
        self.con.cmd("erasepeek", until=r"erasepeek done\n> ", timeout=10)
        self.fine()
        p = self.peek("flash_peek", "st_pk_bank", "st_ft")
        busy, idle, bank = p["flash_peek"][:64], p["flash_peek"][64:], self.u(p["st_pk_bank"])
        ft = self.u16s(p["st_ft"])
        erase = self.span_us(ft[0:2], ft[2:4])
        want = bytes.fromhex(self.b["window"].get(str(bank), ""))
        diff = [(0x4000 + 0x300 * i, busy[i], idle[i]) for i in range(64) if busy[i] != idle[i]]
        self.record(bank=bank, busy=busy.hex(), idle=idle.hex(), differ=len(diff), erase_us=round(erase, 1))
        self.info(f"bank {bank}, the erase took {erase:.0f} us")
        self.info(f"busy {busy.hex()}")
        self.info(f"idle {idle.hex()}")
        self.check(idle == want, f"the idle row is bank {bank}'s code from the image", want.hex() if idle != want else None)
        self.info(f"{len(diff)} of 64 differ during the erase (rtlsim returns 0x00, as the 8x did)"
                  + "".join(f"\n           {a:#06x}: busy {b:02x} idle {c:02x}" for a, b, c in diff))

    def t_report(self):
        self.report()

    TESTS = ["wake", "waiter", "isr_ev", "hot", "jobs", "printer", "flash", "slow", "paste", "stack", "report",
             "flashtime", "erasepeek"]

    # -- the report: what `health` and the logs hold, decoded
    def report(self):
        self.fine()
        p = self.peek("ticks", "sched_pass_max", "sched_overruns", "sched_overrun_state", "con_rx_overruns")
        p.update(self.peek("sched_st_runs", "sched_st_max"))
        p.update(self.peek("st_stack_hw", "st_jobs_claimed", "st_jobs_full", "st_jobs_done", "st_print_done"))
        p.update(self.peek("st_log_sleep_a", "st_log_sleep_b", "st_log_sleep_c"))
        p.update(self.peek("st_log_waiter", "st_log_poster", "st_log_hot"))
        rows = [("uptime", f"ticks {self.u(p['ticks'])} ({self.u(p['ticks']) * TICK_MS / 1000:.1f} s)"),
                ("pass", f"max_us {self.u(p['sched_pass_max']) * FINE_US:.0f} fine_per_tick {self.per_tick}")]
        for name, ms in (("sleep_a", 5), ("sleep_b", 37), ("sleep_c", 1000)):
            e = self.log(p["st_log_" + name])["entries"]
            g = [self.span_us(a, b) for a, b in zip(e, e[1:])]
            rows.append((name, f"req_ms {ms} runs {len(e)} gap_min_us {min(g or [0]):.0f} gap_max_us {max(g or [0]):.0f}"))
        e = self.log(p["st_log_hot"])["entries"]
        g = [self.span_us(a, b) for a, b in zip(e, e[1:])]
        rows.append(("hot", f"runs {len(e)} gap_max_us {max(g or [0]):.0f}"))
        runs, mx = self.u16s(p["sched_st_runs"]), self.u16s(p["sched_st_max"])
        for i, s in enumerate(self.b["states"][1:-1], 1):
            if runs[i]:
                rows.append(("state", f"{s[2:]} runs {runs[i]} max_us {mx[i] * FINE_US:.0f}"))
        ost = self.u(p["sched_overrun_state"])
        rows += [("overruns", f"n {self.u(p['sched_overruns'])} last {self.b['states'][ost][2:] if ost else '-'}"),
                 ("rx", f"overruns {self.u(p['con_rx_overruns'])}"),
                 ("stack", f"hw {self.u(p['st_stack_hw']):#x} headroom {0xff - self.u(p['st_stack_hw'])}"),
                 ("jobs", f"claimed {self.u(p['st_jobs_claimed'])} none {self.u(p['st_jobs_full'])} "
                          f"ended {self.u(p['st_jobs_done'])}"),
                 ("print", f"done {self.u(p['st_print_done'])}")]
        for k, v in rows:
            print(f"    {k} {v}")
        self.record(**{f"{k}{i}": v for i, (k, v) in enumerate(rows)})

    # -- layer 2b: the hang image (sim_neg.py)
    def reset_line(self, what, send, timeout=15):
        t0 = time.time()
        self.con.drain(0.05)
        self.con.write(send)
        text = self.con.until(LAST_RESET, timeout)
        if text is None:
            raise Fail(f"{what}: no 'Last reset:' line within {timeout} s ({self.con.buf[-300:]!r})")
        wall = time.time() - t0
        line = re.search(LAST_RESET + "$", text).group(0).rstrip("\n")
        rest = self.con.until(PROMPT, 5)
        self.check("RTLPlayground loader" in text and "Starting application" in text,
                   f"{what}: the reset went through the loader")
        v = re.search(r"schedtest (\S+)", text)
        self.check(v and v.group(1) == self.b["version"], f"{what}: the image is {self.b['version']}",
                   v and v.group(1))
        self.check(rest is not None, f"{what}: a prompt after the boot")
        print(f"    line {line}")
        return line, wall

    RX = (r"Last reset: (?P<cause>.+?) in (?P<slot>[\w-]+)/(?P<state>\w+) after (?P<ms>\d+) ms"
          r"(?:, PC 0x(?P<pc>[0-9a-f]{4}))? bank (?P<bank>\d+), states(?P<ring>( \S+){8})$")

    def where(self, bank, pc):
        key = pc if pc < 0x4000 else bank << 16 | pc
        name = None
        for a, n in self.b["funcs"]:
            if a > key:
                break
            name = n
        return name

    def t_hang(self):
        h = self.b["sl_hang"]
        for n in (1, 2):
            line, wall = self.reset_line(f"hang {n}", "hang\r")
            m = re.match(self.RX, line)
            self.check(m, f"hang {n}: the line parses", line)
            if not m:
                continue
            ms, pc, bank = int(m["ms"]), int(m["pc"] or "0", 16), int(m["bank"])
            self.record(**{f"hang{n}_ms": ms, f"hang{n}_pc": pc, f"hang{n}_wall_s": round(wall, 2)})
            self.check(m["cause"] == "watchdog", f"hang {n}: cause watchdog ({m['cause']})")
            self.check(m["slot"] == "sl" and m["state"] == "SL_HANG", f"hang {n}: sl/SL_HANG ({m['slot']}/{m['state']})")
            # c = 999 or 1000 ticks left at SL_HANG's entry, age = c or c + 1 (a tick before CRASH_NOTE)
            self.check(ms // TICK_MS in (999, 1000, 1001) and ms % TICK_MS == 0,
                       f"hang {n}: after {ms} ms (the step's age 999..1001 ticks)" + self.sim(f"hang{n}_ms"))
            self.check(h["addr"] <= pc < h["addr"] + h["size"] and bank == h["bank"],
                       f"hang {n}: PC {pc:#06x} bank {bank} inside sl_hang ({h['addr']:#06x}+{h['size']}, bank {h['bank']})")
            self.info(f"hang {n}: {wall:.1f} s from `hang` to the line (5 s + the loader)")
        t = self.con.cmd("health")
        self.check("last reset: watchdog in sl/SL_HANG after" in t, "health shows it")

    def t_ctrl_r(self):
        line, _ = self.reset_line("^R ^R", b"\x12\x12")
        m = re.match(self.RX, line)
        self.check(m and m["cause"] == "^R ^R", "^R ^R: the cause", line)
        if m:
            pc, bank = int(m["pc"] or "0", 16), int(m["bank"])
            fn = self.where(bank, pc)
            self.record(pc=pc, bank=bank, fn=fn)
            self.check(pc and fn and bank <= 3, f"^R ^R: the interrupted PC {pc:#06x} bank {bank} is in {fn}")
        line, _ = self.reset_line("reset", "reset\r")
        m = re.match(self.RX, line)
        self.check(m and m["cause"] == "reset command" and m["slot"] == "cli" and m["state"] == "CLI_RUN"
                   and m["ms"] in ("0", "5") and m["pc"] is None,
                   "reset: reset command in cli/CLI_RUN after 0 (or 5) ms, without a PC", line)

    HANG_TESTS = ["hang", "ctrl_r"]

    def run(self, only=None):
        tests = self.HANG_TESTS if self.b["variant"] == "hang" else self.TESTS
        self.current = "sync"
        self.sync()
        for name in tests:
            if only and name not in only:
                continue
            self.current = name
            t0 = time.time()
            print(f"  {name}")
            try:
                getattr(self, "t_" + name)()
            except Fail as e:
                self.failures.append(f"{name}: {e}")
                print(f"    FAIL {e}")
                try:
                    self.sync()
                except Fail:
                    pass
            print(f"    ({time.time() - t0:.1f} s)")
        return self.results


# ---- --embed: the build's constants, at build time ----
def embed(build):
    import glob
    imgs = sorted(glob.glob(os.path.join(build, "rtlplayground-boot-schedtest*.bin")), key=os.path.getmtime)
    if not imgs:
        sys.exit(f"silicon.py --embed: no -boot schedtest image in {build}")
    m = re.match(r"rtlplayground-boot-schedtest(?:-(hang|overrun|bad_flash))?(?:-crash[0-9a-f]+)?-(.+)-[A-Z0-9_]+-compact\.bin$",
                 os.path.basename(imgs[-1]))
    variant, version = m.group(1) or "base", m.group(2)
    app = open(imgs[-1].replace("rtlplayground-boot-", "rtlplayground-"), "rb").read()
    mp = open(os.path.join(build, "rtlplayground.map")).read()
    data = {n: int(a, 16) for a, n in re.findall(r"^D:\s+([0-9A-F]{8})\s+_(\w+)", mp, re.M)}
    code = sorted((int(a, 16), "_" + n) for a, n in re.findall(r"^C:\s+([0-9A-F]{8})\s+_(\w+)", mp, re.M))
    want = ["ticks", "sched_fine_per_tick", "sched_pass_max", "sched_st_runs", "sched_st_max", "sched_overruns",
            "sched_overrun_state", "con_rx_overruns", "st_con_rx_n", "st_con_rx_sum", "st_stack_hw",
            "st_stack_paint_from", "st_sp_step_max", "st_deep_sp", "st_deep_runs", "st_jobs_claimed", "st_jobs_full",
            "st_jobs_done", "st_fl_state", "st_fl_bad", "st_print_done", "st_slow_runs", "sched_task",
            "st_log_sleep_a", "st_log_sleep_b", "st_log_sleep_c", "st_log_waiter", "st_log_poster", "st_log_isr_ev",
            "st_log_hot", "st_ft", "st_ft_bad", "flash_peek", "st_pk_bank"]
    missing = [n for n in want if n not in data]
    if missing:
        sys.exit(f"silicon.py --embed: not in the .map: {missing}")
    hdr = open(os.path.join(build, "sched", "sched_gen.h")).read()
    enums = re.findall(r"enum \{(.*?)\};", hdr, re.S)
    states = re.findall(r"\b(S_\w+)", enums[0])
    T, i = {}, 0
    for n, alias in re.findall(r"\b(T_\w+)( = T_FIXED)?", enums[1]):
        if alias:
            i = T["T_FIXED"]
        T[n] = i
        i += 1
    seq = open(os.path.join(build, "sched", "sequence.txt")).read()
    a = next(a for a, n in code if n == "_sl_hang")
    nxt = next(b for b, n in code if b > a)
    window = {str(bk): bytes(app[0x4000 + (bk - 1) * 0xC000 + 0x300 * i] for i in range(64)).hex() for bk in (1, 2, 3)}
    b = dict(variant=variant, version=version, image=os.path.basename(imgs[-1]),
             syms={n: data[n] for n in want}, states=states, T=T, slots=T["T_SLOTS"],
             hot_spacing=int(re.search(r"^hot\s+high\s+\d+\s+(\d+)", seq, re.M).group(1)),
             sl_hang=dict(bank=a >> 16, addr=a & 0xffff, size=nxt - a),
             funcs=[[x, n] for x, n in code], window=window)
    src = open(os.path.abspath(__file__)).read()
    src = src.replace("\nBUILD = None ", "\nBUILD = " + json.dumps(b, separators=(",", ":")) + " ", 1)
    sys.stdout.write(src)


# ---- --sim: rtlsim behind a pty, paced to the wall clock ----
def sim_bridge(build, ovs):
    import glob, pty, threading, tty
    sys.path.insert(0, os.path.join(ovs, "tests"))
    import rtlsim_ctl
    img = sorted(glob.glob(os.path.join(build, "rtlplayground-boot-schedtest*.bin")), key=os.path.getmtime)[-1]
    sim = rtlsim_ctl.Sim(img, board=os.path.join(ovs, "boards", "keeplink_kp_9000_9xhpml_v1_2.json"),
                         map=os.path.join(build, "rtlplayground.map"), rtlsim=os.path.join(ovs, "build", "rtlsim"),
                         timeout=120)
    master, slave = pty.openpty()
    tty.setraw(slave)
    port = os.ttyname(slave)
    stop = threading.Event()

    def loop():
        t0, simt = time.time(), 0.0
        while not stop.is_set():
            r, _, _ = select.select([master], [], [], 0.002)
            if r:
                sim.uart_write(os.read(master, 4096))
            ahead = (time.time() - t0) * 1000 - simt
            if ahead >= 2:
                step = min(ahead, 20)
                sim.run(step)
                simt += step
            out = sim.uart_text()
            if out:
                os.write(master, out.encode("latin-1"))
        sim.close()

    th = threading.Thread(target=loop, daemon=True)
    th.start()
    return port, (stop, th, sim)


def main(argv):
    ap = argparse.ArgumentParser(description="schedtest on silicon (C)")
    ap.add_argument("what", nargs="?", choices=["run", "report"], default="run")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--json", help="write the measured numbers here")
    ap.add_argument("--log", help="copy the console's text here")
    ap.add_argument("--embed", metavar="BUILDDIR", help="print this script with BUILDDIR's constants (the build does it)")
    ap.add_argument("--sim", action="store_true", help="against rtlsim (this copy's build directory) through a pty")
    ap.add_argument("--ovs", default=os.path.expanduser("~/src/ovs-rtlpg"))
    a = ap.parse_args(argv)
    if a.embed:
        return embed(a.embed)
    if BUILD is None:
        sys.exit("silicon.py: run the copy in the build directory (make schedtest embeds the build's constants)")
    bridge = None
    if a.sim:
        a.port, bridge = sim_bridge(os.path.dirname(os.path.abspath(__file__)), a.ovs)
    t0 = time.time()
    print(f"silicon: {BUILD['image']} ({BUILD['variant']}) on {a.port}")
    log = open(a.log, "w") if a.log else None
    s = Silicon(Console(a.port, a.baud, log), BUILD)
    try:
        if a.what == "report":
            s.current = "report"
            s.sync()
            s.report()
        else:
            s.run(a.only)
    except Fail as e:
        s.failures.append(str(e))
        print(f"  FAIL {e}")
    finally:
        if bridge:
            bridge[0].set()
            bridge[1].join(5)
    if a.json:
        with open(a.json, "w") as f:
            json.dump(s.results, f, indent=1, sort_keys=True)
    print(f"silicon: {len(s.failures)} failures, {time.time() - t0:.1f} s")
    for f in s.failures:
        print(f"  FAIL {f}")
    return 1 if s.failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
