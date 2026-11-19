# 5. The generator

Two passes, run by the Makefile before compiling, like the `html_data`
step: `tools/sched_sweep.py` collects the tasks from the sources, and
`tools/sched_gen.py` makes the tables. Output: `sched_gen.h`, `sched_gen.c`
and `sched_gen_names.c` in the build directory (decision 24).

## Declaring tasks: TASK and STEP

A task and its steps are declared where they are written (`sched.h`):

```c
TASK(stp, fixed, medium, first=step)	/* no semicolon */
STEP(stp, step)				/* uint8_t stp_step(void) __banked, state S_STP_STEP */
{
	...
}
STEP(stp, second) { ... }		/* S_STP_SECOND */

TASK(l2dump, job, once)
STEP(l2dump, first) { ... }
STEP(l2dump, more) { ... }
```

`TASK(name, fixed|job, high|medium|once [, count=N] [, first=step]
[, entry=step])`. Tasks take their slots in the order of their `TASK` lines,
file by file in the order the Makefile names the step sources
(`SCHED_STEP_SRCS`: `tasks.c flash_job.c`; schedtest's `st_main.c st_b1.c
st_b2.c st_b3.c`); a task's states come in the order of its `STEP`s, and its
first `STEP` is its entry unless `first=` names another. A `STEP` needs its
`TASK` above it in the same file. `entry=` marks a state no step names
(below).

**The sweep** (`tools/sched_sweep.py`) runs the build's own preprocessor
and flags over each step source with `-DSCHED_SWEEP`, so a step behind an
`#ifdef` is in or out exactly as the build has it. In that mode `sched.h`
turns the markers into strings that survive preprocessing; the sweep reads
them and writes, per source, `<builddir>/sched/<source>.def` in the
generator's input format below and `<source>.i`, the preprocessed text
without the markers, which the `.def` names as its `source` (the generator
checks the prototypes and finds the jumps there). Each `.def` line ends
with the source line it came from, and the generator's errors name that
line. Both are build intermediates, not committed; the sweep runs at every
make and rewrites only what changed, the generator runs when a `.def` or `.i`
changed and rewrites only the tables that changed, so an edit that moves no
task or state recompiles nothing but its own file. The sweep's errors: a
`STEP` with no `TASK` above it in its file, a `TASK` with no `STEP`, a bad
field or option, two `TASK`s of one name, a step source with no `TASK`.

The host tests sweep `test_sched.c` the same way with gcc's preprocessor
(test/Makefile). `sequence.def` stays a file.

## The generator's input format

What the sweep writes (and what the generator's own tests, `test/sched_gen/`,
are written in by hand):

```
# task <name> fixed|job [freq=high|medium|once] [count=N] [first=<STATE>]
# state <STATE> <step_function> [entry]
# source <file>     (more of the module's sources, beside the .def)
task stp fixed freq=medium first=STP_STEP	# tasks.c:258
state STP_STEP      stp_step			# tasks.c:259
state STP_SECOND    stp_second
```

- `count=N` makes N fixed instances (one per SFP slot): N slots, all
  starting in `first`; the step tells them apart by `sched_cur`.
- Frequencies: **high** = in every group (below: a few steps apart),
  **medium** = 3 times per pass, **once** = once per pass. Jobs take `once` unless they say
  otherwise.
- `first=` defaults to the task's first state. Task names are lower case,
  states upper case. A comment `# <file>:<line>` names where a line came from
  in the errors.
- Slots: fixed tasks first, in the order of the `.def` files on the command
  line and of their lines; `count=N` gives `<name>0..`. Every slot after
  them (`T_FIXED` up) is dynamic: `task_claim()` takes those. A job has no
  slot; the dynamic slots run at the highest `freq` any job asks for.
- `sequence.def` may override the derived order with an explicit list of
  slot names (`sfp0`, not `sfp`) and `job` placeholders, whitespace
  separated, `#` comments. Every fixed slot must appear, and there must be
  exactly one `job` per dynamic slot (they are dealt out in order).

## Output

`tools/sched_gen.py [--slots 24] [--fill 2] [--sequence FILE] -o OUTDIR X.def...`
writes three files (an unchanged file keeps its timestamp) and prints the
spacing report, which also heads `sched_gen.c`. A golden example:
`test/sched_gen/golden/`.

```c
/* sched_gen.h */
enum { S_NONE, S_STP_STEP, S_STP_SECOND, S_L2_FIRST, S_L2_MORE, ..., S_COUNT };
enum { T_STP, T_LACP, T_SFP0, T_SFP1, ..., T_FIXED, T_JOB0 = T_FIXED, ..., T_SLOTS };
#define SCHED_GEN_SLOTS 24
#define RUN_SEQ_LEN 72
typedef uint8_t (*sched_step_fn)(void) __banked;	/* returns the next state */

/* sched_gen.c: resident (common area) */
__code const sched_step_fn step_addr[S_COUNT] = { 0, stp_step, ... };
__code const uint8_t run_seq[RUN_SEQ_LEN] = { T_RX, T_LINK, ... };
__code const uint8_t fixed_first[T_FIXED] = { S_STP_STEP, ... };

/* sched_gen_names.c: banked (--codeseg/--constseg BANKED) */
__code const char * __code const state_name[S_COUNT] = { "-", "STP_STEP", ... };
__code const char * __code const slot_name[T_SLOTS] = { "stp", ..., "job0", ... };
```

The bank byte comes from the linker, as in SDCC's own banked calls: a
`__banked` function-pointer type is 3 bytes, and SDCC emits each entry as
`.byte _f, (_f >> 8), (_f >> 16)` (decision 16; casts of a function address
are not constant in SDCC). `step_addr[st]()` compiles to a
`__sdcc_banked_call` with no internal RAM.

Where the tables live: `run_seq` and `step_addr` in the common area (read on
every dispatch, and small: `RUN_SEQ_LEN` + 3 per state). `state_name` and
`slot_name` in a bank (only `health` reads them): place_banks.py puts the
file that reads them in the same bank, as for any `__code` data read
directly.

## Checks (the build fails on any)

- A state named in a `.def` whose function doesn't exist (link error), or
  isn't `__banked` (the generator checks the prototype in the header; with
  `STEP()` that holds by construction, the check stays for hand-written
  `.def` files).
