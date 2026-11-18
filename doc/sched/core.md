# 4. The scheduler core

Files: `sched.c` / `sched.h` (common area, resident), `sched_gen.c` /
`sched_gen.h` (generated, chapter 5).

## Data (all xdata)

```c
#define SCHED_SLOTS 24

struct task {
	uint8_t  state;     /* 0 = free; else a generated state ID */
	uint16_t mask;      /* events that wake it (chapter 6) */
	uint16_t wake;      /* tick (5 ms) at which it is due */
};

__xdata struct task sched_task[SCHED_SLOTS];  /* 120 B */
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

All act on the current slot (`sched_cur`) unless they name a slot.

| Call | Effect |
|---|---|
| `task_next(S_FOO)` | the next step is `S_FOO` |
| `task_yield()` | run again at its next position (the default) |
| `task_sleep(ms)` | `wake = now + ceil(ms / 5)`; max 163,000 ms |
| `task_until(tick)` | `wake = tick` |
| `task_wait(ev_mask, timeout_ms)` | wake on any of `ev_mask`, or at the timeout |
| `task_wait(ev_mask, SCHED_FOREVER)` | as above, no timeout (`wake = now + 0x7fff`, re-armed) |
| `task_end()` | free the slot (`state = 0`) |
| `task_claim(S_FIRST)` | claim a free slot for a job; returns the slot or `SCHED_NONE` |
| `event_post(EV_x)` | set bits for the next pass (chapter 6) |

Steps call `task_next()` and one wake call. Helpers are macros or common-area
functions that touch only xdata. Arguments are 8 or 16 bits.

**Waits over 163 s** (ARP ageing, DHCP lease): the module counts down in its
own context and sleeps ≤ 160 s at a time. `SCHED_FOREVER` waits re-arm the
same way, inside the scheduler.

## Claiming slots

- Fixed tasks: `sched_init()` claims a slot for each, in `tasks.def` order,
  at the slot index the generator assigned, so `run_seq` can name it.
- Jobs: `task_claim()` takes the first free slot among those marked
  dynamic. A full table returns `SCHED_NONE`; the caller reports busy and
  does not block. 24 slots: ~14 fixed, 10 dynamic.

## Measurement

- `fine_start()` / `fine_stop()` read ticks + Timer 2 (~6 µs each).
- Per state: longest step, run count.
- Global: passes, overruns > 20 ms (with the state), longest pass, idle passes.
- Per task (debug builds): the longest wait from "became ready" to "ran".
- `health tasks` prints every slot: name, state name, mask, wake, and the
  per-state maxima. `health clear` resets them.
