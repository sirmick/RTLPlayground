# 4. The scheduler core

Files: `sched.h` (the API), `sched.c` (common area, resident),
`sched_bank.c` and `sched_health.c` (banked), and the generated
`sched_gen.h`, `sched_gen.c`, `sched_gen_names.c` (chapter 5).

**Resident** is what runs at every position of every pass, and the small
calls steps make: the ready check and the dispatch (`sched_pass`, which
stores the state a step returns), `sched_ticks`, `task_until`,
`task_mask`, `task_wake`. **Banked** is what runs once per pass or per step
(the event snapshot, idle, the measurement around each step) or rarely
(`sched_init`, `task_claim`, `task_sleep`, `health`). That is safe because no
flash operation outlives its step (chapter 10), and it is needed: all of
the core resident overflowed the common area (B2: 2131 B for `sched.c`
alone). At 72cee59 `sched.c` is 618 B of code, `sched_bank.c` 1651 B,
`sched_health.c` 1677 B (evidence/fb-build.txt).

## Data (all xdata)

```c
struct task {
	uint8_t  state;		/* 0 = free; else a generated state ID */
	uint8_t  mask;		/* hardware events that wake it (chapter 6) */
	uint16_t wake;		/* tick (5 ms) at which it is due */
	uint16_t arg;		/* the step's own: a cursor, a count, a port */
};				/* 6 B */

__xdata struct task sched_task[SCHED_SLOTS];	/* 24 slots: 144 B */
__xdata uint8_t  sched_cur;			/* slot being run */
__xdata struct task * __xdata sched_cur_task;	/* &sched_task[sched_cur] */
__xdata uint16_t sched_now;			/* tick snapshot, renewed after each step */
__xdata uint8_t  sched_ev;			/* this pass's hardware events */
```

For `health`: per state the longest step, the runs since the clear
(saturating), this second's runs and the last second's (16 bits each); globals: passes and idle passes (32 bits, as two
halves), overruns and the last overrun's state, the longest pass, the last
second's load (8 bits). Decision 26.

