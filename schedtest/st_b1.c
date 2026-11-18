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

void sleep_a(void) __banked
{
	st_lp = &st_log_sleep_a;
	st_aux = 0;
	st_log();
	task_sleep(5);
}

/* Woken by the poster's EV_ST_POKE, or after 100 ms */
void waiter(void) __banked
{
	st_lp = &st_log_waiter;
	st_aux = sched_ev & EV_ST_POKE ? 1 : 2;
	st_log();
	task_wait(EV_ST_POKE, 100);
}

/* freq=high, ~1 us of work: when it runs, and the stack at a step's entry */
void hot(void) __banked
{
	if (SP > st_sp_step_max)
		st_sp_step_max = SP;
	st_lp = &st_log_hot;
	st_aux = sched_passes_lo;
	st_log();
}

/* One line per step, only when the TX ring has room for it */
static __xdata uint16_t pr_n;

void pr_idle(void) __banked
{
	if (!st_print_req) {
		task_wait(EV_ST_GO, SCHED_FOREVER);
		return;
	}
	pr_n = 0;
	task_next(S_PR_LINE);
}

void pr_line(void) __banked
{
	if (con_tx_room() < 40) {
		task_wait(EV_CON_TX, SCHED_FOREVER);
		return;
	}
	print_string("P");
	itoa_short(pr_n);
	print_string(" the quick brown fox jumps\n");
	if (++pr_n < st_print_req)
		return;
	st_print_done = pr_n;
	st_print_req = 0;
	print_string("print done\n");
	task_next(S_PR_IDLE);
}
