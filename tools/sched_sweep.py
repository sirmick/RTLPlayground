#!/usr/bin/env python3
"""
sched_sweep.py - the tasks' .def files from their inline TASK/STEP markers.

  sched_sweep.py -o OUTDIR [--cpp 'CMD'] [--all FILE.c...] -- SOURCE.c...

Each scheduler source declares its tasks where they are written (sched.h,
doc/sched/generator.md):

  TASK(stp, fixed, medium)
  STEP(stp, tick) { ... }          void stp_tick(void) __banked, S_STP_TICK

The sweep runs CMD (the build's preprocessor and flags; -DSCHED_SWEEP is
added) over each source, so a step behind an #ifdef is in or out as the
build has it. With SCHED_SWEEP the markers survive preprocessing as strings.
It writes OUTDIR/<source>.def, the format sched_gen.py reads, and
OUTDIR/<source>.i, the preprocessed text without the markers, which the .def
names as its `source`: sched_gen.py checks the step prototypes and finds the
states each step names there. A file that comes out unchanged keeps its
timestamp. The .def files are listed on stdout, in the order of the sources.

Two checks on the steps themselves (decision 22):
- A step's `return` that calls a function (other than STEP_SLEEP/WAIT/ARG,
  which stay as written in the sweep) lets a helper choose the state. It
  fails, unless the source line of that return, or the line above it, says
  so: /* helper returns state: <function> */.
- --all names every source of the build: one with a STEP or TASK line
  that isn't a step source is an error (it would compile, and silently not
  be a step).

A TASK's options: fixed|job, high|medium|once, then count=N, first=<step>,
entry=<step> (a state no step names, reached only through task_claim from
elsewhere), in any order. Its tasks take slots in the order of their TASK
lines; a task's states come in the order of its STEPs. The errors (each with
the source line): a STEP with no TASK of that name in its file, a task with
no STEP, a bad option, two TASKs of one name.
"""
import argparse, os, re, shlex, subprocess, sys

CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
NOT_A_HELPER = {"STEP_SLEEP", "STEP_WAIT", "STEP_ARG", "sizeof"}
HELPER_NOTE = re.compile(r"/\*\s*helper returns state:\s*(\w+)\s*\*/")
DECL = re.compile(r"\b(STEP|TASK)\s*\(")
MARK = re.compile(r'__sched_task__\s*"((?:[^"\\]|\\.)*)"|__sched_step__\s*"(\w*)"\s*"(\w*)"')
LINE = re.compile(r'^#\s*(?:line\s+)?(\d+)\s+"([^"]*)"')
NAME = re.compile(r"^[a-z][a-z0-9_]*$")
STEP_NAME = re.compile(r"^[a-z0-9][a-z0-9_]*$")


class SweepError(Exception):
    pass


def where_of(text, pos, fallback):
    """file:line of `pos` in preprocessed text, from cpp's line markers."""
    path, no = fallback, 1
    for ln in text[:pos].split("\n"):
        m = LINE.match(ln)
        if m:
            path, no = m.group(2), int(m.group(1))
        else:
            no += 1
    return f"{path}:{no - 1}"


def skip_literal(text, i):
    """The index past the string or char literal starting at text[i]."""
    q, i = text[i], i + 1
    while i < len(text) and text[i] != q:
        i += 2 if text[i] == "\\" else 1
    return i + 1


def body_of(text, i):
    """(start, end) of the braces of the function after text[i]."""
    start = text.index("{", i)
    depth, j = 0, start
    while j < len(text):
        c = text[j]
        if c in "\"'":
            j = skip_literal(text, j)
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if not depth:
                return start, j
        j += 1
    raise SweepError("unbalanced braces")


def source_line(where, cache):
    path, _, no = where.rpartition(":")
    if path not in cache:
        cache[path] = open(path).read().split("\n") if os.path.isfile(path) else []
    lines, n = cache[path], int(no)
    return " ".join(lines[max(n - 2, 0):n])


def check_returns(text, src):
    """No step's return lets a helper choose the state, unless the source says so."""
    cache = {}
    for m in MARK.finditer(text):
        if m.group(1) is not None:
            continue
        t, s = m.group(2), m.group(3)
        start, end = body_of(text, m.end())
        for r in re.finditer(r"\breturn\b", text[start:end]):
            i = start + r.end()
            depth, j = 0, i
            while j < end and not (text[j] == ";" and depth == 0):
                if text[j] in "\"'":
                    j = skip_literal(text, j)
                    continue
                depth += {"(": 1, ")": -1}.get(text[j], 0)
                j += 1
            for c in CALL.finditer(text[i:j]):
                fn = c.group(1)
                if fn in NOT_A_HELPER:
                    continue
                where = where_of(text, i, src)
                note = HELPER_NOTE.search(source_line(where, cache))
                if not note or note.group(1) != fn:
                    raise SweepError(f"{where}: STEP({t}, {s}) returns what {fn}() says: a helper chooses the "
                                     f"state (decision 22). Return the state in the step, or mark the line "
                                     f"/* helper returns state: {fn} */")


