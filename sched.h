/*
 * sched.h - the cooperative scheduler (doc/sched/core.md, events.md).
 *
 * A task is a slot {state, mask, wake}. Each pass walks the generated run
 * sequence; a slot whose wake tick has come, or whose mask meets this pass's
 * hardware events, runs one step: the function behind its state, through the generated
 * 3-byte table. A step does a bounded piece of work, says when it wants to
 * run again (one wake call; none: at its next position) and returns the
 * state it wants next (decision 22): the scheduler stores it.
 *
 * Everything here is xdata, 8/16-bit, and no function takes more than one
 * parameter: SDCC puts a second parameter, and a non-leaf function's locals,
 * in internal RAM, of which the scheduler may add none (memory.md).
 */
#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>

/*
 * Tasks and steps are declared where they are written (generator.md):
 *
 *   TASK(name, fixed|job, high|medium|once [, count=N] [, first=step] [, entry=step])
 *   STEP(name, step) { ... }	is uint8_t name_step(void) __banked, state S_NAME_STEP
 *
 * A TASK has no semicolon. Tasks take their slots in the order of their
 * TASK lines, a task's states the order of its STEPs; the first STEP is the
 * entry unless first= names another. tools/sched_sweep.py preprocesses each
 * scheduler source with the build's flags and SCHED_SWEEP, where the markers
 * survive as strings, and writes the .def files sched_gen.py reads.
 */
#ifdef SCHED_SWEEP
#undef __banked		/* the host tests' shim erases it: the generator checks it */
#define TASK(...)	__sched_task__ #__VA_ARGS__
#define STEP(t, s)	__sched_step__ #t #s uint8_t t##_##s(void) __banked
#else
#include "sched_gen.h"
#define TASK(...)
#define STEP(t, s)	uint8_t t##_##s(void) __banked
#endif

#define SCHED_SLOTS	SCHED_GEN_SLOTS	/* the generator's --slots */
#define SCHED_NONE	0xff		/* task_claim(): no free slot */
#define SCHED_FOREVER	0xffff		/* task_sleep()/task_wait(): no timeout */
#define SCHED_TICK_MS	5
#define SCHED_MAX_WAIT	0x7fff		/* ticks; a wake further ahead reads as past */
#define SCHED_SEC_TICKS	200		/* the rates' second (decision 26) */

/* Hardware events (events.md, decision 23): 8 bits, sampled at each pass's
 * start. One task waking another is task_wake(), not an event. Bit 7 is the
 * scheduler's own: a SCHED_FOREVER wait. */
#define HW_RX		0x01	/* INT1: NIC RX */
#define HW_LINK		0x02	/* INT0: the switch's interrupt tree */
#define HW_CON_RX	0x04	/* sampled: console RX ring not empty */
#define HW_CON_TX	0x08	/* sampled: console TX ring has room for a line */
/*		0x10	   reserved, was EV_FLASH_IDLE: no flash operation outlives its step */
#define EV_FOREVER	0x80	/* in a mask: the wake is only a re-armed horizon */

struct task {
	uint8_t  state;		/* 0 = free; else a generated state ID */
	uint8_t  mask;		/* hardware events that wake it */
	uint16_t wake;		/* tick (5 ms) at which it is due */
	uint16_t arg;		/* the step's own: a cursor, a count, a port */
};

extern __xdata struct task sched_task[SCHED_SLOTS];
extern __xdata uint8_t  sched_cur;	/* slot being run */
extern __xdata uint16_t sched_now;	/* tick snapshot, renewed after each step */
extern __xdata uint8_t  sched_ev;	/* this pass's hardware events */

/* Measurement (health). Durations are in fine units of 64 Timer 2 counts,
 * 6.144 us; 16 bits saturate at ~400 ms. */
