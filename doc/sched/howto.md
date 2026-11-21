# 4a. How to add a task

Five small examples, for someone adding a task to the firmware. They use
the API as built (`sched.h`, chapter 4) and follow real code; each names
the function it is modelled on. The tasks in them (`beat`, `txwait`,
`rq`/`srv`, `dump`, `save`) are examples, not in the firmware.
All five, put in one step source with stubs for their helpers, pass the
sweep and the generator and compile with SDCC `--Werror` against 72cee59,
with no internal RAM (2026-11-20, a /tmp build tree; FB).

Before you start:

- Steps go in a step source: `tasks.c` or `flash_job.c`. A new file must be
  added to `SCHED_STEP_SRCS` and `SCHED_SRCS` in `sched.mk`, or the sweep
  fails the build ("a STEP outside the step sources").
- A step is banked. Its locals, if it calls anything, are
  `static __xdata` (chapter 12).
- Each step starts with an empty mask and a wake of "now". A step that
  returns a state with no wait yields: it runs again at its next position.
  Every wait is said again on every exit that waits.
- After a build, `<builddir>/sched/sequence.txt` shows your task's slot and
  spacing, and `health tasks` its states' runs in the last second and
  longest steps, and the load (decision 26).

## 1. A periodic task: once a second

Modelled on `loop` and `house` (`tasks.c`).

```c
#define SEC_TICKS	200		/* 1 s in 5 ms ticks */

static __xdata uint16_t beat_due;	/* the next second, in ticks */

TASK(beat, fixed, once, first=start)
STEP(beat, start)
{
	beat_due = sched_now + SEC_TICKS;
	task_until(beat_due);
	return S_BEAT_SEC;		/* exit: the first second, at beat_due */
}

STEP(beat, sec)
{
	beat_work();			/* a bounded piece of work, under 5 ms */
	beat_due += SEC_TICKS;		/* from the last due tick, not from now */
	task_until(beat_due);
	return STEP_STAY;		/* exit: BEAT_SEC again at the next second */
}
```

Why not `return STEP_SLEEP(STEP_STAY, 1000);`: `task_sleep` adds a tick so
a wait is never early, and counts from when the step runs, which may be
late. Each period would grow by 5 ms or more, and the drift adds up; a 5 ms
sleeper ran every 10 ms on the 8x (silicon.md, C). `task_until(last +
period)` keeps the cadence, and catches up after a long step.

## 2. Wait for a hardware event, with a timeout

Modelled on cli's `CLI_MORE` and schedtest's printer: wait for room in the
console TX ring (`HW_CON_TX`), up to 100 ms.

```c
__xdata uint8_t txw_want;		/* the level: a line waits; set it, then task_wake(T_TXWAIT) */
__code const char * __xdata txw_text;	/* the line (a string in code) */
static __xdata uint8_t txw_lost;	/* lines given up */

TASK(txwait, fixed, once, first=idle)
STEP(txwait, idle)
{
	if (!txw_want)			/* txw_want: set by whoever has a line to print */
		return STEP_SLEEP(S_TXWAIT_IDLE, SCHED_FOREVER);	/* exit: nothing to do, until woken */
	return STEP_WAIT(S_TXWAIT_LINE, HW_CON_TX, 100);	/* exit: room for a line, or 100 ms */
}

STEP(txwait, line)
{
	if (con_tx_room() < CON_TX_LINE) {	/* the level, not the event: is there room now? */
		txw_lost++;			/* no: this is the timeout */
		txw_want = 0;
		return S_TXWAIT_IDLE;		/* exit: give up the line, back to idle */
	}
	print_string(txw_text);		/* one line, under CON_TX_LINE bytes */
	txw_want = 0;
	return S_TXWAIT_IDLE;		/* exit: done */
}
```

The step can't tell whether the event or the timeout woke it: it checks
the level. An event lasts one pass, so a task that wasn't at a ready
position may never see it; the level check also covers a wake for another
reason. Events: `HW_RX`, `HW_LINK`, `HW_CON_RX`, `HW_CON_TX` (chapter 6).

