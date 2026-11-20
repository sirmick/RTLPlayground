#!/usr/bin/env python3
"""
sched_gen.py - generate the scheduler's tables from the modules' .def files.

  sched_gen.py [--slots N] [--fill K] [--sequence FILE] -o OUTDIR X.def...

Each module has a .def, written by tools/sched_sweep.py from the module's
TASK/STEP lines (doc/sched/generator.md), or by hand:

  # task <name> fixed|job [freq=high|medium|once] [count=N] [first=<STATE>]
  # state <STATE> <step_function> [entry]
  # source <file>              (more of the module's sources, beside the .def)
  task stp fixed freq=medium first=STP_STEP
  state STP_STEP      stp_step
  state STP_SECOND    stp_second

A state belongs to the task above it. Fixed tasks get slots 0.. in the order
of the .def files and of their lines; count=N gives N slots (<name>0..).
Every slot after them is dynamic: task_claim() hands those to jobs, and the
run sequence names them as job placeholders.

The checks fail the build: a step function that has no prototype in the
module's sources, isn't __banked, is static or isn't uint8_t f(void) (a step
returns its next state, decision 22); two states of one name, or one
function behind two states; a state nothing reaches (a task's first state, a
`return S_X`, a STEP_SLEEP/WAIT/ARG(S_X, ...), task_claim() or
task_claim_arg() in the scanned sources, or 'entry'); more fixed slots than
--slots. A jump (return, STEP_*) naming another module's state is a
warning.

Writes OUTDIR/sched_gen.h, sched_gen.c (resident: step_addr, run_seq,
fixed_first) and sched_gen_names.c (banked: state_name, slot_name), and
prints the derived sequence's spacing.

With --om OMDIR, also the object model's view of the scheduler (DESIGN 7.3):
OUTDIR/sched_gen_tasks.h and .c (banked: state_task, a task per state,
1-based, 0 for none; task_name with SCHED_TASK_NAMES) and OMDIR/state_sched.py,
the state.sched rows for tools/om_gen.py: the slots, states and tasks by
name, their getters om_get_sched, om_get_sched_slot/_state/_task.
"""
import argparse, os, re, sys

FREQS = ("high", "medium", "once")
MEDIUM_TIMES = 3
STATE_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
IDENT_RE = re.compile(r"^[A-Za-z_]\w*$")
TASK_REF = re.compile(r"\b(return|STEP_SLEEP|STEP_WAIT|STEP_ARG|task_claim|task_claim_arg)\s*\(?\s*S_([A-Z][A-Z0-9_]*)\b")


class SpecError(Exception):
    pass


class Task:
    def __init__(self, name, kind, freq, count, first, where):
        self.name, self.kind, self.freq, self.count = name, kind, freq, count
        self.first, self.where = first, where
        self.states = []


class State:
    def __init__(self, name, fn, entry, task, module, where):
        self.name, self.fn, self.entry = name, fn, entry
        self.task, self.module, self.where = task, module, where


