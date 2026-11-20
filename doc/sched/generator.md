# 5. The generator

Two passes, run by `sched.mk` before compiling: `tools/sched_sweep.py`
collects the tasks from the sources, and `tools/sched_gen.py` makes the
tables. Output: `sched_gen.h`, `sched_gen.c` and `sched_gen_names.c` in the
build directory (`<builddir>/sched/`), with the report in `sequence.txt`
(decision 24).

## Declaring tasks: TASK and STEP

A task and its steps are declared where they are written (`sched.h`). From
the firmware:

```c
TASK(loop, fixed, high, first=start)	/* no semicolon */
STEP(loop, start)			/* uint8_t loop_start(void) __banked, state S_LOOP_START */
{
	...
}
STEP(loop, poll) { ... }		/* S_LOOP_POLL */

TASK(fl, job, once, first=prog)		/* flash_job.c */
STEP(fl, erase) { ... }
STEP(fl, prog) { ... }
```

`TASK(name, fixed|job, high|medium|once [, count=N] [, first=step]
[, entry=step])`. Tasks take their slots in the order of their `TASK`
lines, file by file in the order `sched.mk` names the step sources
(`SCHED_STEP_SRCS`: `tasks.c flash_job.c`; schedtest's `st_main.c st_b1.c
st_b2.c st_b3.c`, and a negative variant's file). A task's states come in
the order of its `STEP`s, and its first `STEP` is its entry unless `first=`
names another. A `STEP` needs its `TASK` above it in the same file.
`entry=` marks a state no step names (below).

**The sweep** (`tools/sched_sweep.py`) runs the build's own preprocessor
and flags over each step source with `-DSCHED_SWEEP`, so a step behind an
`#ifdef` is in or out exactly as the build has it. In that mode `sched.h`
turns the markers into strings that survive preprocessing; the sweep reads
them and writes, per source, `<builddir>/sched/<source>.def` in the
generator's input format below and `<source>.i`, the preprocessed text
without the markers, which the `.def` names as its `source` (the generator
checks the prototypes and finds the jumps there). Each `.def` line ends
with the source line it came from, and the generator's errors name that
line. Both are build intermediates, not committed. The sweep runs at every
make and rewrites only what changed; the generator runs when a `.def` or
`.i` changed and rewrites only the tables that changed. So an edit that
moves no task or state recompiles nothing but its own file, and a second
make compiles nothing (evidence/e5-step-api.txt).

The sweep's errors: a `STEP` with no `TASK` above it in its file, a `TASK`
with no `STEP`, a bad field or option, two `TASK`s of one name, a step
source with no `TASK`. Two more keep decision 22 checkable:

- **No helper chooses a state.** A step's `return` whose expression calls a
  function (other than `STEP_SLEEP/WAIT/ARG`, which stay as written in the
  sweep) fails, unless that source line or the one above says
  `/* helper returns state: <function> */`. Only the host test's generic
  steps say it. So the state a step goes to is written in the step: the
  SFP helpers return `SFP_MORE`/`SFP_DONE`/`SFP_FAIL`, and the step picks
  the state.
- **No STEP outside the step sources.** `sched.mk` passes every C source of
  the build (`--all $(SRCS)`); a `STEP(` or `TASK(` line (not in a comment
  or a `#define`) in one that isn't in `SCHED_STEP_SRCS` fails: it would
  compile and silently not be a step.

The host tests sweep `test_sched.c` the same way with gcc's preprocessor
(`test/Makefile`).

## The generator's input format

What the sweep writes (and what the generator's own tests, `test/sched_gen/`,
are written in by hand). The firmware's, from a build of 72cee59:

```
# task <name> fixed|job [freq=high|medium|once] [count=N] [first=<STATE>]
# state <STATE> <step_function> [entry]
# source <file>     (more of the module's sources, beside the .def)
task loop fixed freq=high first=LOOP_START	# tasks.c:73
state LOOP_START loop_start	# tasks.c:74
state LOOP_POLL loop_poll	# tasks.c:81
```

- `count=N` makes N fixed instances (`sfp`: one per SFP slot, `sfp0`,
  `sfp1`): N slots, all starting in `first`; the step tells them apart by
  `sched_cur`.
- Frequencies: **high** = in every group (below: a few steps apart),
  **medium** = 3 times per pass, **once** = once per pass. Jobs take `once`
  unless they say otherwise.
- `first=` defaults to the task's first state. Task names are lower case,
  states upper case. A comment `# <file>:<line>` names where a line came from
  in the errors.
- Slots: fixed tasks first, in the order of the `.def` files on the command
  line and of their lines; `count=N` gives `<name>0..`. Every slot after
  them (`T_FIXED` up) is dynamic: `task_claim()` takes those. A job has no
  slot; the dynamic slots run at the highest `freq` any job asks for.
- `--sequence FILE` may override the derived order with an explicit list of
  slot names (`sfp0`, not `sfp`) and `job` placeholders, whitespace
  separated, `#` comments. Every fixed slot must appear, and there must be
  exactly one `job` per dynamic slot (they are dealt out in order). Only the
  host test uses one (`test/sched_core/sequence.def`, with `--slots 8`); the
  firmware and schedtest derive theirs.

## Output

`tools/sched_gen.py [--slots 24] [--fill 2] [--sequence FILE] -o OUTDIR X.def...`
writes three files (an unchanged file keeps its timestamp) and prints the
spacing report, which also heads `sched_gen.c`. A golden example:
`test/sched_gen/golden/`. The firmware's, abridged:

```c
/* sched_gen.h */
enum { S_NONE, S_RX_IDLE, S_RX_STEP, S_LINK_READ, ..., S_FL_DONE, S_COUNT };
enum { T_RX, T_LINK, T_LOOP, T_CON, T_CLI, ..., T_HOUSE, T_FIXED, T_JOB0 = T_FIXED, ..., T_SLOTS };
#define SCHED_GEN_SLOTS 24
#define RUN_SEQ_LEN 72
typedef uint8_t (*sched_step_fn)(void) __banked;	/* returns the next state */

/* sched_gen.c: resident (common area) */
__code const sched_step_fn step_addr[S_COUNT] = { 0, rx_idle, rx_step, ... };
__code const uint8_t run_seq[RUN_SEQ_LEN] = { T_RX, T_LINK, T_LOOP, T_CON, T_STP, T_CLI, ... };
__code const uint8_t fixed_first[T_FIXED] = { S_RX_IDLE, ... };

/* sched_gen_names.c: banked */
__code const char * __code const state_name[S_COUNT] = { "-", "RX_IDLE", ... };
__code const char * __code const slot_name[T_SLOTS] = { "rx", ..., "job0", ... };
```

The bank byte comes from the linker, as in SDCC's own banked calls: a
`__banked` function-pointer type is 3 bytes, and SDCC emits each entry as
`.byte _f, (_f >> 8), (_f >> 16)` (decision 16; casts of a function address
are not constant in SDCC). `step_addr[st]()` compiles to a
`__sdcc_banked_call` with no internal RAM.

Where the tables live: `run_seq`, `step_addr` and `fixed_first` in the
common area (read on every dispatch; 171 B for the firmware's 29 states and
72 positions, fb-build.txt). `state_name` and `slot_name` in a bank (only
`health` reads them): place_banks.py puts `sched_health.c`, which reads
them, in the same bank, as for any `__code` data read directly.

## Checks (the build fails on any)

- A state whose function has no prototype in the module's sources, or
  isn't `__banked` (with `STEP()` that holds by construction; the check
  stays for hand-written `.def` files: schedtest's old `bad_spec`).
- Two states with the same name, or one function used by two states.
- A step function that is `static` or not `uint8_t f(void)` (rule 4,
  chapter 12).
- A state no task can reach, unless marked `entry`: reached means a task's
  first state, or named in a `return S_X`, `STEP_SLEEP/WAIT/ARG(S_X, ...)`,
  `task_claim(S_X)` or `task_claim_arg(S_X, ...)` in the scanned sources
  (the `.i` beside the `.def` and its `source` lines). schedtest's
  `ST_NEG=bad_spec` is such a state.
- More fixed slots than `--slots`, or none left over when there are jobs;
  two slots of one name, or a slot named `fixed`, `slots` or `job<N>` (its
  enum would clash with `T_FIXED`, `T_SLOTS`, `T_JOB<N>`).
- A warning, not a failure: a `return S_X` or `STEP_*(S_X ...)` naming
  another module's state. One module starts another's work with
  `task_wake` (chapter 6) or a request (the flash job), not by jumping into
  its states.

## Sequence derivation

1. Put every **high** task in a group G; between two G, `--fill` (default
   2) other positions. With 4 high tasks that is every 6th position: what
   bounds a high task's latency is the steps between its runs
   (G - 1 + fill), not the positions.
2. Spread **medium** tasks over the other positions, 3 appearances each.
3. Fill the rest with **once** tasks and the dynamic slots, in slot order.
4. Report each task's appearances and worst gap in positions (the build
   prints it; `sequence.txt`). A task's worst latency is that gap in steps
   times the longest step; `health tasks` gives the longest steps, the
   multiplication is the reader's.

The firmware's (fb-build.txt): 72 positions; rx, link, loop, con 12 times,
worst gap 6; stp, lacp 3 times, worst gap 24; the rest once. Open question
O2 is whether to change `--fill`.

Host unit tests (`test/test_sched_gen.py`): parse, the checks with broken
inputs, the sweep's checks, the derived sequence for a known input, and the
golden output.