## 3. One task wakes another

Modelled on con waking cli (`CON_EDIT`, `CLI_IDLE`), and the flash job
waking net (`FL_DONE`, `NET_TICK`).

```c
__xdata uint8_t srv_pending;		/* the level: work for srv */

TASK(rq, fixed, once)
STEP(rq, tick)
{
	if (rq_has_work()) {
		srv_pending = 1;		/* say why first */
		task_wake(T_SRV);		/* then wake: srv runs at its next position */
	}
	task_until(sched_now + 1);
	return STEP_STAY;		/* exit: again next tick */
}

TASK(srv, fixed, once, first=idle)
STEP(srv, idle)
{
	if (!srv_pending)		/* woken for nothing (or the wake came twice): sleep again */
		return STEP_SLEEP(S_SRV_IDLE, SCHED_FOREVER);	/* exit: wait for the next wake */
	srv_pending = 0;
	srv_work();
	return STEP_STAY;		/* exit: look again at the next position */
}
```

A wake carries nothing and isn't queued: two wakes before srv runs are one
wake. So the waker sets a level first, and the woken step re-checks it. A
woken task can be in any state; if it is periodic, it also checks it is
due before it advances its own timer (as `NET_TICK` does with `net_due`).

## 4. A job: one row a step

A job claims a free slot, walks a table with its cursor in `arg`, and frees
the slot at the end. Modelled on `task_claim()`'s use in
`flash_job_start()` and the cli jobs' line-a-step printing (`CLI_MORE`,
`cli_job_more()`).

```c
#define DUMP_ROWS	64

/* from a command: 1 if the dump started */
uint8_t dump_start(void)
{
	if (task_claim_arg(S_DUMP_ROW, 0) == SCHED_NONE)	/* arg = the first row */
		return 0;		/* the table is full: busy */
	return 1;		/* an if, not `return a != b`: that cost a bit of internal RAM */
}

TASK(dump, job, once)
STEP(dump, row)
{
	if (task_arg() >= DUMP_ROWS)
		return STEP_END;		/* exit: the last row is out, the slot is free */
	if (con_tx_room() < CON_TX_LINE)
		return STEP_WAIT(STEP_STAY, HW_CON_TX, SCHED_FOREVER);	/* exit: wait for room for a line */
	dump_print_row(task_arg());	/* one line, under CON_TX_LINE bytes */
	return STEP_ARG(STEP_STAY, task_arg() + 1);	/* exit: the next row at the next position */
}
```

- If the table is full, `task_claim_arg` gives `SCHED_NONE`: refuse the
  work ("busy"), don't wait for a slot.
- The cursor can also be the module's own xdata; `arg` is handy when
  several instances may run.
- In the firmware a console command's long output is not a job like this
  but a cli job, so that it ends with the prompt and goes into a web reply
  (chapter 7): set `cli_job` and add a case to `cli_job_more()`.

## 5. Flash, and a bounded hardware poll

**Never erase or program from your step.** Ask the flash job, which waits
for the flash in the common area (chapter 10). Modelled on
`loader_confirm()` (`boot.c`) and house's retry.

```c
__xdata uint8_t save_asked;		/* the level: set it, then task_wake(T_SAVE) */
extern __xdata uint8_t save_buf[];	/* the module's own: what to save */
extern __xdata uint16_t save_len;

TASK(save, fixed, once, first=idle)
STEP(save, idle)
{
	if (!save_asked)
		return STEP_SLEEP(S_SAVE_IDLE, SCHED_FOREVER);	/* exit: nothing asked, until woken */
	save_asked = 0;
	return S_SAVE_ASK;		/* exit: make the request at the next position */
}

STEP(save, ask)
{
	fl_req = FL_CONFIG;		/* a config save: fl_len bytes at fl_src */
	fl_src = save_buf;
	fl_len = save_len;
	if (!flash_job_start())
		return STEP_SLEEP(STEP_STAY, 1000);	/* exit: busy, or an upload is coming: ask again in 1 s */
	return STEP_SLEEP(S_SAVE_WAIT, 50);	/* exit: the job has started; look for its end */
}

STEP(save, wait)
{
	if (!fl_finished)
		return STEP_SLEEP(STEP_STAY, 50);	/* exit: still writing */
	return S_SAVE_IDLE;		/* exit: written, back to idle */
}
```