Generated (chapter 5): `step_addr[]` (3 B per state), `run_seq[]` (a slot
index per position), `fixed_first[]` (each fixed slot's first state),
`state_name[]`, `slot_name[]`.

## The loop

`sched_run()` (banked) never returns. It turns the watchdog on, then runs
passes:

```c
void sched_run(void) __banked
{
	swdt_kick();
	swdt_on = 1;			/* chapter 11: off until here */
	for (;;) {
		swdt_kick();
		if (!sched_pass())
			sched_idle();
	}
}

uint8_t sched_pass(void)		/* resident */
{
	sched_m_pass_start();
	sched_now = sched_ticks();
	sched_ev = sched_events();	/* chapter 6 */
	ran = 0;
	for (pos = 0; pos < RUN_SEQ_LEN; pos++) {
		sched_cur = run_seq[pos];
		sched_cur_task = &sched_task[sched_cur];
		st = sched_cur_task->state;
		if (!st || !ready())
			continue;
		sched_cur_task->mask = 0;	/* a step that says nothing yields */
		sched_cur_task->wake = sched_now;
		CRASH_NOTE(sched_cur, st);	/* chapter 11 */
		sched_st = st;
		sched_m_step_start();
		st = step_addr[st]();		/* the step: its next state */
		if (st != STEP_STAY)
			sched_cur_task->state = st;	/* STEP_END: 0, the slot is free */
		sched_m_step_end();
		sched_now = sched_ticks();	/* steps take time */
		ran = 1;
	}
	sched_cur = SCHED_NONE;
	sched_m_pass_end();
	return ran;
}
```

- **Ready** (`ready()`, a leaf so its temporaries stay in registers): an
  event in the mask, or the wake tick has come. A wake that has come on a
  mask with `EV_FOREVER` is only the horizon of a wait without timeout: the
  pass moves it 0x7fff ticks on and doesn't run the step.
- **Several positions:** a task in `run_seq` several times can run several
  times per pass, once at each position where it is ready. A step that
  yields (returns with no wake time) runs again at its next position: that
  is how `rx` drains the NIC ring and `cli` prints a dump a line a step.
- **The dispatch** `step_addr[st]()` is SDCC's own banked call through a
  3-byte table: it loads r0-r2 and calls `__sdcc_banked_call`, keeping r5-r7
  pushed for the step's length (3 B of stack; O8). No internal RAM
  (evidence/b2-resources.txt).
- **`sched_ticks()`** reads the low 16 bits of the ISR's 32-bit `ticks` and
  reads again if the low byte changed meanwhile.

## Idle without a race

`sched_idle()` sleeps the CPU (`PCON.0`) until the next interrupt, unless
there is work already. The old `idle()` checked its flags and then set
`PCON.0`; an interrupt between the two left the CPU asleep for up to a
tick. The new one closes the race with the 8051 rule that an `IE` write
delays interrupt acceptance by one instruction:

```c
EA = 0;
if (link_irq || rx_irq || con_rx_ready()
    || (uint8_t)ticks != (uint8_t)sched_now) {	/* a tick since the pass began */
	EA = 1;
	return;
}
EA = 1;
PCON |= 1;	/* the interrupt that wakes us is taken after this write */
```

The tick (200 Hz) wakes it at least every 5 ms; that is the time resolution.
The tick test is there because a task may have come due after its position
in the pass.

## API for steps

A step is `STEP(task, name)`: `uint8_t task_name(void) __banked`, state
`S_TASK_NAME` (chapter 5). It **returns the state it wants next**
(decision 22). The dispatcher stores it; no helper, and no other call, sets
a slot's state.

| Return | Next |
|---|---|
| `return S_FOO;` | `S_FOO`, at its next position (a yield) |
| `return STEP_STAY;` | the same state again, at its next position (`0xff`: no state is) |
| `return STEP_END;` | nothing: the slot is free (`0`) |
| `return STEP_SLEEP(S_FOO, ms);` | `S_FOO`, after `ms` (`task_sleep` below) |
| `return STEP_WAIT(S_FOO, hw, ms);` | `S_FOO`, on any of the hardware events `hw`, or after `ms` (`SCHED_FOREVER`: no timeout) |
| `return STEP_ARG(S_FOO, a);` | `S_FOO` with the slot's `arg = a`, at its next position |

`STEP_SLEEP`, `STEP_WAIT` and `STEP_ARG` are expressions, e.g.
`(task_sleep(ms), (s))`; `s` may be `STEP_STAY`. A step that can fall off
its end fails the build: SDCC's warning 59 ("must return value") is an
error for the step sources (`--Werror`, `sched.mk`); schedtest's
`ST_NEG=fall_through` proves it (evidence/e5-step-api.txt).

The firmware's `rx` step, whole:

```c
STEP(rx, step)
{
	tn = rx_service();		/* up to RX_BUDGET frames */
	if (webcmd_pending)
		task_wake(T_CLI);	/* httpd queued a /cmd */
	if (tn)
		return STEP_STAY;	/* more waiting: again at the next position */
	return STEP_WAIT(S_RX_STEP, HW_RX, SCHED_FOREVER);
}
```

Each slot has a 16-bit **`arg`**, kept across steps: `task_arg()` reads
it, `task_set_arg(a)` sets it (macros on `sched_cur_task`). It is 0 when a
fixed task starts and when a job is claimed; `task_claim_arg(S_FIRST, a)`
claims with `arg = a`. The SFP slots keep their poll tick in it. A
`count=N` task tells its instances apart by `sched_cur - T_<NAME>0`.

Under the macros are the low-level calls. All act on the current slot
unless they name one; none chooses a state.

| Call | Effect |
|---|---|
| `task_sleep(ms)` | `wake = tick + ceil(ms / 5) + 1`, the tick read at the call: never early, even when the call comes just before a tick; at most 5 ms late. 0 is a yield. Up to 65,534 ms (16-bit argument) |
| `task_sleep(SCHED_FOREVER)` | no timeout: `EV_FOREVER` in the mask and a horizon `tick + 0x7fff`, pushed on when it comes |
| `task_until(tick)` | `wake = tick` (at most 0x7fff ahead) |
| `task_mask(hw)` | also woken by any of the hardware events `hw` (ORed in) |
| `task_wait(hw, ms)` | a macro: `task_mask(hw); task_sleep(ms)` |
| `task_wake(slot)` | another slot (a fixed task's `T_X`, a job's claimed slot) is due now, its `EV_FOREVER` cleared: it runs at its next position (chapter 6) |
| `task_claim(S_FIRST)` | claims a free dynamic slot for a job, starting in `S_FIRST`; returns the slot or `SCHED_NONE` |
| `task_claim_arg(S_FIRST, a)` | the same, the job's `arg = a` (a macro: `sched_claim_arg = a`, then the claim) |

**Periodic tasks use `task_until(last + period)`**, not `task_sleep`: the
sleep's extra tick, right for a one-off wait, makes a 5 ms period 10 ms
(silicon.md, C). `loop` and `house` keep their next second in a static
(`loop_due += 200`); stp, lacp, lldp and net run once a tick with
`task_until(sched_now + 1)`.

**Longer waits** than ~163 s: the module counts in its own data. None of the
firmware's tasks needs one; house counts ARP's 10 s in seconds.

No API function takes two parameters: SDCC would put the second in
internal RAM. The `STEP_*` macros, `task_wait` and `task_claim_arg` are
built from one-parameter calls.

## Claiming slots

- Fixed tasks: `sched_init()` puts each in its slot, in the order of the
  `TASK` lines (chapter 5), in its first state, due now, `arg` 0. Slots that
  have no fixed task start free.
- Jobs: `task_claim()` takes the first free slot from `T_FIXED` up (mask 0,
  due now). A full table returns `SCHED_NONE`; the caller refuses the work
  and does not block (the flash job: chapter 10).
- The firmware: 12 fixed slots and 12 dynamic (evidence/fb-build.txt);
  schedtest: 13 and 11.

## Measurement

- The fine clock: ticks plus Timer 2's count, read with interrupts off and
  corrected for an overflow whose ISR is still pending. The unit is 64
  counts, 6.144 µs; a tick is 813 units (`sched_fine_per_tick`, from
  `RCAP2` at `sched_init`).
- Around each step (`sched_m_step_start`, `sched_m_step_end`): the step's
  length, saturating at 80 ticks (400 ms, shown as `>400ms`); per state the
  longest, and a run counted twice: in `sched_st_runs[]`, since the clear
  (16 bits, saturating; `state.sched.state.<S>.runs`), and in
  `sched_st_sec[]`, this second's (16 bits: a state runs at most a few
  hundred times a second; nothing saturates). A
  step of 50 ms or more (8138 units,
  decision 25; 20 ms, 3255, until F1b) is an overrun: counted, with its
  state. The step start also clears `hw_stuck`
  (chapter 9).
