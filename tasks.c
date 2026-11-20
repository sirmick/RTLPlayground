/*
 * tasks.c - the firmware's fixed tasks (doc/sched/modules.md), declared
 * by their TASK/STEP lines (sched.h): the old main loop's phases, at its
 * cadence. The phases' bodies are
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
#include "cmd_parser.h"
#include "httpd/httpd.h"
#include "flash_job.h"
#include "uip/uip.h"
#include "uip/uip_arp.h"
#include "machine.h"
#include "timer.h"
#include "sched.h"
#include "con_ring.h"
#include "crash.h"

#define SEC_TICKS	200	/* 1 s */
#define SFP_TICKS	10	/* the SFP pins every 50 ms */
#define STP_CATCH_UP	8	/* stp_timers() runs per tick at most */

/* rtlplayground.c */
void house_second(void);
void link_service(void);
uint8_t rx_service(void);
void handle_tx(void);

extern __code const struct machine machine;

static __xdata uint16_t house_due, loop_due;
static __xdata uint8_t ti, tn;
static __xdata uint8_t rx_wait;	/* cli waited for outbuf before this step */

/* rx: woken by INT1 (HW_RX); up to RX_BUDGET frames a step */
TASK(rx, fixed, high, first=idle)
STEP(rx, idle)
{
	return STEP_WAIT(S_RX_STEP, HW_RX, SCHED_FOREVER);
}

STEP(rx, step)
{
	rx_wait = webcmd_wait;		/* the ACK that frees outbuf clears it */
	tn = rx_service();
	if (webcmd_pending || rx_wait)
		task_wake(T_CLI);	/* httpd queued a /cmd, or an ACK may have freed outbuf */
	if (tn)
		return STEP_STAY;	/* more waiting: again at the next position */
	return STEP_WAIT(S_RX_STEP, HW_RX, SCHED_FOREVER);
}

/* link: woken by INT0, and by house once a second (link_irq) */
TASK(link, fixed, high)
STEP(link, read)
{
	link_service();
	return STEP_WAIT(S_LINK_READ, HW_LINK, SCHED_FOREVER);
}

/* loop: rldp_tick() once a second */
TASK(loop, fixed, high, first=start)
STEP(loop, start)
{
	loop_due = sched_now + SEC_TICKS;
	task_until(loop_due);
	return S_LOOP_POLL;
}

STEP(loop, poll)
{
	if (rldp_enabled)
		rldp_tick();
	loop_due += SEC_TICKS;
	task_until(loop_due);
	return STEP_STAY;
}

/* con: today's line editor, on its own line; a complete line goes to cli */
TASK(con, fixed, high)
STEP(con, edit)
{
	cmd_edit();
	if (con_line_ready)
		task_wake(T_CLI);
	return STEP_WAIT(S_CON_EDIT, HW_CON_RX, SCHED_FOREVER);
}

/* cli: one command at a time, a console line or a web /cmd (console.md).
 * The prompt only from CLI_PROMPT. A dump runs as a job, a line a step in
 * CLI_MORE (E1); a web /cmd a line a step in CLI_NEXT, each step with the
 * capture on, so only cli's own output goes into the reply. A reply that
 * outgrows outbuf isn't cut: cli waits (webcmd_wait) while httpd sends what
 * is there, and is woken by rx on the ACKs; the timeout notices a
 * connection that ended in the net task. */
#define CLI_CON	1
#define CLI_WEB	2
/* Free TX ring a command starts with: more than any unchunked command
 * prints (the tour's most, lacp show, ~600 B), so it doesn't wait on the
 * UART; a job's line needs CON_TX_LINE. */
#define CLI_ROOM	1024
#define CLI_WEB_WAIT	50	/* ms: a wait for outbuf's room, at most */
/* outbuf's room a web step starts with: more than a command or a job's
 * line prints (CLI_ROOM's bound is the ring's, not the commands') */
#define CLI_WEB_ROOM	640
static __xdata uint8_t cli_src, cli_web_more;
static __xdata uint8_t cli_gen;		/* the web command this run serves (webcmd_gen) */
static __xdata uint8_t cli_dropped;	/* its connection ended: no reply, no more lines */
static __xdata uint16_t cli_reset_by;	/* do sys.reset over the web: the latest tick for it */
#define CLI_RESET_TICKS	(3000 / SCHED_TICK_MS)

TASK(cli, fixed, once, first=idle)
STEP(cli, idle)
{
	if (con_line_ready) {
		cli_src = CLI_CON;
		return S_CLI_RUN;
	}
	if (webcmd_pending) {
		cli_src = CLI_WEB;
		return S_CLI_RUN;
	}
	return STEP_SLEEP(S_CLI_IDLE, SCHED_FOREVER);	/* woken by con (a line) or rx (a /cmd) */
}

