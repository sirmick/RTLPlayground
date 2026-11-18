# 5. The generator

`tools/sched_gen.py`, run by the Makefile before compiling, like the
`html_data` step. Input: one `tasks.def` per module plus `sched/sequence.def`.
Output: `sched_gen.h` and `sched_gen.c` in the build directory.

## Input format

One `.def` file per module, next to its source (`rtl837x_stp.def`, ...):

```
# task <name> fixed|job [freq=high|medium|once] [count=N] [first=<STATE>]
# state <STATE> <step_function> [entry]
# source <file>     (more of the module's sources, beside the .def)
task stp fixed freq=medium first=STP_STEP
state STP_STEP      stp_step
state STP_SECOND    stp_second

task l2dump job first=L2_FIRST
state L2_FIRST      l2_first
state L2_MORE       l2_more
```

- `count=N` makes N fixed instances (one per SFP slot): N slots, all
  starting in `first`; the step tells them apart by `sched_cur`.
- Frequencies: **high** = in every group (below: a few steps apart),
  **medium** = 3 times per pass, **once** = once per pass. Jobs take `once` unless they say
  otherwise.
- `first=` defaults to the task's first state. Task names are lower case,
  states upper case.
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
typedef void (*sched_step_fn)(void) __banked;

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
  isn't `__banked` (the generator checks the prototype in the header).
- Two states with the same name, or one function used by two states.
- A step function that is `static` or not `void f(void)` (rule 4, chapter 12).
- A state no task can reach, unless marked `entry`: reached means a task's
  first state, or named in a `task_next(S_X)`, `STEP_NEXT/SLEEP/WAIT/ARG(S_X, ...)`,
  `task_claim(S_X)` or `task_claim_arg(S_X, ...)` in the
  scanned sources (the `.h`/`.c` beside the `.def` and its `source` lines).
- More fixed slots than `SCHED_SLOTS`, or none left over when there are jobs;
  two slots of one name, or a slot named `fixed`, `slots` or `job<N>` (its
  enum would clash with `T_FIXED`, `T_SLOTS`, `T_JOB<N>`).
- A `task_next(S_X)` or `STEP_*(S_X ...)` in a module's source naming a state of another module
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
