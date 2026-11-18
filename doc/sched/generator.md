# 5. The generator

`tools/sched_gen.py`, run by the Makefile before compiling, like the
`html_data` step. Input: one `tasks.def` per module plus `sched/sequence.def`.
Output: `sched_gen.h` and `sched_gen.c` in the build directory.

## Input format

One `.def` file per module, next to its source (`rtl837x_stp.def`, ...):

```
# task <name> fixed|job [freq=high|medium|once] [count=N] [first=<STATE>]
# state <STATE> <step_function>
task stp fixed freq=medium first=STP_STEP
state STP_STEP      stp_step
state STP_SECOND    stp_second

task l2dump job first=L2_FIRST
state L2_FIRST      l2_first
state L2_MORE       l2_more
```

- `count=N` makes N fixed instances (one per SFP slot): N slots, all
  starting in `first`; the step tells them apart by `sched_cur`.
- Frequencies: **high** = checked every ~3 positions, **medium** = ~3 times
  per pass, **once** = once per pass. Jobs take `once` unless they say
  otherwise.
- `sequence.def` may override the derived order with an explicit list of
  task names (and `job` placeholders).

## Output

```c
/* sched_gen.h */
enum { S_NONE, S_STP_STEP, S_STP_SECOND, S_L2_FIRST, S_L2_MORE, ..., S_COUNT };
enum { T_STP, T_LACP, T_SFP0, T_SFP1, ..., T_FIXED, T_JOB0 = T_FIXED, ... };
#define RUN_SEQ_LEN 48

/* sched_gen.c */
__code const uint8_t step_addr[S_COUNT][3] = {          /* 24-bit, bank in byte 2 */
	{ 0, 0, 0 },
	{ (uint8_t)(uint16_t)stp_step, (uint8_t)((uint16_t)stp_step >> 8), BANKOF(stp_step) },
	...
};
__code const uint8_t run_seq[RUN_SEQ_LEN] = { ... };
__code const uint8_t fixed_first[T_FIXED] = { S_STP_STEP, ... };
__code const char * __code const state_name[S_COUNT] = { "-", "stp_step", ... };
```

The bank byte comes from the linker, as in SDCC's own banked calls
(`mov r2,#(func >> 16)`). If C can't express it, the table is emitted as an
`.asm` file with `.db` lines, which the assembler resolves (`<func`, `>func`,
`func >> 16`). Settling this is the first thing B1 does.

Where the tables live: `run_seq` and `step_addr` in the common area (read on
every dispatch, and small: 48 + 3 per state). `state_name` in a bank (only
`health` reads it).

## Checks (the build fails on any)

- A state named in a `.def` whose function doesn't exist (link error), or
  isn't `__banked` (the generator checks the prototype in the header).
- Two states with the same name, or one function used by two states.
- A state no task can reach, unless marked `entry` (reached by `task_claim`).
- More tasks than `SCHED_SLOTS`.
- A `task_next(S_X)` in a module's source naming a state of another module
  (warning: cross-module jumps go through events).

## Sequence derivation

1. Put every **high** task in a group G; place G every 3rd position.
2. Spread **medium** tasks over the gaps, 3 appearances each.
3. Fill the rest with **once** tasks and job placeholders.
4. Report each task's spacing (worst gap in positions); `health` multiplies
   by the measured longest step to give each task's worst latency.

Host unit tests (B1): parse, the checks with broken inputs, the derived
sequence for a known input, and a golden output file.