STEP(cli, run)
{
	if (cli_src == CLI_WEB) {
		/* still queued: its connection may have ended since CLI_IDLE,
		 * and then config_buf may hold the next request */
		if (!webcmd_pending)
			return S_CLI_IDLE;
		webcmd_pending = 0;
		cli_gen = webcmd_gen;
		cli_dropped = 0;
		httpd_webcmd_begin();
		return S_CLI_NEXT;
	}
	if (con_tx_room() < CLI_ROOM)
		return STEP_WAIT(S_CLI_RUN, HW_CON_TX, SCHED_FOREVER);
	for (ti = 0; ti < CMD_BUF_SIZE; ti++)	/* the console's line into cmd_buffer */
		if (!(cmd_buffer[ti] = con_line[ti]))
			break;
	if (!cmd_buffer[0])
		return S_CLI_PROMPT;
	cmd_tokenize();
	if (err_status == ERR_OK)
		cmd_parser();
	if (cli_job)
		return S_CLI_MORE;
	return S_CLI_PROMPT;
}

/* The web command this run serves still has its connection waiting: not
 * ended, and no new /cmd queued since (which would hold config_buf). */
static uint8_t cli_web_live(void)
{
	if (!webcmd_busy)	/* an if, not &&: SDCC gave && a bit of internal RAM */
		return 0;
	return webcmd_gen == cli_gen;
}

/* 1: no room in outbuf for the step, or httpd is sending it: wait */
static uint8_t cli_web_full(void)
{
	if (!webcmd_wait && WEBCMD_ROOM(CLI_WEB_ROOM))
		return 0;
	webcmd_wait = 1;
	task_sleep(CLI_WEB_WAIT);
	return 1;
}

/* Its connection ended: no reply and no more of its lines (config_buf may
 * hold the next request already). A pure dump stops; a job with side
 * effects (stp off's entries) runs to its end, its output discarded. */
static void cli_web_drop(void)
{
	cli_dropped = 1;
	if (!CLI_JOB_KEEPS(cli_job))
		cli_job = 0;
}

STEP(cli, next)
{
	if (!cli_web_live()) {
		cli_web_drop();
		return S_CLI_PROMPT;
	}
	if (con_tx_room() < CLI_ROOM)		/* the reply's console mirror */
		return STEP_WAIT(S_CLI_NEXT, HW_CON_TX, SCHED_FOREVER);
	if (cli_web_full())
		return S_CLI_NEXT;
	WEBCMD_CAPTURE_ON();
	cli_web_more = exec_line();
	WEBCMD_CAPTURE_OFF();
	if (cli_job)
		return S_CLI_MORE;
	if (!cli_web_more)
		return S_CLI_PROMPT;
	return S_CLI_NEXT;			/* the next line at the next step */
}

STEP(cli, more)
{
	if (cli_src == CLI_WEB && !cli_dropped && !cli_web_live()) {
		cli_web_drop();
		if (!cli_job)
			return S_CLI_PROMPT;
	}
	if (con_tx_room() < CON_TX_LINE)	/* the console (or its mirror of the reply) */
		return STEP_WAIT(S_CLI_MORE, HW_CON_TX, SCHED_FOREVER);
	if (cli_src == CLI_WEB && !cli_dropped) {
		if (cli_web_full())
			return S_CLI_MORE;
		WEBCMD_CAPTURE_ON();
	}
	cli_job_more();
	if (cli_src == CLI_WEB && !cli_dropped)
		WEBCMD_CAPTURE_OFF();
	if (cli_job_sleep) {			/* the eee job's PHY reset (E4) */
		cli_job_sleep = 0;
		return STEP_SLEEP(S_CLI_MORE, CLI_JOB_SLEEP_MS);
	}
	if (cli_job)
		return S_CLI_MORE;		/* its next line at the next step */
	if (cli_src == CLI_WEB && !cli_dropped && cli_web_more)
		return S_CLI_NEXT;
	return S_CLI_PROMPT;
}

STEP(cli, prompt)
{
	if (fl_kind >= FL_FACTORY)		/* do sys.factory_reset: its job first */
		return STEP_SLEEP(S_CLI_PROMPT, 100);
	if (cli_src == CLI_WEB) {
		if (!cli_dropped && cli_web_live()) {
			httpd_webcmd_end();	/* httpd sends the reply on a poll */
			cli_dropped = 1;	/* done with it: a reset's wait comes back here */
			cli_reset_by = sched_now + CLI_RESET_TICKS;
		}
		/* do sys.reset: httpd resets once the reply's connection closes;
		 * this, if it never does (a client that leaves it half open) */
		if (crash_pending) {
			if ((int16_t)(sched_now - cli_reset_by) < 0)
				return STEP_SLEEP(S_CLI_PROMPT, 100);
			reset_chip(crash_pending);
		}
	} else {
		if (crash_pending)		/* do sys.reset, sys.factory_reset: after the ok */
			reset_chip(crash_pending);
		print_cmd_prompt();
		con_line_ready = 0;		/* con reads on: type-ahead echoes after the prompt */
	}
	return S_CLI_IDLE;
}

