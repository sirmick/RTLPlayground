/*
 * sched_bank.c - the scheduler's banked half; see sched.c. The fine clock
 * and the per-state measurement, the event snapshot, idle and the loop,
 * init, task_claim and task_sleep.
 */
#include <8051.h>
#include <stdint.h>
#include "rtl837x_sfr.h"
#include "sched.h"
#include "con_ring.h"
#include "crash.h"

extern __xdata uint8_t hw_stuck;	/* rtlplayground.c (E4) */

extern volatile __xdata uint32_t ticks;
extern volatile __bit link_irq;
extern volatile __bit rx_irq;
#define cur sched_cur_task

/* ---- the fine clock: ticks + Timer 2 (counts from RCAP2 up to 0xffff) ---- */

__xdata uint16_t sched_fine_per_tick;	/* units of 64 counts per tick (813) */
static __xdata uint8_t fine_mid_h;	/* TH2 halfway between the reload and the overflow */
static __xdata uint16_t fine_reload;	/* RCAP2 */
static __xdata uint16_t fine_raw;
__xdata uint16_t sched_f_tick, sched_f_cnt;	/* the last fine_read() */
static __xdata uint16_t s_tick, s_cnt;	/* step start */
static __xdata uint16_t p_tick, p_cnt;	/* pass start */

static void fine_read(void)
{
	uint8_t h, l;
	EA = 0;
	h = TH2;
	l = TL2;
	if (TH2 != h) {
		h = TH2;
		l = TL2;
	}
	sched_f_tick = *(volatile __xdata uint16_t *)&ticks;
	/* Overflowed with the ISR still pending: the count is past the reload */
	if ((T2CON & 0x80) && h < fine_mid_h)
		sched_f_tick++;
	EA = 1;
	/* Before the timer's first overflow after it is started, it counts up
	 * from 0, below the reload: count that as the start of the tick. */
	fine_raw = ((uint16_t)h << 8) | l;
	if (fine_raw < fine_reload)
		fine_raw = fine_reload;
	sched_f_cnt = (uint16_t)(fine_raw - fine_reload) >> 6;	/* 16-bit, as SDCC does it */
}

/* Fine units from (e_tick, e_cnt) to the last fine_read(), saturating. */
static __xdata uint16_t e_tick, e_cnt, el;
static uint16_t fine_elapsed(void)
{
	e_tick = sched_f_tick - e_tick;
	if (e_tick >= 80)
		return 0xffff;
	el = sched_f_cnt - e_cnt;		/* may wrap below 0; the ticks make up for it */
	while (e_tick--)
		el += sched_fine_per_tick;
	return el;
}

static __xdata uint16_t d;

void sched_fine_read(void) __banked
{
	fine_read();
}

void sched_m_pass_start(void) __banked
{
	fine_read();
	p_tick = sched_f_tick;
	p_cnt = sched_f_cnt;
}

void sched_m_step_start(void) __banked
{
	hw_stuck = 0;		/* each step may wait out one poll timeout (E4) */
	fine_read();
	s_tick = sched_f_tick;
	s_cnt = sched_f_cnt;
}

void sched_m_step_end(void) __banked
{
	fine_read();
	e_tick = s_tick;
	e_cnt = s_cnt;
	d = fine_elapsed();
	if (sched_st_runs[sched_st] != 0xffff)
		sched_st_runs[sched_st]++;
	if (d > sched_st_max[sched_st])
		sched_st_max[sched_st] = d;
	if (d >= SCHED_OVERRUN_FINE) {
		sched_overruns++;
		sched_overrun_state = sched_st;
	}
}

void sched_m_pass_end(void) __banked
{
	e_tick = p_tick;
	e_cnt = p_cnt;
	fine_read();
	d = fine_elapsed();
	if (d > sched_pass_max)
		sched_pass_max = d;
	if (!++sched_passes_lo)
		sched_passes_hi++;
}

/* ---- sleeping ---- */