#define SCHED_FINE_NS		6144
#define SCHED_OVERRUN_FINE	8138	/* 50 ms, the step limit (decision 25) */
extern __xdata uint16_t sched_st_max[S_COUNT];	/* longest step per state */
extern __xdata uint16_t sched_st_runs[S_COUNT];	/* runs per state, this second */
extern __xdata uint16_t sched_st_rate[S_COUNT];	/* runs per state, the last second */
extern __xdata uint8_t  sched_load;		/* % of the last second not asleep */
extern __xdata uint16_t sched_roll_tick;	/* the next second's tick: moves at each roll (the sim's tools see it) */
extern __xdata uint16_t sched_passes_lo, sched_passes_hi;
extern __xdata uint16_t sched_idle_lo, sched_idle_hi;
extern __xdata uint16_t sched_overruns;
extern __xdata uint8_t  sched_overrun_state;	/* the last overrun's state */
extern __xdata uint16_t sched_pass_max;		/* longest pass, fine units */
extern __xdata uint16_t sched_fine_per_tick;	/* fine units per 5 ms tick */
extern __xdata uint16_t sched_f_tick, sched_f_cnt;	/* the last sched_fine_read(): tick, units into it */
extern __xdata uint8_t  sched_st;		/* the state being measured */
extern __xdata struct task * __xdata sched_cur_task;	/* &sched_task[sched_cur] */

/* What a step returns (decision 22): `return S_FOO;` is S_FOO at its next
 * position (a yield); STEP_STAY the same state; STEP_END frees the slot. The
 * others say when as well, and are expressions: `return STEP_WAIT(...);`.
 * A step that falls off its end fails the build (SDCC's warning 59, an
 * error for the step sources: sched.mk). */
#define STEP_END		0	/* the slot is free (state 0) */
#define STEP_STAY		0xff	/* the same state again (no state is 0xff) */
#ifndef SCHED_SWEEP		/* swept, they stay as written: the generator sees their state */
#define STEP_SLEEP(s, ms)	(task_sleep(ms), (s))
#define STEP_WAIT(s, hw, ms)	(task_mask(hw), task_sleep(ms), (s))
#define STEP_ARG(s, a)		(task_set_arg(a), (s))
#endif

/* The slot's own 16 bits, kept across steps: 0 when a fixed task starts
 * (a count=N task tells its instances apart by sched_cur - T_<NAME>0) and
 * when a job is claimed, unless task_claim_arg() gives it one. */
#define task_arg()		(sched_cur_task->arg)
#define task_set_arg(a)		(sched_cur_task->arg = (a))
extern __xdata uint16_t sched_claim_arg;
#define task_claim_arg(s, a)	(sched_claim_arg = (a), task_claim(s))

/* The low-level calls under the macros, for the rare step that needs them.
 * All act on the current slot; none chooses its state. */
void task_sleep(uint16_t ms) __banked;	/* due in at least ms (0: yield; SCHED_FOREVER: never) */
void task_until(uint16_t tick);		/* due at `tick` (at most SCHED_MAX_WAIT ahead) */
void task_mask(uint8_t hw_mask);	/* also woken by any of these hardware events */
#define task_wait(hw_mask, ms)	do { task_mask(hw_mask); task_sleep(ms); } while (0)
uint8_t task_claim(uint8_t first) __banked;	/* a free dynamic slot, starting in `first`; or SCHED_NONE */
/* Another task, now (decision 23): slot (T_CLI, a job's claimed slot) runs at
 * its next position, this pass or the next, whatever it waits for; its step
 * re-checks why (a woken periodic task, that it is due). */
void task_wake(uint8_t slot);

uint16_t sched_ticks(void);		/* the low 16 bits of the 200 Hz tick */
uint16_t sched_ms_to_ticks(uint16_t ms) __banked;	/* never early: ceil(ms / 5) + 1 */

void sched_init(void) __banked;		/* fixed tasks into their slots, all due now */
uint8_t sched_pass(void);		/* one pass over run_seq; 1 if any step ran */
uint8_t sched_events(void) __banked;	/* this pass's hardware events (events.md) */
void sched_idle(void) __banked;		/* sleep to the next interrupt, unless one is pending */
void sched_run(void) __banked;		/* never returns */
void sched_clear_stats(void) __banked;

/* measurement around each step and pass (sched_bank.c) */
void sched_fine_read(void) __banked;	/* now, into sched_f_tick/sched_f_cnt */
void sched_m_pass_start(void) __banked;
void sched_m_step_start(void) __banked;
void sched_m_step_end(void) __banked;
void sched_m_pass_end(void) __banked;

/* health (banked, with the names) */
void sched_health_tasks(void) __banked;
void sched_health_begin(void) __banked;	/* health tasks as the cli's job (E1): */
uint8_t sched_health_line(void) __banked;	/* a line a call, 0 after the last */
void sched_health_clear(void) __banked;

#endif
