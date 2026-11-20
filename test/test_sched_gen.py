#!/usr/bin/env python3
"""
Host tests for tools/sched_gen.py (doc/sched/generator.md).

  - golden: the plausible task set (sched_gen/plausible/, modules.md's tasks)
    generates exactly sched_gen/golden/*; --update rewrites the golden files
  - the generated C compiles and links with host gcc (sdcc_shim.h), and the
    tables hold what the .def files say
  - spacing of the derived sequence
  - sequence.def overrides
  - every broken spec fails with its message; a cross-module task_next warns
  - the sweep (tools/sched_sweep.py): TASK/STEP lines through cpp into the
    .def the generator takes; its errors

  python3 test_sched_gen.py [--update]
"""
import contextlib, io, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import sched_gen                                        # noqa: E402
import sched_sweep                                      # noqa: E402

PLAUSIBLE = os.path.join(HERE, "sched_gen", "plausible")
GOLDEN = os.path.join(HERE, "sched_gen", "golden")
fails = checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print(f"FAIL: {what}")


def plausible_defs():
    # relative paths, so the golden files name them the same from any checkout
    order = open(os.path.join(PLAUSIBLE, "ORDER")).read().split()
    return [os.path.join("sched_gen", "plausible", d) for d in order]


def said(r):
    return repr(r) if isinstance(r, str) else "no error"


def gen(defs, slots=24, fill=2, sequence=None):
    """(outputs, report, warnings) or the SpecError message."""
    try:
        return sched_gen.generate(defs, slots, fill, sequence)
    except sched_gen.SpecError as e:
        return str(e)


def test_golden(update):
    out, report, warnings = gen(plausible_defs())
    check(warnings == [], f"plausible set warns: {warnings}")
    for name, text in out.items():
        p = os.path.join(GOLDEN, name)
        if update:
            os.makedirs(GOLDEN, exist_ok=True)
            open(p, "w").write(text)
        check(os.path.exists(p) and open(p).read() == text, f"golden {name} differs (--update to accept)")


def test_compiles():
    """gcc builds the golden output with a stub per step; the tables point where the .def says."""
    defs = plausible_defs()
    out, _, _ = gen(defs)
    states = [s for m in map(sched_gen.parse_def, defs) for t in m.tasks for s in t.states]
    with tempfile.TemporaryDirectory() as d:
        for name, text in out.items():
            open(os.path.join(d, name), "w").write(text)
        main = ['#include <stdio.h>', '#include "sched_gen.h"']
        main += [f"uint8_t {s.fn}(void) {{ return 0; }}" for s in states]
        main.append("int main(void)\n{\n\tint bad = 0;")
        main.append("\tbad |= step_addr[S_NONE] != 0;")
        main += [f"\tbad |= step_addr[S_{s.name}] != {s.fn};" for s in states]
        main += [f'\tbad |= strcmp(state_name[S_{s.name}], "{s.name}") != 0;' for s in states]
        main.append("\tbad |= fixed_first[T_SFP1] != S_SFP_POLL || fixed_first[T_CLI] != S_CLI_IDLE;")
        main.append("\tbad |= T_SLOTS != SCHED_GEN_SLOTS || T_JOB0 != T_FIXED;")
        main.append('\tbad |= strcmp(slot_name[T_SFP0], "sfp0") != 0 || strcmp(slot_name[T_JOB0], "job0") != 0;')
        main.append("\tfor (int i = 0; i < RUN_SEQ_LEN; i++) bad |= run_seq[i] >= T_SLOTS;")
        main.append("\treturn bad;\n}")
        open(os.path.join(d, "main.c"), "w").write("#include <string.h>\n" + "\n".join(main) + "\n")
        exe = os.path.join(d, "t")
        r = subprocess.run(["gcc", "-std=gnu11", "-Wall", "-Werror", "-include", os.path.join(HERE, "sdcc_shim.h"),
                            "-I", d, os.path.join(d, "main.c"), os.path.join(d, "sched_gen.c"),
                            os.path.join(d, "sched_gen_names.c"), "-o", exe], capture_output=True, text=True)
        check(r.returncode == 0, f"generated C doesn't build:\n{r.stderr}")
        if r.returncode == 0:
            check(subprocess.run([exe]).returncode == 0, "generated tables don't match the .def files")


