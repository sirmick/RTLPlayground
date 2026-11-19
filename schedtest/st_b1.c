/*
 * st_b1.c - schedtest dummies in BANK1 (pinned by schedtest.mk):
 * sleep_a (5 ms), waiter, hot (high frequency), printer.
 */
#include <8051.h>
#include <stdint.h>
#include "rtl837x_common.h"
#include "sched.h"
#include "con_ring.h"
#include "schedtest/schedtest.h"

__xdata uint16_t st_print_done;
__xdata uint8_t  st_sp_step_max;

void deep_1(void) __banked
{
	if (--st_deep_left)
		deep_2();
	else
		st_deep_bottom();
}

TASK(sleep_a, fixed, once)
STEP(sleep_a, step)
{
	st_lp = &st_log_sleep_a;
	st_aux = 0;
	st_log();
	return STEP_SLEEP(S_SLEEP_A_STEP, 5);
}

/* Woken by the poster's EV_ST_POKE, or after 100 ms */
TASK(waiter, fixed, once)
STEP(waiter, step)
{
	st_lp = &st_log_waiter;
	st_aux = sched_ev & EV_ST_POKE ? 1 : 2;
	st_log();
	return STEP_WAIT(S_WAITER_STEP, EV_ST_POKE, 100);
}

/* freq=high, ~1 us of work while `hot on`: when it runs, and the stack at
 * a step's entry. Off by default: its yield keeps every pass busy, so the
 * scheduler (and the simulator) never idles. */
__xdata uint8_t st_hot_on;

TASK(hot, fixed, high)
STEP(hot, step)
{
	if (!st_hot_on)
		return STEP_WAIT(S_HOT_STEP, EV_ST_GO, SCHED_FOREVER);
	if (SP > st_sp_step_max)
		st_sp_step_max = SP;
	st_lp = &st_log_hot;
	st_aux = sched_passes_lo;
	st_log();
	return STEP_STAY;
}

/* One line per step, only when the TX ring has room for it; the slot's arg
 * is the line number */
TASK(pr, fixed, once, first=idle)
STEP(pr, idle)
{
	if (!st_print_req)
		return STEP_WAIT(S_PR_IDLE, EV_ST_GO, SCHED_FOREVER);
	return STEP_ARG(S_PR_LINE, 0);
}

STEP(pr, line)
{
	if (con_tx_room() < 40)
		return STEP_WAIT(S_PR_LINE, EV_CON_TX, SCHED_FOREVER);
	print_string("P");
	itoa_short(task_arg());
	print_string(" the quick brown fox jumps\n");
	if (++task_arg() < st_print_req)
		return STEP_STAY;	/* the next line at the next position */
	st_print_done = task_arg();
	st_print_req = 0;
	print_string("print done\n");
	return S_PR_IDLE;
}