`fl_finished` says the last job ended; `fl_src` must stay valid until
then. `flash_job_start()` refuses rather than waits: the job is running, an
upload's body is coming (`up_active`), or no slot is free. The real
requesters are simpler still: the web's POST `/config` is answered when the
job wakes net, `loader_confirm()` is asked again by house next second, and
the console's `factory-reset` lets the job reset the chip.

**A hardware poll gets a bound and an error path.** Modelled on
`l2_flush_wait()` (`rtl837x_port.c`) and `sfp_read_block()`
(`rtl837x_pins.c`).

```c
/* the common case: bit 0 of a register's low byte, up to HW_POLL_TICKS */
if (hw_poll(RTL837X_REG_I2C_CTRL))
	return false;			/* timed out: counted in health tasks; fail the read */

/* any other condition: the same bound by hand */
hw_start(HW_POLL_TICKS);
do {
	reg_read(MY_STATUS_REG);
	if (!(SFR_DATA_0 & MY_BUSY))
		return 0;		/* done */
} while (!hw_expired());
hw_timeout(MY_STATUS_REG);		/* count it, with the register */
return 1;				/* the caller goes on, or fails, but doesn't spin */
```

A step waits out one timeout at most: after one, `hw_stuck` makes every
later poll in the same step give up at once (chapter 9).

## Rules

- Every exit returns a state: `S_X`, `STEP_STAY`, `STEP_END`, or a
  `STEP_SLEEP/WAIT/ARG(...)`. One that can fall off its end fails the
  build (warning 59 is an error for the step sources).
- `STEP_STAY` means the same state again; `STEP_END` frees the slot.
- No helper chooses a state. A helper returns a status (`SFP_MORE`,
  `SFP_DONE`, `SFP_FAIL`) and the step picks the state; the sweep fails a
  `return` that calls a function.
- No `delay()` in a step: return with a wait.
- 8- and 16-bit arithmetic; data and locals in `__xdata`; no function with
  two parameters. The resource gate fails any internal RAM growth.
- Under 5 ms a step; 50 ms is the limit (decision 25). Check `health tasks`, and
  `tools/cli_steps.py` or `tools/sched_tour.py` in rtlsim.
- Print at most a line (100 B) a step after checking room, unless the
  output is known to be small (a command starts with 1 KiB free).
- Flash work goes through the flash job.

## Common mistakes

- **`task_sleep` for a period.** The period grows a tick each time; use
  `task_until(last + period)`.
- **No level re-check.** Acting on a wake or an event without looking at
  the condition again: a wake may be for nothing, an event may be gone, and
  a timeout looks like a wake.
- **A helper picking the state.** `return helper();` hides the state
  machine from the generator and the reader; the sweep rejects it.
- **Printing more than the ring holds.** A dump in one step waits on the
  UART at 87 µs a byte: 1000 lines are seconds in one step. Make it a cli
  job, a line a step.
- **A wait said once.** The mask is cleared before each step: a step that
  returns without `STEP_WAIT` no longer waits for the event.
- **A boolean expression returned or tested.** SDCC may keep `a != b` or
  `a && b` in a bit of internal RAM: `return task_claim(...) != SCHED_NONE`
  did, in a first draft of example 4. Write an `if` with two returns.
- **A local in IRAM.** A plain local in a step that calls a function, or a
  32-bit expression, can spill into internal RAM; the gate fails it.