/* net: uIP's periodic calls, every tick; and, woken by the flash job's end
 * (fl_poll), a poll of the connection at once, which sends an upload's
 * answer, or a window update if its window was short (net.md item 1). Woken,
 * the periodic part still waits until it is due. */
static __xdata uint16_t net_due;

TASK(net, fixed, once, first=start)
STEP(net, start)
{
	net_due = sched_now;
	return S_NET_TICK;
}

STEP(net, tick)
{
	if (fl_poll) {
		fl_poll = 0;
		uip_poll_conn(&uip_conns[0]);
		if (uip_len > 0) {
			uip_arp_out();
			tcpip_output();
		}
	}
	if ((int16_t)(sched_now - net_due) >= 0) {
		handle_tx();
		net_due = sched_now + 1;
	}
	task_until(net_due);
	return STEP_STAY;
}

/* stp: its 50 Hz timer, caught up by at most STP_CATCH_UP a tick */
TASK(stp, fixed, medium)
STEP(stp, tick)
{
	if (stp_enabled) {
		tn = STP_CATCH_UP;
		while (tn-- && timer_expired(&stp_step)) {
			timer_reset(&stp_step);
			stp_timers();
		}
	}
	task_until(sched_now + 1);
	return STEP_STAY;
}

TASK(lacp, fixed, medium)
STEP(lacp, tick)
{
	if (lacpEnabled)
		lacp_timers();
	task_until(sched_now + 1);
	return STEP_STAY;
}

/* lldp, and the cable test's state machine (a job from E on), every tick */
TASK(lldp, fixed, once)
STEP(lldp, tick)
{
	lldp_service();
	cabletest_tick();
	task_until(sched_now + 1);
	return STEP_STAY;
}

/* sfp0, sfp1 (E2, modules.md "SFP, per slot"): a slot's pins every 50 ms;
 * none on a board without the slot. An inserted module waits out its
 * wake-up delay in SFP_WAKE, is read a transaction a step in SFP_READ,
 * configured in SFP_CONFIG; then SFP_DDM keeps its diagnostics in the
 * cache the web pages read, every SFP_DDM_POLLS polls, 4 transactions. */
TASK(sfp, fixed, once, count=2, first=start)
STEP(sfp, start)
{
	ti = sched_cur - T_SFP0;
	if (ti >= machine.n_sfp)
		return STEP_END;
	task_until(sched_now + SFP_TICKS);
	return STEP_ARG(S_SFP_POLL, sched_now + SFP_TICKS);	/* arg: the slot's poll tick */
}

/* the next 50 ms point (the slot's arg); behind (after a read): now */
static __xdata uint16_t sfp_t;
static void sfp_next_poll(void)
{
	sfp_t = task_arg() + SFP_TICKS;
	if ((int16_t)(sfp_t - sched_now) < 0)
		sfp_t = sched_now;
	task_set_arg(sfp_t);
	task_until(sfp_t);
}

STEP(sfp, poll)
{
	ti = sched_cur - T_SFP0;
	if (sfp_pins(ti))
		return S_SFP_READ;
	if (sfp_ddm_wanted(ti))
		return S_SFP_DDM;
	sfp_next_poll();
	if (sfp_wake_pending[ti])
		return S_SFP_WAKE;
	return STEP_STAY;
}

STEP(sfp, wake)
{
	ti = sched_cur - T_SFP0;
	if (sfp_pins(ti))
		return S_SFP_READ;
	sfp_next_poll();
	if (!sfp_wake_pending[ti])		/* out again */
		return S_SFP_POLL;
	return STEP_STAY;
}

STEP(sfp, read)
{
	ti = sched_cur - T_SFP0;
	tn = sfp_read_next(ti);
	if (tn == SFP_MORE)
		return STEP_STAY;
	if (tn == SFP_FAIL)			/* said; the next poll finds it again */
		return S_SFP_POLL;
	return S_SFP_CONFIG;
}

STEP(sfp, config)
{
	ti = sched_cur - T_SFP0;
	sfp_configure(ti);
	if (sfp_ddm_wanted(ti))			/* its first refresh at once */
		return S_SFP_DDM;
	return S_SFP_POLL;
}

STEP(sfp, ddm)
{
	ti = sched_cur - T_SFP0;
	if (sfp_ddm_next(ti) == SFP_MORE)
		return STEP_STAY;
	return S_SFP_POLL;			/* done, or failed: again at the next refresh */
}

/* house: once a second */
TASK(house, fixed, once, first=start)
STEP(house, start)
{
	house_due = sched_now + SEC_TICKS;
	task_until(house_due);
	return S_HOUSE_SEC;
}

STEP(house, sec)
{
	house_second();
	house_due += SEC_TICKS;
	task_until(house_due);
	return STEP_STAY;
}
