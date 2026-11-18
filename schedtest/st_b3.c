/*
 * st_b3.c - schedtest dummies in BANK3 (pinned by schedtest.mk):
 * sleep_c (1 s), isr_ev (UART RX ISR -> event -> task), slow.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "sched.h"
#include "con_ring.h"
#include "schedtest/schedtest.h"

__xdata uint16_t st_slow_runs;
volatile __xdata uint16_t st_hang_spins;

void sleep_c(void) __banked
{
	st_lp = &st_log_sleep_c;
	st_aux = 0;
	st_log();
	task_sleep(1000);
}

/* Woken by EV_CON_RX alone (con drains the ring) */
void isr_ev(void) __banked
{
	if (sched_ev & EV_CON_RX) {
		st_lp = &st_log_isr_ev;
		st_aux = con_rx_head;
		st_log();
	}
	task_wait(EV_CON_RX, SCHED_FOREVER);
}

/* slow: one 25-30 ms step on request (an overrun); hang: a step that never
 * returns (ST_NEG=hang), for the watchdog. ST_NEG=overrun runs slow once by
 * itself, 2 s after boot. */
static __xdata uint16_t sl_t0;

void sl_idle(void) __banked
{
#ifdef ST_NEG_OVERRUN
	static __xdata uint8_t auto_done;
	if (!auto_done) {
		auto_done = 1;
		st_slow_req = 1;
		task_sleep(2000);
		return;
	}
#endif
	if (st_slow_req == 1)
		task_next(S_SL_SLOW);
	else if (st_slow_req == 2)
		task_next(S_SL_HANG);
	else
		task_wait(EV_ST_GO, SCHED_FOREVER);
}

void sl_slow(void) __banked
{
	sl_t0 = sched_ticks();
	while ((uint16_t)(sched_ticks() - sl_t0) < 6)	/* 25-30 ms */
		;
	st_slow_runs++;
	st_slow_req = 0;
	task_next(S_SL_IDLE);
}

void sl_hang(void) __banked
{
#ifdef ST_NEG_HANG
	for (;;)		/* busy, not a jump-to-self (which rtlsim takes for a halt) */
		st_hang_spins++;
#endif
	st_slow_req = 0;
	task_next(S_SL_IDLE);
}