class Module:
    def __init__(self, path):
        self.path = path
        self.tasks = []
        self.sources = []       # existing files scanned for prototypes and jumps


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def parse_def(path):
    mod = Module(path)
    base = os.path.splitext(path)[0]
    extra = []
    task = None
    with open(path) as f:
        lines = f.read().splitlines()
    for no, raw in enumerate(lines, 1):
        line, _, note = raw.partition("#")
        line = line.split()
        # a line that says where it came from (tools/sched_sweep.py's
        # "# tasks.c:48") is named by that in the errors
        where = note.strip() if re.match(r"^\s*\S+:\d+\s*$", note) else f"{path}:{no}"
        if not line:
            continue
        kw, args = line[0], line[1:]
        if kw == "task":
            if len(args) < 2:
                raise SpecError(f"{where}: task needs a name and fixed|job")
            name, kind = args[0], args[1]
            if not re.match(r"^[a-z][a-z0-9_]*$", name):
                raise SpecError(f"{where}: task name '{name}' must be lower case (a-z, 0-9, _)")
            if kind not in ("fixed", "job"):
                raise SpecError(f"{where}: task {name}: '{kind}' is not fixed or job")
            opts = {}
            for a in args[2:]:
                k, eq, v = a.partition("=")
                if not eq or k not in ("freq", "count", "first") or k in opts:
                    raise SpecError(f"{where}: task {name}: bad option '{a}'")
                opts[k] = v
            freq = opts.get("freq", "once")
            if freq not in FREQS:
                raise SpecError(f"{where}: task {name}: freq '{freq}' is not high, medium or once")
            count = 1
            if "count" in opts:
                if kind == "job":
                    raise SpecError(f"{where}: task {name}: count= is for fixed tasks")
                if not opts["count"].isdigit() or not 1 <= int(opts["count"]) <= 16:
                    raise SpecError(f"{where}: task {name}: count '{opts['count']}' is not 1..16")
                count = int(opts["count"])
            task = Task(name, kind, freq, count, opts.get("first"), where)
            mod.tasks.append(task)
        elif kw == "state":
            if task is None:
                raise SpecError(f"{where}: state before any task")
            if len(args) not in (2, 3) or (len(args) == 3 and args[2] != "entry"):
                raise SpecError(f"{where}: state needs <STATE> <function> [entry]")
            if not STATE_RE.match(args[0]):
                raise SpecError(f"{where}: state '{args[0]}' must be upper case (A-Z, 0-9, _)")
            if not IDENT_RE.match(args[1]):
                raise SpecError(f"{where}: '{args[1]}' is not a function name")
            task.states.append(State(args[0], args[1], len(args) == 3, task, mod, where))
        elif kw == "source":
            if len(args) != 1:
                raise SpecError(f"{where}: source needs one file name")
            p = os.path.join(os.path.dirname(path), args[0])
            if not os.path.isfile(p):
                raise SpecError(f"{where}: source '{args[0]}' not found")
            extra.append(p)
        else:
            raise SpecError(f"{where}: unknown line '{kw}'")
    for t in mod.tasks:
        if not t.states:
            raise SpecError(f"{t.where}: task {t.name} has no states")
        if t.first is None:
            t.first = t.states[0].name
        elif t.first not in [s.name for s in t.states]:
            raise SpecError(f"{t.where}: task {t.name}: first={t.first} is not one of its states")
    mod.sources = [p for p in (base + ".h", base + ".c") if os.path.isfile(p)] + extra
    return mod


def check_function(st, texts):
    """The step function's prototype or definition, in the module's sources."""
    fn = st.fn
    pat = re.compile(r"(?m)^[ \t]*(?P<ret>[A-Za-z_][\w \t\*]*?)[ \t\*]+" + re.escape(fn)
                     + r"\s*\((?P<args>[^)]*)\)(?P<attr>[^;{}()=]*)[;{]")
    found = []
    for path, text in texts:
        for m in pat.finditer(text):
            ret = m.group("ret").split()
            if not ret or ret[-1] in ("return", "else", "do", "case", "goto"):
                continue
            found.append((path, ret, m.group("args").strip(), m.group("attr")))
    files = ", ".join(p for p, _ in texts) or "(no .h/.c beside the .def)"
    if not found:
        raise SpecError(f"{st.where}: state {st.name}: function {fn} has no prototype or definition in {files}")
    for path, ret, args, attr in found:
        if "static" in ret:
            raise SpecError(f"{st.where}: state {st.name}: {fn} is static in {path}; the table in sched_gen.c can't reach it")
        if "__banked" not in attr.split():
            raise SpecError(f"{st.where}: state {st.name}: {fn} is not __banked in {path}")
        if [w for w in ret if w not in ("extern",)] != ["uint8_t"] or args != "void":
            raise SpecError(f"{st.where}: state {st.name}: {fn} must be 'uint8_t {fn}(void) __banked' ({path})")


