/*
 * sched.c - the cooperative scheduler's core; see sched.h and
 * doc/sched/core.md. Resident (common area): what runs at every position of
 * every pass (the ready check, the dispatch) and the small calls every step
 * makes. The rest, once per pass or per step (event snapshot, idle,
 * measurement) or rarer (init, claim, sleep), is in sched_bank.c: no flash
 * operation outlives its step (flash.md), so banked code may run between
 * steps.
 *
 * Internal RAM: none. Every local of a function that calls another is a
 * static __xdata, and no function takes a second parameter (memory.md).
 */
#include <8051.h>
#include <stdint.h>
#include "rtl837x_sfr.h"
#include "sched.h"
#include "crash.h"

extern volatile __xdata uint32_t ticks;

__xdata struct task sched_task[SCHED_SLOTS];
__xdata uint8_t  sched_cur;
__xdata uint16_t sched_now;
__xdata uint16_t sched_ev;
__xdata uint16_t sched_posted;

__xdata uint16_t sched_st_max[S_COUNT];
__xdata uint16_t sched_st_runs[S_COUNT];
__xdata uint16_t sched_passes_lo, sched_passes_hi;
__xdata uint16_t sched_idle_lo, sched_idle_hi;
__xdata uint16_t sched_overruns;
__xdata uint8_t  sched_overrun_state;
__xdata uint16_t sched_pass_max;
__xdata uint8_t  sched_st;

__xdata struct task * __xdata sched_cur_task;	/* &sched_task[sched_cur] */

/* The low half of ticks: the ISR may carry between the two byte loads, so
 * read until the low byte agrees (as timer.c). */
uint16_t sched_ticks(void)
{
	uint16_t t;
	do {
		t = *(volatile __xdata uint16_t *)&ticks;
	} while ((uint8_t)t != *(volatile __xdata uint8_t *)&ticks);
	return t;
}

/* ---- the API for steps ---- */

void task_until(uint16_t tick)
{
	sched_cur_task->wake = tick;
}

void task_mask(uint16_t ev_mask)
{
	sched_cur_task->mask |= ev_mask;
}

/* ---- the loop ---- */

/* Due (the wake tick has come) or woken (an event in its mask). A wait
 * without timeout only has a horizon: when that comes, push it on. A leaf,
 * so its temporaries stay in registers. */
static uint8_t ready(void)
{
	if (sched_cur_task->mask & sched_ev)
		return 1;
	if ((int16_t)(sched_now - sched_cur_task->wake) < 0)
		return 0;
	if (sched_cur_task->mask & EV_FOREVER) {
		sched_cur_task->wake = sched_now + SCHED_MAX_WAIT;
		return 0;
	}
	return 1;
}

static __xdata uint8_t pos, st, ran;
uint8_t sched_pass(void)
{
	sched_m_pass_start();
	sched_now = sched_ticks();
	sched_ev = sched_events();
	ran = 0;
	for (pos = 0; pos < RUN_SEQ_LEN; pos++) {
		sched_cur = run_seq[pos];
		sched_cur_task = &sched_task[sched_cur];
		st = sched_cur_task->state;
		if (!st)
			continue;
		if (!ready())
			continue;
		sched_cur_task->mask = 0;	/* a step that says nothing yields */
		sched_cur_task->wake = sched_now;
		CRASH_NOTE(sched_cur, st);
		sched_st = st;
		sched_m_step_start();
		st = step_addr[st]();
		if (st != STEP_STAY)		/* the state it returned (STEP_END: 0, free) */
			sched_cur_task->state = st;
		sched_m_step_end();
		sched_now = sched_ticks();
		ran = 1;
	}
	sched_cur = SCHED_NONE;
	sched_m_pass_end();
	return ran;
}


