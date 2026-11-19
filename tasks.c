/*
 * tasks.c - the firmware's fixed tasks (doc/sched/modules.md, tasks.def):
 * the old main loop's phases, at its cadence. The phases' bodies are
 * rtlplayground.c's (resident, as the loop was); each step calls one and
 * says when it is due again. Periodic work runs at task_until(last +
 * period), which catches up after a long step as the old timer_reset() did
 * (a task_sleep() would add a tick each time: silicon.md); the per-tick
 * phases run once at the first pass of each tick, as the old tick_pending.
 */
#include <8051.h>
#include <stdint.h>
#include <stdbool.h>
#include "rtl837x_common.h"
#include "rtl837x_stp.h"
#include "rtl837x_lacp.h"
#include "rtl837x_lldp.h"
#include "rtl837x_rldp.h"
#include "rtl837x_cabletest.h"
#include "sfp.h"
#include "cmd_editor.h"
#include "machine.h"
#include "timer.h"
#include "sched.h"

#define SEC_TICKS	200	/* 1 s */
#define SFP_TICKS	10	/* the SFP pins every 50 ms */
#define STP_CATCH_UP	8	/* stp_timers() runs per tick at most */

/* rtlplayground.c */
void house_second(void);
void link_service(void);
uint8_t rx_service(void);
void cmd_service(void);
void handle_tx(void);

extern __code const struct machine machine;

static __xdata uint16_t house_due, loop_due;
static __xdata uint16_t sfp_due[2];
static __xdata uint8_t ti, tn;

/* rx: woken by INT1 (EV_RX); up to RX_BUDGET frames a step */
void rx_wait(void) __banked
{
	STEP_WAIT(S_RX_STEP, EV_RX, SCHED_FOREVER);
}

void rx_task(void) __banked
{
	if (rx_service())
		return;			/* more waiting: again at the next position */
	STEP_WAIT(S_RX_STEP, EV_RX, SCHED_FOREVER);
}

/* link: woken by INT0, and by house once a second (link_irq) */
void link_task(void) __banked
{
	link_service();
	STEP_WAIT(S_LINK_READ, EV_LINK, SCHED_FOREVER);
}

/* loop: rldp_tick() once a second */
void loop_start(void) __banked
{
	loop_due = sched_now + SEC_TICKS;
	task_until(loop_due);
	STEP_NEXT(S_LOOP_POLL);
}

void loop_task(void) __banked
{
	if (rldp_enabled)
		rldp_tick();
	loop_due += SEC_TICKS;
	task_until(loop_due);
}

/* con: today's editor and command parser */
void con_task(void) __banked
{
	cmd_edit();
	cmd_service();
	STEP_WAIT(S_CON_EDIT, EV_CON_RX, SCHED_FOREVER);
}

/* net: uIP's periodic calls, every tick */
void net_task(void) __banked
{
	handle_tx();
	task_until(sched_now + 1);
}

/* stp: its 50 Hz timer, caught up by at most STP_CATCH_UP a tick */
void stp_task(void) __banked
{
	if (stp_enabled) {
		tn = STP_CATCH_UP;
		while (tn-- && timer_expired(&stp_step)) {
			timer_reset(&stp_step);
			stp_timers();
		}
	}
	task_until(sched_now + 1);
}

void lacp_task(void) __banked
{
	if (lacpEnabled)
		lacp_timers();
	task_until(sched_now + 1);
}

/* lldp, and the cable test's state machine (a job from E on), every tick */
void lldp_task(void) __banked
{
	lldp_tick();
	cabletest_tick();
	task_until(sched_now + 1);
}

/* sfp0, sfp1: a slot's pins every 50 ms; none on a board without the slot */
void sfp_start(void) __banked
{
	ti = sched_cur - T_SFP0;
	if (ti >= machine.n_sfp)
		STEP_END();
	sfp_due[ti] = sched_now + SFP_TICKS;
	task_until(sfp_due[ti]);
	STEP_NEXT(S_SFP_POLL);
}

void sfp_task(void) __banked
{
	ti = sched_cur - T_SFP0;
	handle_sfp_slot(ti);
	sfp_due[ti] += SFP_TICKS;
	task_until(sfp_due[ti]);
}

/* house: once a second */
void house_start(void) __banked
{
	house_due = sched_now + SEC_TICKS;
	task_until(house_due);
	STEP_NEXT(S_HOUSE_SEC);
}

void house_task(void) __banked
{
	house_second();
	house_due += SEC_TICKS;
	task_until(house_due);
}