def load(defs, slots):
    mods = [parse_def(p) for p in defs]
    tasks = [t for m in mods for t in m.tasks]
    states = [s for t in tasks for s in t.states]

    seen = {}
    for t in tasks:
        if t.name in seen:
            raise SpecError(f"{t.where}: task {t.name} also defined at {seen[t.name]}")
        seen[t.name] = t.where
    seen, fns = {}, {}
    for s in states:
        if s.name == "NONE" or s.name == "COUNT":
            raise SpecError(f"{s.where}: state name {s.name} is reserved")
        if s.name in seen:
            raise SpecError(f"{s.where}: state {s.name} also defined at {seen[s.name].where}")
        if s.fn in fns:
            raise SpecError(f"{s.where}: function {s.fn} is behind two states, {fns[s.fn].name} and {s.name}")
        seen[s.name], fns[s.fn] = s, s
    if len(states) + 1 > 255:
        raise SpecError(f"{len(states)} states; a state ID is one byte (at most 254)")

    texts = {}
    for m in mods:
        texts[m.path] = [(p, strip_comments(open(p).read())) for p in m.sources]
    for s in states:
        check_function(s, texts[s.module.path])

    # Who reaches whom: a task's first state, return/STEP_*/task_claim in the sources.
    reached = {t.first for t in tasks}
    warnings = []
    for m in mods:
        own = {s.name for t in m.tasks for s in t.states}
        for path, text in texts[m.path]:
            for no, line in enumerate(text.splitlines(), 1):
                for call, name in TASK_REF.findall(line):
                    reached.add(name)
                    if not call.startswith("task_claim") and name in seen and name not in own:
                        warnings.append(f"{path}:{no}: warning: " + (f"return S_{name}" if call == "return" else f"{call}(S_{name})") + " jumps into "
                                        f"{seen[name].module.path}; cross-module jumps go through events")
    for s in states:
        if s.name not in reached and not s.entry:
            raise SpecError(f"{s.where}: state {s.name} is reached by no return/STEP_*/task_claim and is no task's first state (mark it 'entry', in a TASK line entry=<step>, if task_claim reaches it some other way)")

    fixed = []
    for t in tasks:
        if t.kind == "fixed":
            for i in range(t.count):
                fixed.append((t.name if t.count == 1 else f"{t.name}{i}", t))
    names = {}
    for n, t in fixed:
        if n in names or re.match(r"^job\d+$", n) or n in ("fixed", "slots"):
            raise SpecError(f"{t.where}: slot name {n} clashes with "
                            + (f"{names[n].where}" if n in names else
                               "the dynamic slots' job<N>" if n.startswith("job") else
                               f"the generated T_{n.upper()}"))
        names[n] = t
    if len(fixed) > slots:
        raise SpecError(f"{len(fixed)} fixed slots, more than the {slots} slots (--slots)")
    jobs = [t for t in tasks if t.kind == "job"]
    if jobs and len(fixed) == slots:
        raise SpecError(f"{len(fixed)} fixed slots fill all {slots} slots: none is left for the jobs")
    return mods, tasks, states, fixed, jobs, warnings


def slot_table(fixed, jobs, slots):
    """(name, freq) per slot: fixed, then the dynamic slots at the jobs' freq."""
    jfreq = "once"
    for t in jobs:
        if FREQS.index(t.freq) < FREQS.index(jfreq):
            jfreq = t.freq
    table = [(name, t.freq) for name, t in fixed]
    table += [(f"job{i}", jfreq) for i in range(slots - len(fixed))]
    return table


def derive(table, fill):
    """High slots as a group G; between two G, `fill` other positions:
    medium slots MEDIUM_TIMES each, spread out, the rest once."""
    high = [i for i, (_, f) in enumerate(table) if f == "high"]
    medium = [i for i, (_, f) in enumerate(table) if f == "medium"]
    once = [i for i, (_, f) in enumerate(table) if f == "once"]
    n = MEDIUM_TIMES * len(medium) + len(once)
    items = [None] * n
    for j, slot in enumerate(medium):
        for k in range(MEDIUM_TIMES):
            p = (k * n) // MEDIUM_TIMES + (j * n) // (MEDIUM_TIMES * len(medium))
            while items[p % n] is not None:
                p += 1
            items[p % n] = slot
    it = iter(once)
    items = [x if x is not None else next(it) for x in items]
    if not high:
        return items
    seq = []
    for c in range(0, max(n, 1), fill):
        seq += high + items[c:c + fill]
    return seq


