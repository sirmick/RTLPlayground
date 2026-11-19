# 4. The scheduler core

Files: `sched.c` / `sched.h` (common area, resident), `sched_bank.c` and
`sched_health.c` (banked), `sched_gen.c` / `sched_gen.h` (generated,
chapter 5).

**Resident** is what runs at every position of every pass: the ready check
and the dispatch (`sched_pass`), and the small calls every step makes
(`task_next`, `task_until`, `task_mask`, `task_end`). **Banked** is what
runs once per pass or per step (the event snapshot, idle, the measurement
around each step) or rarely (`sched_init`, `task_claim`, `task_sleep`,
`health`). That is safe because no flash operation outlives its step
(chapter 10), and it is needed: the whole core resident overflowed the
common area (B2 measured 2.1 KiB for `sched.c` alone; the resident half is
606 B).

## Data (all xdata)

```c
#define SCHED_SLOTS 24

struct task {
	uint8_t  state;     /* 0 = free; else a generated state ID */
	uint16_t mask;      /* events that wake it (chapter 6) */
	uint16_t wake;      /* tick (5 ms) at which it is due */
	uint16_t arg;       /* the step's own: a cursor, a count, a port */
};                      /* 7 B */

__xdata struct task sched_task[SCHED_SLOTS];  /* 168 B */
__xdata uint8_t  sched_cur;                   /* slot being run */
__xdata uint16_t sched_now;                   /* tick snapshot */
__xdata uint16_t sched_ev;                    /* this pass's events */
__xdata uint16_t sched_posted;                /* posted by tasks, for the next pass */
```

Per state, for `health`: longest step (fine-clock units, 16 bits,
saturating) and a run count. Global: passes, overruns (> 20 ms), the last
overrun's state, idle count.

Generated (chapter 5): `step_addr[]` (3 B per state), `run_seq[]` (slot
index per position), `state_name[]`, the fixed-task init list.

## The loop

```c
void sched_run(void)                      /* never returns */
{
	for (;;) {
		swdt_kick();                          /* chapter 11 */
		sched_now = ticks_lo();
		sched_ev = events_snapshot();         /* chapter 6: ISR bits, sampled bits, sched_posted */
		ran = 0;
		for (pos = 0; pos < RUN_SEQ_LEN; pos++) {
			slot = run_seq[pos];
			t = &sched_task[slot];
			if (!t->state || !ready(t))
				continue;
			sched_cur = slot;
			st = t->state;
			t->mask = 0;                  /* default after a step: yield */
			t->wake = sched_now;
			crash_note(slot, st);         /* chapter 11 */
			fine_start();
			dispatch(st);                 /* 3-byte table call */
			fine_stop(st);                /* per-state max, overrun count */
			sched_now = ticks_lo();       /* steps take time */
			ran = 1;
		}
		if (!ran)
			sched_idle();
	}
}
```

- **Ready check:** `(int16_t)(sched_now - t->wake) >= 0 || (t->mask & sched_ev)`.
- **A task appearing several times** in `run_seq` can run several times per
  pass, once each time it is ready at its position. A yielding task does
  run again at its next position; that is how a busy job gets more turns
  than once per pass. A job that should run once per pass sets
  `wake = now + 1`.
- **`dispatch(st)`** loads the 24-bit address of `step_addr[st]` into
  `r0:r1:r2` and calls `__sdcc_banked_call`, exactly as SDCC does for a
  direct banked call. About 20 bytes of asm.
- **Cost of an idle position:** a load, a compare, a branch: ~1 µs.

## Idle without a race

Today's `idle()` checks its flags and then sets `PCON.0`. An interrupt
between the two leaves the CPU asleep for up to a tick (5 ms). The new idle
closes the race with the 8051 rule that an `IE` write delays interrupt
acceptance by one instruction:

```c
EA = 0;
if (!events_pending())
	{ EA = 1; PCON |= 1; }   /* the interrupt that wakes us is taken after the PCON write */
else
	EA = 1;
```

The tick (200 Hz) wakes it at least every 5 ms; that is the time resolution.

## API for steps

A step ends with a **return macro**, which sets what comes next and
returns (decision 18):