def check_all(files, sources):
    """A STEP or TASK line in a source of the build that isn't swept."""
    swept = {os.path.normpath(p) for p in sources}
    for p in files:
        if os.path.normpath(p) in swept or not os.path.isfile(p):
            continue
        text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), open(p).read(), flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
        for no, line in enumerate(text.split("\n"), 1):
            m = DECL.search(line)
            if m and not line.lstrip().startswith("#"):
                raise SweepError(f"{p}:{no}: {m.group(1)}(...) in a source that isn't a step source "
                                 f"(SCHED_STEP_SRCS): it would not be a step")


def sweep(text, src):
    """(def text, the text without markers) of one preprocessed source."""
    tasks, order = {}, []
    for m in MARK.finditer(text):
        where = where_of(text, m.start(), src)
        if m.group(1) is not None:
            args = [a.strip() for a in m.group(1).split(",")]
            if len(args) < 3:
                raise SweepError(f"{where}: TASK({m.group(1)}) needs a name, fixed|job and high|medium|once")
            name, kind, freq = args[:3]
            if not NAME.match(name):
                raise SweepError(f"{where}: TASK name '{name}' must be lower case (a-z, 0-9, _)")
            if name in tasks:
                raise SweepError(f"{where}: TASK({name}) also at {tasks[name]['where']}")
            if kind not in ("fixed", "job"):
                raise SweepError(f"{where}: TASK({name}): '{kind}' is not fixed or job")
            if freq not in ("high", "medium", "once"):
                raise SweepError(f"{where}: TASK({name}): '{freq}' is not high, medium or once")
            opts = {}
            for a in args[3:]:
                k, eq, v = a.partition("=")
                k, v = k.strip(), v.strip()
                if not eq or k not in ("count", "first", "entry") or k in opts or not v:
                    raise SweepError(f"{where}: TASK({name}): bad option '{a}'")
                if k != "count" and not STEP_NAME.match(v):
                    raise SweepError(f"{where}: TASK({name}): {k}={v} is not a step name")
                opts[k] = v
            tasks[name] = dict(kind=kind, freq=freq, opts=opts, where=where, states=[])
            order.append(name)
        else:
            t, s = m.group(2), m.group(3)
            if t not in tasks:
                raise SweepError(f"{where}: STEP({t}, {s}) has no TASK({t}, ...) above it in this file")
            if not STEP_NAME.match(s):
                raise SweepError(f"{where}: STEP({t}, {s}): the step name must be lower case")
            tasks[t]["states"].append((s, where))
    lines = [f"# generated by tools/sched_sweep.py from {src}; do not edit"]
    for name in order:
        t = tasks[name]
        if not t["states"]:
            raise SweepError(f"{t['where']}: TASK({name}) has no STEP({name}, ...)")
        have = [s for s, _ in t["states"]]
        for k in ("first", "entry"):
            if k in t["opts"] and t["opts"][k] not in have:
                raise SweepError(f"{t['where']}: TASK({name}): {k}={t['opts'][k]} is no STEP({name}, ...)")
        opt = [f"freq={t['freq']}"]
        if "count" in t["opts"]:
            opt.append(f"count={t['opts']['count']}")
        if "first" in t["opts"]:
            opt.append(f"first={name.upper()}_{t['opts']['first'].upper()}")
        lines.append(f"task {name} {t['kind']} {' '.join(opt)}\t# {t['where']}")
        for s, where in t["states"]:
            entry = " entry" if t["opts"].get("entry") == s else ""
            lines.append(f"state {name.upper()}_{s.upper()} {name}_{s}{entry}\t# {where}")
    check_returns(text, src)
    return lines, MARK.sub(" ", text)


def write(path, text):
    old = open(path).read() if os.path.exists(path) else None
    if old != text:                     # untouched outputs keep their timestamps
        with open(path, "w") as f:
            f.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description="the .def files from the TASK/STEP markers")
    ap.add_argument("-o", "--outdir", required=True)
    ap.add_argument("--cpp", default="cpp", help="the preprocessor and the build's flags (default cpp)")
    ap.add_argument("--all", nargs="*", default=[], help="every source of the build: no STEP/TASK outside SOURCE")
    ap.add_argument("sources", nargs="+")
    a = ap.parse_args(argv)
    try:
        check_all(a.all, a.sources)
    except SweepError as e:
        print(f"sched_sweep: error: {e}", file=sys.stderr)
        return 1
    os.makedirs(a.outdir, exist_ok=True)
    out, seen = [], {}
    for src in a.sources:
        stem = os.path.splitext(os.path.basename(src))[0]
        if stem in seen:
            print(f"sched_sweep: error: {src} and {seen[stem]} would both be {stem}.def", file=sys.stderr)
            return 1
        seen[stem] = src
        r = subprocess.run(shlex.split(a.cpp) + ["-DSCHED_SWEEP", src], capture_output=True, text=True)
        if r.returncode:
            sys.stderr.write(r.stderr)
            print(f"sched_sweep: error: preprocessing {src} failed", file=sys.stderr)
            return 1
        try:
            lines, plain = sweep(r.stdout, src)
        except SweepError as e:
            print(f"sched_sweep: error: {e}", file=sys.stderr)
            return 1
        if len(lines) == 1:
            print(f"sched_sweep: error: {src} has no TASK", file=sys.stderr)
            return 1
        lines.append(f"source {stem}.i")
        write(os.path.join(a.outdir, stem + ".i"), plain)
        d = os.path.join(a.outdir, stem + ".def")
        write(d, "\n".join(lines) + "\n")
        out.append(d)
    print(" ".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