def read_sequence(path, table, nfixed):
    names = {name: i for i, (name, _) in enumerate(table[:nfixed])}
    seq, jobs = [], 0
    with open(path) as f:
        for no, raw in enumerate(f, 1):
            for tok in raw.split("#", 1)[0].split():
                if tok == "job":
                    seq.append(None)
                    jobs += 1
                elif tok in names:
                    seq.append(names[tok])
                else:
                    raise SpecError(f"{path}:{no}: '{tok}' is not a fixed slot or 'job'")
    ndyn = len(table) - nfixed
    if jobs and not ndyn:
        raise SpecError(f"{path}: job placeholders, but no dynamic slots")
    if jobs > ndyn:
        raise SpecError(f"{path}: {jobs} job placeholders for {ndyn} dynamic slots: one per slot")
    if ndyn and jobs < ndyn:
        raise SpecError(f"{path}: {jobs} job placeholders for {ndyn} dynamic slots: a claimed slot would never run")
    missing = [n for n, i in names.items() if i not in seq]
    if missing:
        raise SpecError(f"{path}: never runs: {', '.join(missing)}")
    nxt = 0
    for i, x in enumerate(seq):
        if x is None:
            seq[i] = nfixed + nxt % ndyn if ndyn else None
            nxt += 1
    return [x for x in seq if x is not None]


def spacing(seq, table):
    n = len(seq)
    rows = []
    for i, (name, freq) in enumerate(table):
        pos = [p for p, s in enumerate(seq) if s == i]
        gaps = [(pos[(k + 1) % len(pos)] - p) % n or n for k, p in enumerate(pos)]
        rows.append((name, freq, len(pos), max(gaps)))
    return rows


def c_list(vals, per=16, indent="\t"):
    vals = [str(v) for v in vals]
    return ",\n".join(indent + ", ".join(vals[i:i + per]) for i in range(0, len(vals), per))