def test_spacing():
    defs = plausible_defs()
    for fill in (1, 2, 3, 4):
        out, report, _ = gen(defs, fill=fill)
        seq_len = int(re.search(r"#define RUN_SEQ_LEN (\d+)", out["sched_gen.h"]).group(1))
        rows = {r.split()[0]: r.split() for r in report[2:]}
        check(len(rows) == 24, f"fill {fill}: every slot reported")
        for name, freq, runs, gap in rows.values():
            runs, gap = int(runs), int(gap)
            if freq == "high":
                # 4 high slots: the group plus `fill` others between two of its runs
                check(gap == 4 + fill, f"fill {fill}: {name} worst gap {gap}, want {4 + fill}")
            elif freq == "medium":
                check(runs == 3, f"fill {fill}: {name} runs {runs} times, want 3")
                check(gap <= -(-seq_len // 3) + 4 + fill, f"fill {fill}: {name} worst gap {gap} of {seq_len}")
            else:
                check(runs == 1 and gap == seq_len, f"fill {fill}: {name} once per pass")
    # a known tiny input, exactly: two high, one medium, three once, no jobs
    with tempfile.TemporaryDirectory() as d:
        write_module(d, "m", "task a fixed freq=high\nstate A_S a_s\ntask b fixed freq=high\nstate B_S b_s\n"
                     "task m fixed freq=medium\nstate M_S m_s\ntask x fixed\nstate X_S x_s\n"
                     "task y fixed\nstate Y_S y_s\ntask z fixed\nstate Z_S z_s\n")
        out, _, _ = gen([os.path.join(d, "m.def")], slots=6)
        seq = re.search(r"run_seq\[RUN_SEQ_LEN\] = \{\n(.*?)\n\};", out["sched_gen.c"], re.S).group(1)
        seq = [t.strip() for t in seq.replace("\n", "").split(",")]
        want = "A B M X A B M Y A B M Z".split()
        check(seq == [f"T_{w}" for w in want], f"tiny sequence {seq}")


def write_module(d, name, deftext, protos=None, body=""):
    open(os.path.join(d, name + ".def"), "w").write(deftext)
    fns = re.findall(r"^state \S+ (\S+)", deftext, re.M)
    if protos is None:
        protos = "".join(f"uint8_t {f}(void) __banked;\n" for f in fns)
    open(os.path.join(d, name + ".h"), "w").write(protos)
    open(os.path.join(d, name + ".c"), "w").write(body)


def test_sequence_def():
    defs = plausible_defs()
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "sequence.def")
        fixed = "rx link loop con cli net stp lacp lldp sfp0 sfp1 igmp house"
        open(p, "w").write("# explicit\n" + fixed + "\nrx stp\n" + "job " * 11 + "\n")
        r = gen(defs, sequence=p)
        check(not isinstance(r, str), f"sequence.def rejected: {r}")
        if not isinstance(r, str):
            check("#define RUN_SEQ_LEN 26" in r[0]["sched_gen.h"], "sequence.def length")
            check("T_HOUSE, T_RX, T_STP, T_JOB0" in r[0]["sched_gen.c"].replace("\n\t", " "), "sequence.def order")
        for text, msg in (
            (fixed + " job" * 10, "10 job placeholders for 11 dynamic slots"),
            (fixed.replace(" igmp", "") + " job" * 11, "never runs: igmp"),
            (fixed + " sfp" + " job" * 11, "'sfp' is not a fixed slot or 'job'"),
            (fixed + " job" * 12, "12 job placeholders for 11 dynamic slots: one per slot"),
        ):
            open(p, "w").write(text + "\n")
            r = gen(defs, sequence=p)
            check(isinstance(r, str) and msg in r, f"sequence.def: want '{msg}', got {said(r)}")


BROKEN = [
    # (what, {module: (def, header or None for matching prototypes, .c body)}, slots, message)
    ("no prototype", {"m": ("task a fixed\nstate A_S a_s\n", "", "")}, 24,
     "function a_s has no prototype or definition"),
    ("not __banked", {"m": ("task a fixed\nstate A_S a_s\n", "uint8_t a_s(void);\n", "")}, 24,
     "a_s is not __banked"),
    ("static", {"m": ("task a fixed\nstate A_S a_s\n", "", "static uint8_t a_s(void) __banked\n{\n}\n")}, 24,
     "a_s is static"),
    ("takes a parameter", {"m": ("task a fixed\nstate A_S a_s\n", "uint8_t a_s(uint8_t p) __banked;\n", "")}, 24,
     "must be 'uint8_t a_s(void) __banked'"),
    ("returns no state", {"m": ("task a fixed\nstate A_S a_s\n", "void a_s(void) __banked;\n", "")}, 24,
     "must be 'uint8_t a_s(void) __banked'"),
    ("duplicate state", {"m": ("task a fixed\nstate A_S a_s\n", None, ""),
                         "n": ("task b fixed\nstate A_S b_s\n", None, "")}, 24,
     "state A_S also defined at"),
    ("one function, two states", {"m": ("task a fixed\nstate A_S a_s\nstate A_T a_s entry\n", None, "")}, 24,
     "function a_s is behind two states, A_S and A_T"),
    ("duplicate task", {"m": ("task a fixed\nstate A_S a_s\n", None, ""),
                        "n": ("task a fixed\nstate B_S b_s\n", None, "")}, 24,
     "task a also defined at"),
    ("unreachable", {"m": ("task a fixed\nstate A_S a_s\nstate A_T a_t\n", None, "")}, 24,
     "state A_T is reached by no return/STEP_*/task_claim"),
    ("too many tasks", {"m": ("task a fixed count=3\nstate A_S a_s\n", None, "")}, 2,
     "3 fixed slots, more than the 2 slots"),
    ("no slot left for jobs", {"m": ("task a fixed count=2\nstate A_S a_s\ntask j job\nstate J_S j_s\n", None, "")}, 2,
     "none is left for the jobs"),
    ("state before task", {"m": ("state A_S a_s\n", None, "")}, 24, "state before any task"),
    ("task without states", {"m": ("task a fixed\n", None, "")}, 24, "task a has no states"),
    ("first= not its state", {"m": ("task a fixed first=B_S\nstate A_S a_s\n", None, "")}, 24,
     "first=B_S is not one of its states"),
    ("bad freq", {"m": ("task a fixed freq=often\nstate A_S a_s\n", None, "")}, 24,
     "freq 'often' is not high, medium or once"),
    ("bad kind", {"m": ("task a periodic\nstate A_S a_s\n", None, "")}, 24, "'periodic' is not fixed or job"),
    ("count on a job", {"m": ("task a job count=2\nstate A_S a_s\n", None, "")}, 24, "count= is for fixed tasks"),
    ("lower-case state", {"m": ("task a fixed\nstate a_s a_s\n", None, "")}, 24, "state 'a_s' must be upper case"),
    ("unknown line", {"m": ("task a fixed\nstat A_S a_s\n", None, "")}, 24, "unknown line 'stat'"),
    ("slot name clash", {"m": ("task s fixed count=2\nstate A_S a_s\ntask s1 fixed\nstate B_S b_s\n", None, "")}, 24,
     "slot name s1 clashes"),
    ("task named fixed", {"m": ("task fixed fixed\nstate A_S a_s\n", None, "")}, 24,
     "slot name fixed clashes with the generated T_FIXED"),
    ("task named slots", {"m": ("task slots fixed\nstate A_S a_s\n", None, "")}, 24,
     "slot name slots clashes with the generated T_SLOTS"),
    ("reserved state", {"m": ("task a fixed\nstate COUNT a_s\n", None, "")}, 24, "state name COUNT is reserved"),
]


def test_broken():
    for what, mods, slots, msg in BROKEN:
        with tempfile.TemporaryDirectory() as d:
            for name, (deftext, hdr, body) in mods.items():
                write_module(d, name, deftext, hdr, body)
            r = gen([os.path.join(d, n + ".def") for n in mods], slots=slots)
            check(isinstance(r, str) and msg in r, f"broken spec '{what}': want '{msg}', got {said(r)}")
            if isinstance(r, str):
                check(re.match(r"^\S+\.def:\d+: |^\d+ ", r) is not None, f"'{what}': message names no place: {r}")
    # the command line: exit 1 and the message on stderr
    with tempfile.TemporaryDirectory() as d:
        write_module(d, "m", "task a fixed\nstate A_S a_s\n", "uint8_t a_s(void);\n")
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            rc = sched_gen.main(["-o", d, os.path.join(d, "m.def")])
        check(rc == 1 and "sched_gen: error:" in err.getvalue() and "not __banked" in err.getvalue(),
              f"command line: rc {rc}, stderr {err.getvalue()!r}")
        check(not os.path.exists(os.path.join(d, "sched_gen.h")), "a failed run writes no output")


def test_step_macros_reach():
    """The return macros and task_claim_arg reach states as task_next and task_claim do"""
    with tempfile.TemporaryDirectory() as d:
        body = ("uint8_t a_s(void) __banked { return STEP_SLEEP(S_A_T, 5); }\n"
                "uint8_t a_t(void) __banked { return STEP_WAIT(S_A_U, 1, 2); }\n"
                "uint8_t a_u(void) __banked { return STEP_ARG(S_A_V, 3); }\n"
                "uint8_t a_v(void) __banked { task_claim_arg(S_J_S, 4); return (S_A_S); }\n")
        write_module(d, "m", "task a fixed\nstate A_S a_s\nstate A_T a_t\nstate A_U a_u\nstate A_V a_v\n"
                     "task j job\nstate J_S j_s\n", body=body)
        r = gen([os.path.join(d, "m.def")])
        check(not isinstance(r, str), f"return, STEP_* and task_claim_arg reach their states: {said(r)}")
        write_module(d, "m", "task a fixed\nstate A_S a_s\n", body="uint8_t a_s(void) __banked\n{\n\treturn STEP_WAIT(S_B_T, 1, 2);\n}\n")
        write_module(d, "n", "task b fixed\nstate B_S b_s\nstate B_T b_t\n")
        r = gen([os.path.join(d, "m.def"), os.path.join(d, "n.def")])
        check(not isinstance(r, str) and len(r[2]) == 1 and "STEP_WAIT(S_B_T) jumps into" in r[2][0],
              f"a STEP_WAIT into another module warns: {r[2] if not isinstance(r, str) else r}")


def test_cross_module_warning():
    with tempfile.TemporaryDirectory() as d:
        write_module(d, "m", "task a fixed\nstate A_S a_s\n", body="uint8_t a_s(void) __banked\n{\n\treturn S_B_T;\n}\n")
        write_module(d, "n", "task b fixed\nstate B_S b_s\nstate B_T b_t\n")
        r = gen([os.path.join(d, "m.def"), os.path.join(d, "n.def")])
        check(not isinstance(r, str), f"cross-module jump fails the build: {r}")
        if not isinstance(r, str):
            w = r[2]
            check(len(w) == 1 and "m.c:3: warning: return S_B_T jumps into" in w[0], f"warning: {w}")


# The sweep (tools/sched_sweep.py): TASK/STEP lines through the host's cpp,
# with sched.h itself (SCHED_SWEEP) and the host shim, as test/Makefile does.
ROOT = os.path.join(HERE, "..")
SWEEP_CPP = f"gcc -E -include {os.path.join(HERE, 'sdcc_shim.h')} -I{ROOT}"
SWEEP_HEAD = '#include <stdint.h>\n#include "sched.h"\n'


def sweep(d, files, expect_fail=False):
    """Run the sweep over {name: C text} in d: (rc, stderr, [.def paths])."""
    srcs = []
    for name, text in files.items():
        p = os.path.join(d, name)
        open(p, "w").write(SWEEP_HEAD + text)
        srcs.append(p)
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stderr(err), contextlib.redirect_stdout(out):
        rc = sched_sweep.main(["-o", os.path.join(d, "gen"), "--cpp", SWEEP_CPP] + srcs)
    return rc, err.getvalue(), out.getvalue().split()


def test_sweep():
    with tempfile.TemporaryDirectory() as d:
        rc, err, defs = sweep(d, {"m.c": (
            "TASK(stp, fixed, medium, first=second)\n"
            "STEP(stp, step) { return S_STP_SECOND; }\n"
            "#ifdef NOT_IN_THIS_BUILD\nSTEP(stp, gone) { }\n#endif\n"
            "STEP(stp, second) { return STEP_WAIT(S_STP_STEP, 1, 2); }\n"
            "TASK(sfp, fixed, once, count=2)\nSTEP(sfp, poll) { }\n"
            "TASK(j, job, high, entry=two)\nSTEP(j, one) { }\nSTEP(j, two) { }\n")})
        check(rc == 0, f"sweep: rc {rc}: {err}")
        text = open(defs[0]).read() if defs else ""
        want = ["task stp fixed freq=medium first=STP_SECOND", "state STP_STEP stp_step", "state STP_SECOND stp_second",
                "task sfp fixed freq=once count=2", "state SFP_POLL sfp_poll",
                "task j job freq=high", "state J_ONE j_one", "state J_TWO j_two entry", "source m.i"]
        got = [ln.split("\t#")[0] for ln in text.splitlines()[1:]]
        check(got == want, f"sweep: the .def from the markers (an #ifdef'd step left out): {got}")
        check(re.search(r"state STP_SECOND stp_second\t# \S*m\.c:8$", text, re.M) is not None,
              "sweep: each line says where in the source it came from")
        i = open(os.path.join(d, "gen", "m.i")).read() if defs else ""
        check("__sched_" not in i and re.search(r"uint8_t stp_second\(void\) __banked", i),
              "sweep: the .i keeps the prototypes, not the markers")
        r = gen(defs)
        check(not isinstance(r, str), f"sweep: the generator takes its .def: {said(r)}")
        if not isinstance(r, str):
            check("\tstp_second,\t/* S_STP_SECOND */" in r[0]["sched_gen.c"]
                  and "\tS_STP_SECOND,\t/* T_STP */" in r[0]["sched_gen.c"], "sweep: first= is the fixed slot's start")
        # unchanged output keeps its timestamp
        t0 = os.stat(defs[0]).st_mtime_ns
        os.utime(defs[0], ns=(t0 - 10**9, t0 - 10**9))
        sweep(d, {"m.c": open(os.path.join(d, "m.c")).read()[len(SWEEP_HEAD):]})
        check(os.stat(defs[0]).st_mtime_ns == t0 - 10**9, "sweep: an unchanged .def is not rewritten")
        # a generator error names the C line
        rc, err, defs = sweep(d, {"m.c": "TASK(a, fixed, once)\nSTEP(a, s) { }\nSTEP(a, lost) { }\n"})
        r = gen(defs)
        check(isinstance(r, str) and re.search(r"m\.c:5: state A_LOST is reached by no", r),
              f"sweep: an unreached state is named by its STEP's line: {said(r)}")
    for what, text, msg in SWEEP_BROKEN:
        with tempfile.TemporaryDirectory() as d:
            rc, err, _ = sweep(d, {"m.c": text})
            check(rc == 1 and msg in err and re.search(r"m\.c:\d+: |has no TASK", err),
                  f"sweep '{what}': want '{msg}', got rc {rc} {err.strip()!r}")
    with tempfile.TemporaryDirectory() as d:
        rc, err, _ = sweep(d, {"m.c": "TASK(a, fixed, once)\nSTEP(a, s) { }\n", "x.c": "#error broken\n"})
        check(rc == 1 and "preprocessing" in err, f"sweep: a cpp error fails it: {err.strip()!r}")


def test_sweep_checks():
    """A step returning a helper's choice; a STEP outside the step sources."""
    helper = "static uint8_t pick(void) { return S_A_S; }\nTASK(a, fixed, once)\n"
    for what, body, ok in (
        ("a helper's return", "STEP(a, s) { return pick(); }\n", False),
        ("a helper's return inside a macro", "STEP(a, s) { return STEP_SLEEP(pick(), 5); }\n", False),
        ("marked on its line", "STEP(a, s) { return pick(); }\t/* helper returns state: pick */\n", True),
        ("marked above", "STEP(a, s)\n{\n\t/* helper returns state: pick */\n\treturn pick();\n}\n", True),
        ("marked for another", "STEP(a, s) { return pick(); }\t/* helper returns state: other */\n", False),
        ("STEP_* macros, a cast, a ternary", "STEP(a, s) { return (uint8_t)(1 ? STEP_WAIT(S_A_S, 1, 2) : STEP_ARG(S_A_S, 3)); }\n", True),
        ("a call before the return", "STEP(a, s) { if (pick()) return STEP_STAY; return S_A_S; }\n", True),
        ("a string with a brace", "static void p(const char *x) { (void)x; }\nSTEP(a, s) { p(\"}{\"); return S_A_S; }\n", True),
    ):
        with tempfile.TemporaryDirectory() as d:
            rc, err, _ = sweep(d, {"m.c": helper + body})
            if ok:
                check(rc == 0, f"sweep checks, {what}: accepted: rc {rc} {err.strip()!r}")
            else:
                check(rc == 1 and "returns what pick() says: a helper chooses the state" in err
                      and re.search(r"m\.c:\d+: STEP\(a, s\)", err), f"sweep checks, {what}: rejected: rc {rc} {err.strip()!r}")
    with tempfile.TemporaryDirectory() as d:
        other = os.path.join(d, "other.c")
        open(other, "w").write("/* STEP(a, s) in a comment */\nint x;\n#define STEP(t, s) nothing\n")
        rc, err, _ = sweep(d, {"m.c": "TASK(a, fixed, once)\nSTEP(a, s) { return S_A_S; }\n"})
        out, e = io.StringIO(), io.StringIO()
        with contextlib.redirect_stderr(e), contextlib.redirect_stdout(out):
            rc = sched_sweep.main(["-o", os.path.join(d, "gen"), "--cpp", SWEEP_CPP, "--all", other,
                                   os.path.join(d, "m.c"), "--", os.path.join(d, "m.c")])
        check(rc == 0, f"--all: a comment or a #define naming STEP is no step: {e.getvalue().strip()!r}")
        open(other, "a").write("STEP(b, s) { return 0; }\n")
        e = io.StringIO()
        with contextlib.redirect_stderr(e), contextlib.redirect_stdout(io.StringIO()):
            rc = sched_sweep.main(["-o", os.path.join(d, "gen"), "--cpp", SWEEP_CPP, "--all", other,
                                   "--", os.path.join(d, "m.c")])
        check(rc == 1 and re.search(r"other\.c:4: STEP\(\.\.\.\) in a source that isn't a step source", e.getvalue()),
              f"--all: a STEP outside the step sources fails: rc {rc} {e.getvalue().strip()!r}")


SWEEP_BROKEN = [
    ("step without task", "STEP(a, s) { }\n", "STEP(a, s) has no TASK(a, ...) above it"),
    ("task without step", "TASK(a, fixed, once)\n", "TASK(a) has no STEP(a, ...)"),
    ("too few fields", "TASK(a, fixed)\nSTEP(a, s) { }\n", "needs a name, fixed|job and high|medium|once"),
    ("bad kind", "TASK(a, fixd, once)\nSTEP(a, s) { }\n", "'fixd' is not fixed or job"),
    ("bad freq", "TASK(a, fixed, often)\nSTEP(a, s) { }\n", "'often' is not high, medium or once"),
    ("bad option", "TASK(a, fixed, once, prio=1)\nSTEP(a, s) { }\n", "bad option 'prio=1'"),
    ("first unknown", "TASK(a, fixed, once, first=t)\nSTEP(a, s) { }\n", "first=t is no STEP(a, ...)"),
    ("upper case task", "TASK(A, fixed, once)\nSTEP(A, s) { }\n", "must be lower case"),
    ("two tasks", "TASK(a, fixed, once)\nSTEP(a, s) { }\nTASK(a, job, once)\n", "TASK(a) also at"),
    ("no task at all", "int x;\n", "has no TASK"),
]


def test_om_rows():
    """--om: state_task maps each state to its task (1-based), and the rows
    load in tools/om_gen.py as state.sched's objects, named by slot, state and
    task; the plain outputs (golden) don't change."""
    defs = plausible_defs()
    rows = sched_gen.om_rows(defs, 24, 2, None)
    check(set(rows) == {"sched_gen_tasks.h", "sched_gen_tasks.c", "state_sched.py"}, f"--om writes {sorted(rows)}")
    out, _, _ = gen(defs)
    check(not set(rows) & set(out), "--om's files are its own: the golden outputs stay as they are")
    tasks = [t for m in map(sched_gen.parse_def, defs) for t in m.tasks]
    states = [s for t in tasks for s in t.states]
    with tempfile.TemporaryDirectory() as d:
        for name, text in list(out.items()) + list(rows.items()):
            open(os.path.join(d, name), "w").write(text)
        main = ['#include <stdio.h>', '#include "sched_gen_tasks.h"', "int main(void)\n{\n\tint bad = state_task[S_NONE] != 0;"]
        main += [f"\tbad |= state_task[S_{s.name}] != {tasks.index(s.task) + 1};" for s in states]
        main.append(f"\tbad |= SCHED_TASKS != {len(tasks)};")
        main.append("\treturn bad;\n}")
        open(os.path.join(d, "main.c"), "w").write("\n".join(main) + "\n")
        exe = os.path.join(d, "t")
        r = subprocess.run(["gcc", "-std=gnu11", "-Wall", "-Werror", "-include", os.path.join(HERE, "sdcc_shim.h"),
                            "-I", d, os.path.join(d, "main.c"), os.path.join(d, "sched_gen_tasks.c"), "-o", exe],
                           capture_output=True, text=True)
        check(r.returncode == 0, f"sched_gen_tasks.c doesn't build:\n{r.stderr}")
        if r.returncode == 0:
            check(subprocess.run([exe]).returncode == 0, "state_task doesn't match the .def files")
        # the rows through the object model's generator
        sys.path.insert(0, os.path.join(HERE, "..", "tools"))
        import om_gen
        om_gen.SCHEMA.update({"objs": [], "enums": {}, "msgs": {}, "cur": None})
        sd = os.path.join(d, "om")
        os.makedirs(sd)
        open(os.path.join(sd, "state_sched.py"), "w").write(rows["state_sched.py"])
        objs = {o.regname: o for o in om_gen.load_schema([sd])}
        check(set(objs) == {"sched", "sched.slot", "sched.state", "sched.task"}, f"the objects: {sorted(objs)}")
        slot = objs.get("sched.slot")
        check(slot is not None and slot.levels[0][0] == "WORD" and slot.levels[0][1] == 24
              and slot.levels[0][2].words[:2] == [tasks[0].name, tasks[1].name], "state.sched.slot.<slot>: 24, by slot name")
        st = objs.get("sched.state")
        check(st is not None and st.levels[0][2].words == [s.name for s in states], "state.sched.state.<STATE>: every state")
        check(om_gen.SCHEMA["enums"]["sstate"] == ["none"] + [s.name for s in states],
              "a slot's state: none, then the states (a word's number is the state's)")
        check(om_gen.SCHEMA["enums"]["stask"] == ["none"] + [t.name for t in tasks], "a slot's task: none, then the tasks")
        om_gen.gen(list(objs.values()), os.path.join(d, "omout"))
        check(os.path.exists(os.path.join(d, "omout", "om_gen.h")), "om_gen writes their tables")
        # --om-words (schedtest): the same words, no objects
        om_gen.SCHEMA.update({"objs": [], "enums": {}, "msgs": {}, "cur": None})
        words = sched_gen.om_rows(defs, 24, 2, None, words_only=True)["state_sched.py"]
        open(os.path.join(sd, "state_sched.py"), "w").write(words)
        check(not om_gen.load_schema([sd]), "--om-words: no state.sched objects")
        check(om_gen.SCHEMA["enums"]["sstate"] == ["none"] + [s.name for s in states], "--om-words: the words stay")


def main():
    update = "--update" in sys.argv
    os.chdir(HERE)
    test_golden(update)
    test_compiles()
    test_spacing()
    test_sequence_def()
    test_broken()
    test_step_macros_reach()
    test_cross_module_warning()
    test_sweep()
    test_sweep_checks()
    test_om_rows()
    print(f"sched_gen: {checks} checks, {fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