static __xdata uint16_t mt_ms, mt_q;	/* live across the division's call */
uint16_t sched_ms_to_ticks(uint16_t ms) __banked
{
	if (!ms)
		return 0;
	mt_ms = ms;
	mt_q = mt_ms / SCHED_TICK_MS;
	if (mt_ms != mt_q * SCHED_TICK_MS)
		mt_q++;
	return mt_q + 1;	/* the call may come just before a tick */
}

static __xdata uint16_t sl_ms;
void task_sleep(uint16_t ms) __banked
{
	if (ms == SCHED_FOREVER) {
		cur->mask |= EV_FOREVER;
		cur->wake = sched_ticks() + SCHED_MAX_WAIT;
		return;
	}
	sl_ms = sched_ms_to_ticks(ms);
	cur->wake = sched_ticks() + sl_ms;
}

__xdata uint16_t sched_claim_arg;	/* task_claim_arg's argument, consumed by the claim */

uint8_t task_claim(uint8_t first) __banked
{
	uint8_t i;
	for (i = T_FIXED; i < SCHED_SLOTS; i++) {
		if (!sched_task[i].state) {
			sched_task[i].state = first;
			sched_task[i].mask = 0;
			sched_task[i].wake = sched_now;
			sched_task[i].arg = sched_claim_arg;
			sched_claim_arg = 0;
			return i;
		}
	}
	sched_claim_arg = 0;
	return SCHED_NONE;
}

void sched_clear_stats(void) __banked
{
	uint8_t i;
	for (i = 0; i < S_COUNT; i++) {
		sched_st_max[i] = 0;
		sched_st_runs[i] = 0;
	}
	sched_passes_lo = sched_passes_hi = 0;
	sched_idle_lo = sched_idle_hi = 0;
	sched_overruns = 0;
	sched_overrun_state = 0;
	sched_pass_max = 0;
}

static __xdata uint8_t ii;
void sched_init(void) __banked
{
	sched_now = sched_ticks();
	for (ii = 0; ii < SCHED_SLOTS; ii++) {
		sched_task[ii].state = ii < T_FIXED ? fixed_first[ii] : 0;
		sched_task[ii].mask = 0;
		sched_task[ii].wake = sched_now;
		sched_task[ii].arg = 0;
	}
	sched_cur = SCHED_NONE;
	fine_reload = ((uint16_t)RCAP2H << 8) | RCAP2L;
	sched_fine_per_tick = (uint16_t)(0 - fine_reload) >> 6;
	fine_mid_h = ((uint16_t)RCAP2H + 0x100) >> 1;
	sched_clear_stats();
}

/* ---- hardware events (events.md) ---- */

static __xdata uint8_t ev;
uint8_t sched_events(void) __banked
{
	ev = 0;
	EA = 0;
	if (link_irq) {
		link_irq = 0;
		ev |= HW_LINK;
	}
	if (rx_irq) {
		rx_irq = 0;
		ev |= HW_RX;
	}
	EA = 1;
	if (con_rx_ready())
		ev |= HW_CON_RX;
	if (con_tx_room() >= CON_TX_LINE)
		ev |= HW_CON_TX;
	return ev;
}

/* Sleep to the next interrupt, unless there is something to do already.
 * The IE write delays the interrupt by one instruction, so the interrupt
 * that should wake us is taken after the PCON write (core.md). A tick since
 * the pass started counts: a task may have come due after its position. */
void sched_idle(void) __banked
{
	if (!++sched_idle_lo)
		sched_idle_hi++;
	EA = 0;
	if (link_irq || rx_irq || con_rx_ready()
	    || *(volatile __xdata uint8_t *)&ticks != (uint8_t)sched_now) {
		EA = 1;
		return;
	}
	EA = 1;
	PCON |= 1;
}

void sched_run(void) __banked
{
	swdt_kick();
	swdt_on = 1;
	for (;;) {
		swdt_kick();
		if (!sched_pass())
			sched_idle();
	}
}
