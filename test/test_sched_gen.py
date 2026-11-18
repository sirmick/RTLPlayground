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

  python3 test_sched_gen.py [--update]
"""
import contextlib, io, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import sched_gen                                        # noqa: E402

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
        main += [f"void {s.fn}(void) {{ }}" for s in states]
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
        protos = "".join(f"void {f}(void) __banked;\n" for f in fns)
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
        ):
            open(p, "w").write(text + "\n")
            r = gen(defs, sequence=p)
            check(isinstance(r, str) and msg in r, f"sequence.def: want '{msg}', got {said(r)}")


BROKEN = [
    # (what, {module: (def, header or None for matching prototypes, .c body)}, slots, message)
    ("no prototype", {"m": ("task a fixed\nstate A_S a_s\n", "", "")}, 24,
     "function a_s has no prototype or definition"),
    ("not __banked", {"m": ("task a fixed\nstate A_S a_s\n", "void a_s(void);\n", "")}, 24,
     "a_s is not __banked"),
    ("static", {"m": ("task a fixed\nstate A_S a_s\n", "", "static void a_s(void) __banked\n{\n}\n")}, 24,
     "a_s is static"),
    ("takes a parameter", {"m": ("task a fixed\nstate A_S a_s\n", "void a_s(uint8_t p) __banked;\n", "")}, 24,
     "must be 'void a_s(void) __banked'"),
    ("returns a value", {"m": ("task a fixed\nstate A_S a_s\n", "uint8_t a_s(void) __banked;\n", "")}, 24,
     "must be 'void a_s(void) __banked'"),
    ("duplicate state", {"m": ("task a fixed\nstate A_S a_s\n", None, ""),
                         "n": ("task b fixed\nstate A_S b_s\n", None, "")}, 24,
     "state A_S also defined at"),
    ("one function, two states", {"m": ("task a fixed\nstate A_S a_s\nstate A_T a_s entry\n", None, "")}, 24,
     "function a_s is behind two states, A_S and A_T"),
    ("duplicate task", {"m": ("task a fixed\nstate A_S a_s\n", None, ""),
                        "n": ("task a fixed\nstate B_S b_s\n", None, "")}, 24,
     "task a also defined at"),
    ("unreachable", {"m": ("task a fixed\nstate A_S a_s\nstate A_T a_t\n", None, "")}, 24,
     "state A_T is reached by no task_next/task_claim"),
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
        write_module(d, "m", "task a fixed\nstate A_S a_s\n", "void a_s(void);\n")
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            rc = sched_gen.main(["-o", d, os.path.join(d, "m.def")])
        check(rc == 1 and "sched_gen: error:" in err.getvalue() and "not __banked" in err.getvalue(),
              f"command line: rc {rc}, stderr {err.getvalue()!r}")
        check(not os.path.exists(os.path.join(d, "sched_gen.h")), "a failed run writes no output")


def test_cross_module_warning():
    with tempfile.TemporaryDirectory() as d:
        write_module(d, "m", "task a fixed\nstate A_S a_s\n", body="void a_s(void) __banked\n{\n\ttask_next(S_B_T);\n}\n")
        write_module(d, "n", "task b fixed\nstate B_S b_s\nstate B_T b_t\n")
        r = gen([os.path.join(d, "m.def"), os.path.join(d, "n.def")])
        check(not isinstance(r, str), f"cross-module jump fails the build: {r}")
        if not isinstance(r, str):
            w = r[2]
            check(len(w) == 1 and "m.c:3: warning: task_next(S_B_T) jumps into" in w[0], f"warning: {w}")


def main():
    update = "--update" in sys.argv
    os.chdir(HERE)
    test_golden(update)
    test_compiles()
    test_spacing()
    test_sequence_def()
    test_broken()
    test_cross_module_warning()
    print(f"sched_gen: {checks} checks, {fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