| Macro | Next |
|---|---|
| `STEP_NEXT(S_FOO)` | `S_FOO`, at its next position (a yield) |
| `STEP_SLEEP(S_FOO, ms)` | `S_FOO`, after `ms` (`task_sleep` below) |
| `STEP_WAIT(S_FOO, ev, ms)` | `S_FOO`, on any of `ev`, or after `ms` (`SCHED_FOREVER`: no timeout) |
| `STEP_ARG(S_FOO, a)` | `S_FOO` with the slot's `arg = a`, at its next position |
| `STEP_END()` | nothing: the slot is free |
| bare `return;` | the same state again, at its next position |

Each slot has a 16-bit **`arg`**, kept across steps: `task_arg()` reads it,
`task_set_arg(a)` sets it (macros on `sched_cur_task`). It is 0 when a fixed
task starts and when a job is claimed; `task_claim_arg(S_FIRST, a)` claims
with `arg = a`. A `count=N` fixed task tells its instances apart by
`sched_cur - T_<NAME>0`: an index in `arg` would cost a table in the common
area.

```c
void l2_more(void) __banked              /* chapter 7: arg is the next entry */
{
	if (!con_tx_room_line())
		STEP_WAIT(S_L2_MORE, EV_CON_TX, SCHED_FOREVER);
	if (!l2_print_entry(task_arg()))     /* one line */
		STEP_NEXT(S_CLI_PROMPT);
	task_set_arg(task_arg() + 1);       /* and stays in L2_MORE */
}
```

Under the macros are the low-level calls, for the rare step that needs them.
All act on the current slot (`sched_cur`) unless they name a slot.

| Call | Effect |
|---|---|
| `task_next(S_FOO)` | the next step is `S_FOO` |
| `task_yield()` | run again at its next position (the default) |
| `task_sleep(ms)` | `wake = tick + ceil(ms / 5) + 1`, the tick read at the call: never early, even when the call comes just before a tick; at most 5 ms late. Max 65,534 ms (16-bit argument); longer: `task_until` |
| `task_until(tick)` | `wake = tick` |
| `task_wait(ev_mask, timeout_ms)` | wake on any of `ev_mask`, or at the timeout |
| `task_wait(ev_mask, SCHED_FOREVER)` | as above, no timeout (`wake = now + 0x7fff` and `EV_FOREVER` in the mask; when that horizon comes the pass pushes it on instead of running the step) |
| `task_end()` | free the slot (`state = 0`) |
| `task_claim(S_FIRST)` | claim a free slot for a job; returns the slot or `SCHED_NONE` |
| `task_claim_arg(S_FIRST, a)` | the same, the job's `arg = a` (a macro: `sched_claim_arg = a`, then the claim) |
| `event_post(EV_x)` | set bits for the next pass (chapter 6) |

The macros expand to these calls; they are common-area or banked functions
that touch only xdata. Arguments are 8 or 16 bits.

**Waits over 65 s** (ARP ageing, DHCP lease): the module counts down in its
own context and sleeps ≤ 65 s at a time (or `task_until` up to 163 s).
`SCHED_FOREVER` waits re-arm inside the scheduler.

No API function takes two parameters: SDCC would put the second in
internal RAM. `task_wait(m, ms)` is a macro for `task_mask(m); task_sleep(ms)`;
the STEP_ macros and `task_claim_arg` are built the same way.

## Claiming slots

- Fixed tasks: `sched_init()` claims a slot for each, in the order of their `TASK` lines (generator.md),
  at the slot index the generator assigned, so `run_seq` can name it.
- Jobs: `task_claim()` takes the first free slot among those marked
  dynamic. A full table returns `SCHED_NONE`; the caller reports busy and
  does not block. 24 slots: 13 fixed, 11 dynamic (modules.md).

## Measurement

- Around each step, banked hooks read ticks + Timer 2 (~6 µs each). The
  unit is 64 Timer 2 counts, 6.144 µs; 16 bits saturate at ~400 ms.
- Per state: longest step, run count.
- Global: passes, overruns > 20 ms (with the state), longest pass, idle passes.
- Per task (debug builds): the longest wait from "became ready" to "ran".
- `health tasks` prints every slot: name, state name, mask, wake, and the
  per-state maxima. `health clear` resets them.