- Two states with the same name, or one function used by two states.
- A step function that is `static` or not `uint8_t f(void)` (rule 4, chapter 12).
- A state no task can reach, unless marked `entry`: reached means a task's
  first state, or named in a `return S_X`, `STEP_SLEEP/WAIT/ARG(S_X, ...)`,
  `task_claim(S_X)` or `task_claim_arg(S_X, ...)` in the
  scanned sources (the `.h`/`.c` beside the `.def` and its `source` lines).
- More fixed slots than `SCHED_SLOTS`, or none left over when there are jobs;
  two slots of one name, or a slot named `fixed`, `slots` or `job<N>` (its
  enum would clash with `T_FIXED`, `T_SLOTS`, `T_JOB<N>`).
- A `return S_X` or `STEP_*(S_X ...)` in a module's source naming a state of another module
  (warning: cross-module jumps go through events).

## Sequence derivation

1. Put every **high** task in a group G; between two G, `--fill` (default
   2) other positions. With 4 high tasks that is every 6th position, not
   every 3rd: what bounds a high task's latency is the steps between its
   runs (G - 1 + fill), not the positions.
2. Spread **medium** tasks over the other positions, 3 appearances each.
3. Fill the rest with **once** tasks and the dynamic slots, in slot order.
4. Report each task's spacing (worst gap in positions); `health` multiplies
   by the measured longest step to give each task's worst latency.

Host unit tests (B1): parse, the checks with broken inputs, the derived
sequence for a known input, and a golden output file.