- Around each pass: the longest pass, the pass count.
- **The second** (decision 26): at the first pass end at or past the
  second's tick, each state's `sched_st_sec[]` is published into `sched_st_rate[]`
  and zeroed, and the time asleep becomes `sched_load`. The seconds keep to
  a fixed 200-tick grid, half a second off the grid of the 1 s tasks
  (`house`, `loop` start theirs at their first step, just after
  `sched_init`), so a 1/s state reads 1, not 0 or 2 as its run and the roll
  jitter. A roll is late by at most the pass that crossed the tick; that
  time goes to the next second, not out of the average: a periodic state
  reads its period's rate +-1 at worst, and in the sim exactly
  (`tools/sched_rates.py`: `HOUSE_SEC`, `LOOP_POLL` 1/s, `NET_TICK`,
  `STP_TICK`, `LACP_TICK`, `LLDP_TICK` 200/s). A pass of more than a second
  starts the grid again from it. The first second after boot is half a
  second (its rates and load read half). The counts start at 0 with XSEG's
  clear. The roll writes the next second's tick (`sched_roll_tick`) last:
  a tool that stops the firmware anywhere and sees it move sees the whole
  second, so it can total the runs (`tools/flash_job_steps.py` counts
  `FL_ERASE` since power-on that way).
- **Time asleep:** `sched_idle()` reads the fine clock before its check and
  after the wake, and adds what it slept, in 1% steps of a second (2 ticks,
  1626 units) and a remainder under one (16 bits: memory.md
  rule 3; the remainder carries into the next second). The fine clock is
  Timer 2, which runs in IDL (its ISR is what wakes the CPU), and a sleep
  ends at the next interrupt, the tick's at the latest: so a sleep is at
  most a tick plus the waking ISR, and `fine_elapsed()` counts a tick that
  came inside it. The ISR's time and the few instructions around `PCON`
  count as asleep. `load = 100 - (steps asleep)`, floored to 0: the part of
  the last second not asleep, -0/+1 point from the 1% steps. Checked
  against rtlsim's own busy cycles over 10 idle seconds: 21.7 % against
  21.8 % on the 8x, 21.9 against 22.0 on SWGT024. The firmware's idle is
  not 0: two passes a tick over the 72 positions and the four tick tasks
  cost ~1.1 ms of each 5 ms tick in the sim. While `l2` dumps 1000 hosts
  the load reads 93-94 %.
- `health tasks` prints every slot (name, state name, mask, wake), every
  state's runs in the last second (`runs/s`) and longest step, and the
  totals: passes, idle, the last second's load (`load NN%`), longest pass,
  overruns and the last one's state, console RX overruns, hardware-poll
  timeouts and the last register, and the last reset's line. It runs as a
  cli job, a line a step (chapter 7); the web's `/cmd` runs the same.
  `health clear` resets the longest steps, the totals and the overruns; the
  rates and the load stay, as they are always the last whole second.
