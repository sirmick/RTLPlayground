/*
 * st_b3.c - schedtest dummies in BANK3 (pinned by schedtest.mk):
 * sleep_c (1 s), isr_ev (UART RX ISR -> event -> task), slow.
 */
#include <8051.h>
#include <stdint.h>
#include "rtl837x_common.h"
#include "sched.h"
#include "con_ring.h"
#include "schedtest/schedtest.h"

__xdata uint16_t st_slow_runs;
__xdata uint8_t  st_stack_paint_from, st_stack_hw;
__xdata uint8_t  st_deep_depth, st_deep_left, st_deep_sp;
__xdata uint16_t st_deep_runs;
static __xdata uint8_t si;

/* With interrupts off, so no ISR pushes into the area mid-paint */
void st_stack_paint(void) __banked
{
	st_stack_paint_from = SP + 8;
	st_stack_hw = 0;
	EA = 0;
	for (si = 0xff; si >= st_stack_paint_from; si--)
		*(__idata uint8_t *)si = ST_PAINT;
	EA = 1;
}

static void stack_scan(void)
{
	for (si = 0xff; si >= st_stack_paint_from; si--) {
		if (*(__idata uint8_t *)si != ST_PAINT) {
			if (si > st_stack_hw)
				st_stack_hw = si;
			return;
		}
	}
}

void deep_3(void) __banked
{
	if (--st_deep_left)
		deep_1();
	else
		st_deep_bottom();
}

/* The bottom of the chain: note SP, then keep the console busy (a line
 * out; input, if the test sends it, comes in) for 5-10 ms */
static __xdata uint16_t dt0;
void st_deep_bottom(void) __banked
{
	if (SP > st_deep_sp)
		st_deep_sp = SP;
	print_string("deep\n");
	dt0 = sched_ticks();
	while ((uint16_t)(sched_ticks() - dt0) < 2)
		;
}

void deep(void) __banked
{
	if (st_deep_depth) {
		st_deep_left = st_deep_depth;
		deep_1();
		st_deep_runs++;
	}
	STEP_SLEEP(S_DEEP, 20);
}
volatile __xdata uint16_t st_hang_spins;

void sleep_c(void) __banked
{
	stack_scan();
	st_lp = &st_log_sleep_c;
	st_aux = 0;
	st_log();
	STEP_SLEEP(S_SLEEP_C, 1000);
}

/* Woken by EV_CON_RX alone (con drains the ring) */
void isr_ev(void) __banked
{
	if (sched_ev & EV_CON_RX) {
		st_lp = &st_log_isr_ev;
		st_aux = con_rx_head;
		st_log();
	}
	STEP_WAIT(S_ISR_EV, EV_CON_RX, SCHED_FOREVER);
}

/* slow: one step of st_slow_ms on request (default 30, an overrun;
 * the length rides into SL_SLOW in the slot's arg); hang: a step that never
 * returns (ST_NEG=hang), for the watchdog. ST_NEG=overrun runs slow once by
 * itself, 2 s after boot. */
static __xdata uint16_t sl_t0, sl_c0, sl_n, sl_dt, sl_el;	/* up to 400 ms */
__xdata uint16_t st_slow_ms = 30;

void sl_idle(void) __banked
{
#ifdef ST_NEG_OVERRUN
	static __xdata uint8_t auto_done;
	if (!auto_done) {
		auto_done = 1;
		st_slow_req = 1;
		STEP_SLEEP(S_SL_IDLE, 2000);
	}
#endif
	if (st_slow_req == 1)
		STEP_ARG(S_SL_SLOW, st_slow_ms);
	if (st_slow_req == 2)
		STEP_NEXT(S_SL_HANG);
	STEP_WAIT(S_SL_IDLE, EV_ST_GO, SCHED_FOREVER);
}

void sl_slow(void) __banked
{
	sl_n = task_arg() * 163;		/* fine units (6.144 us): ms x 162.8 */
	sched_fine_read();
	sl_t0 = sched_f_tick;
	sl_c0 = sched_f_cnt;
	do {					/* the fine clock: ms within ~10 us */
		sched_fine_read();
		sl_dt = sched_f_tick - sl_t0;
		sl_el = sched_f_cnt - sl_c0;
		while (sl_dt--)
			sl_el += sched_fine_per_tick;
	} while (sl_el < sl_n);
	st_slow_runs++;
	st_slow_req = 0;
	STEP_NEXT(S_SL_IDLE);
}

void sl_hang(void) __banked
{
#ifdef ST_NEG_HANG
	for (;;)		/* busy, not a jump-to-self (which rtlsim takes for a halt) */
		st_hang_spins++;
#endif
	st_slow_req = 0;
	STEP_NEXT(S_SL_IDLE);
}