def generate(defs, slots, fill, sequence):
    mods, tasks, states, fixed, jobs, warnings = load(defs, slots)
    table = slot_table(fixed, jobs, slots)
    seq = read_sequence(sequence, table, len(fixed)) if sequence else derive(table, fill)
    if not seq:
        raise SpecError("the run sequence is empty: no tasks")
    if len(seq) > 255:
        raise SpecError(f"run sequence of {len(seq)} positions; RUN_SEQ_LEN must fit a byte")
    rows = spacing(seq, table)

    src = " ".join(defs) + (f" {sequence}" if sequence else "")
    head = f"/* Generated by tools/sched_gen.py from {src}; do not edit. */\n"
    sname = lambda s: f"S_{s}"
    tname = lambda n: f"T_{n.upper()}"
    report = [f"run sequence: {len(seq)} positions, {len(table)} slots "
              f"({len(fixed)} fixed, {len(table) - len(fixed)} dynamic)"
              + ("" if sequence else f", {fill} between high groups"),
              f"{'slot':<10} {'freq':<7} {'runs':>4} {'worst gap':>9}"]
    report += [f"{n:<10} {f:<7} {c:>4} {g:>9}" for n, f, c, g in rows]

    h = [head, "#ifndef SCHED_GEN_H", "#define SCHED_GEN_H", "", "#include <stdint.h>", ""]
    h.append("enum {\n\tS_NONE,\n" + "".join(f"\t{sname(s.name)},\n" for s in states) + "\tS_COUNT\n};\n")
    tl = [f"\t{tname(n)},\n" for n, _ in table[:len(fixed)]]
    tl.append("\tT_FIXED,\n")
    tl += [f"\t{tname(n)}{' = T_FIXED' if i == 0 else ''},\n" for i, (n, _) in enumerate(table[len(fixed):])]
    tl.append("\tT_SLOTS\n")
    h.append("enum {\n" + "".join(tl) + "};\n")
    h.append(f"#define SCHED_GEN_SLOTS {slots}")
    h.append(f"#define RUN_SEQ_LEN {len(seq)}\n")
    h.append("typedef uint8_t (*sched_step_fn)(void) __banked;\t/* returns the next state */\n")
    h.append("extern __code const sched_step_fn step_addr[S_COUNT];")
    h.append("extern __code const uint8_t run_seq[RUN_SEQ_LEN];")
    h.append("extern __code const uint8_t fixed_first[T_FIXED];")
    h.append("/* in a bank: read them only from code placed with sched_gen_names.c */")
    h.append("extern __code const char * __code const state_name[S_COUNT];")
    h.append("extern __code const char * __code const slot_name[T_SLOTS];\n")
    h.append("#endif")

    c = [head, "/*", " * " + "\n * ".join(report), " */", "", '#include "sched_gen.h"', ""]
    c += [f"uint8_t {s.fn}(void) __banked;" for s in states]
    c.append("\n/* 24-bit address per state: low, high, bank (the linker fills in all three) */")
    c.append("__code const sched_step_fn step_addr[S_COUNT] = {\n\t0,\n"
             + "".join(f"\t{s.fn},\t/* {sname(s.name)} */\n" for s in states) + "};\n")
    c.append("__code const uint8_t run_seq[RUN_SEQ_LEN] = {\n"
             + c_list([tname(table[i][0]) for i in seq], per=8) + "\n};\n")
    first = {t.name: t.first for t in tasks}
    c.append("__code const uint8_t fixed_first[T_FIXED] = {\n"
             + "".join(f"\t{sname(first[t.name])},\t/* {tname(n)} */\n" for n, t in fixed) + "};")

    nm = [head, '#include "sched_gen.h"', ""]
    nm.append("__code const char * __code const state_name[S_COUNT] = {\n\t\"-\",\n"
              + "".join(f"\t\"{s.name}\",\n" for s in states) + "};\n")
    nm.append("__code const char * __code const slot_name[T_SLOTS] = {\n"
              + "".join(f"\t\"{n}\",\n" for n, _ in table) + "};")
    return {"sched_gen.h": "\n".join(h) + "\n", "sched_gen.c": "\n".join(c) + "\n",
            "sched_gen_names.c": "\n".join(nm) + "\n"}, report, warnings


def om_rows(defs, slots, fill, sequence):
    """{name: text}: sched_gen_tasks.h/.c and state_sched.py (--om)."""
    mods, tasks, states, fixed, jobs, warnings = load(defs, slots)
    table = slot_table(fixed, jobs, slots)
    src = " ".join(defs)
    head = f"/* Generated by tools/sched_gen.py --om from {src}; do not edit. */\n"
    tix = {t.name: i + 1 for i, t in enumerate(tasks)}
    h = [head, "#ifndef SCHED_GEN_TASKS_H", "#define SCHED_GEN_TASKS_H", "", '#include "sched_gen.h"', "",
         f"#define SCHED_TASKS {len(tasks)}",
         "/* in a bank, as sched_gen_names.c: a state's task, 1-based (0: none) */",
         "extern __code const uint8_t state_task[S_COUNT];",
         "#ifdef SCHED_TASK_NAMES",
         "extern __code const char * __code const task_name[SCHED_TASKS + 1];",
         "#endif", "", "#endif"]
    c = [head, '#include "sched_gen_tasks.h"', ""]
    c.append("__code const uint8_t state_task[S_COUNT] = {\n\t0,\n"
             + "".join(f"\t{tix[s.task.name]},\t/* S_{s.name}: {s.task.name} */\n" for s in states) + "};\n")
    c.append("#ifdef SCHED_TASK_NAMES")
    c.append("__code const char * __code const task_name[SCHED_TASKS + 1] = {\n\t\"-\",\n"
             + "".join(f"\t\"{t.name}\",\n" for t in tasks) + "};")
    c.append("#endif")
    w = lambda xs: " ".join(xs)
    sn = [s.name for s in states]
    tn = [t.name for t in tasks]
    py = [f"# Generated by tools/sched_gen.py --om from {src}; do not edit.",
          "# state.sched (DESIGN 7.3): the scheduler's totals, then its slots, states",
          "# and tasks by name. A state or a task word 0, none: a free slot.",
          "",
          f'SLOT = Enum("sslot", "{w(n for n, _ in table)}")',
          f'STATE = Enum("sstate", "none {w(sn)}")',
          f'STATES = Enum("sstates", "{w(sn)}")\t# the same words, past none',
          f'TASK = Enum("stask", "none {w(tn)}")',
          f'TASKS = Enum("stasks", "{w(tn)}")',
          "",
          'state("sched", get="om_get_sched")',
          'st("passes", U32())',
          'st("idle", U32(), tip="Passes that found nothing due")',
          'st("pass_max_us", U32())',
          'st("overruns", U16(), tip="Steps of 50 ms or more")',
          'st("overrun_last", STATE)',
          'st("slots", U8())',
          'st("fixed", U8())',
          'st("fine_per_tick", U16(), tip="Timer 2 fine units (6.144 us) a 5 ms tick measured")',
          "",
          'state("sched.slot", count=SLOT, get="om_get_sched_slot")',
          'st("task", TASK)',
          'st("state", STATE)',
          'st("mask", Flags("smask", "rx link con_rx con_tx b4 b5 b6 forever"), tip="The events that wake it")',
          'st("wake", U16(), tip="Ticks (5 ms) to its wake, 0 if due")',
          'st("arg", U16())',
          "",
          'state("sched.state", count=STATES, get="om_get_sched_state")',
          'st("runs", U16(), tip="Saturates at 65535")',
          'st("max_us", U32(), tip="The longest; 402630 and more: past 400 ms")',
          "",
          'state("sched.task", count=TASKS, get="om_get_sched_task")',
          'st("max_us", U32(), tip="The longest of its states")']
    return {"sched_gen_tasks.h": "\n".join(h) + "\n", "sched_gen_tasks.c": "\n".join(c) + "\n",
            "state_sched.py": "\n".join(py) + "\n"}


def write(d, name, text):
    os.makedirs(d, exist_ok=True)
    p = os.path.join(d, name)
    old = open(p).read() if os.path.exists(p) else None
    if old != text:                 # untouched outputs keep their timestamps
        with open(p, "w") as f:
            f.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description="generate the scheduler tables from .def files")
    ap.add_argument("-o", "--outdir", required=True)
    ap.add_argument("--slots", type=int, default=24, help="SCHED_SLOTS (default 24)")
    ap.add_argument("--fill", type=int, default=2, help="positions between two high groups (default 2)")
    ap.add_argument("--sequence", help="sequence.def: an explicit run sequence")
    ap.add_argument("--om", metavar="OMDIR", help="also the object model's state.sched rows (state_sched.py) "
                    "into OMDIR, and sched_gen_tasks.h/.c")
    ap.add_argument("defs", nargs="+")
    a = ap.parse_args(argv)
    if not 1 <= a.slots <= 64 or a.fill < 1:
        ap.error("--slots is 1..64 and --fill at least 1")
    try:
        out, report, warnings = generate(a.defs, a.slots, a.fill, a.sequence)
        om = om_rows(a.defs, a.slots, a.fill, a.sequence) if a.om else {}
    except SpecError as e:
        print(f"sched_gen: error: {e}", file=sys.stderr)
        return 1
    for w in warnings:
        print(w, file=sys.stderr)
    for name, text in out.items():
        write(a.outdir, name, text)
    for name, text in om.items():
        write(a.om if name.endswith(".py") else a.outdir, name, text)
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
