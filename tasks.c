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
#include "cmd_parser.h"
#include "httpd/httpd.h"
#include "flash_job.h"
#include "uip/uip.h"
#include "uip/uip_arp.h"
#include "machine.h"
#include "timer.h"
#include "sched.h"
#include "con_ring.h"

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
static __xdata uint16_t sfp_due[2];
static __xdata uint8_t ti, tn;

/* rx: woken by INT1 (EV_RX); up to RX_BUDGET frames a step */
void rx_wait(void) __banked
{
	STEP_WAIT(S_RX_STEP, EV_RX, SCHED_FOREVER);
}

void rx_task(void) __banked
{
	tn = rx_service();
	if (webcmd_pending)
		event_post(EV_WEBCMD);	/* httpd queued a /cmd */
	if (tn)
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

/* con: today's line editor, on its own line; a complete line goes to cli */
void con_task(void) __banked
{
	cmd_edit();
	if (con_line_ready)
		event_post(EV_LINE);
	STEP_WAIT(S_CON_EDIT, EV_CON_RX, SCHED_FOREVER);
}

/* cli: one command at a time, a console line or a web /cmd (console.md).
 * The prompt only from CLI_PROMPT. A dump runs as a job, a line a step in
 * CLI_MORE (E1); a web /cmd a line a step in CLI_NEXT, each step with the
 * capture on, so only cli's own output goes into the reply. */
#define CLI_CON	1
#define CLI_WEB	2
/* Free TX ring a command starts with: more than any unchunked command
 * prints (the tour's most, lacp show, ~600 B), so it doesn't wait on the
 * UART; a job's line needs CON_TX_LINE. */
#define CLI_ROOM	1024
static __xdata uint8_t cli_src, cli_web_more;

void cli_idle(void) __banked
{
	if (con_line_ready) {
		cli_src = CLI_CON;
		STEP_NEXT(S_CLI_RUN);
	}
	if (webcmd_pending) {
		cli_src = CLI_WEB;
		STEP_NEXT(S_CLI_RUN);
	}
	STEP_WAIT(S_CLI_IDLE, EV_LINE | EV_WEBCMD, SCHED_FOREVER);
}

void cli_run(void) __banked
{
	if (cli_src == CLI_WEB) {
		/* still queued: its connection may have ended since CLI_IDLE,
		 * and then config_buf may hold the next request */
		if (!webcmd_pending)
			STEP_NEXT(S_CLI_IDLE);
		webcmd_pending = 0;
		httpd_webcmd_begin();
		STEP_NEXT(S_CLI_NEXT);
	}
	if (con_tx_room() < CLI_ROOM)
		STEP_WAIT(S_CLI_RUN, EV_CON_TX, SCHED_FOREVER);
	task_next(S_CLI_PROMPT);
	for (ti = 0; ti < CMD_BUF_SIZE; ti++)	/* the console's line into cmd_buffer */
		if (!(cmd_buffer[ti] = con_line[ti]))
			break;
	if (!cmd_buffer[0])
		return;
	cmd_tokenize();
	if (err_status == ERR_OK)
		cmd_parser();
	if (cli_job)
		STEP_NEXT(S_CLI_MORE);
}

/* A web command's connection ended: stop, nobody reads the reply, and
 * config_buf may soon hold the next request. */
static void cli_web_drop(void)
{
	cli_job = 0;
	task_next(S_CLI_IDLE);
}

void cli_next(void) __banked
{
	if (!webcmd_busy) {
		cli_web_drop();
		return;
	}
	if (con_tx_room() < CLI_ROOM)		/* the reply's console mirror */
		STEP_WAIT(S_CLI_NEXT, EV_CON_TX, SCHED_FOREVER);
	WEBCMD_CAPTURE_ON();
	cli_web_more = exec_line();
	WEBCMD_CAPTURE_OFF();
	if (cli_job)
		STEP_NEXT(S_CLI_MORE);
	if (!cli_web_more)
		STEP_NEXT(S_CLI_PROMPT);
	/* stays in CLI_NEXT: the next line at the next step */
}

void cli_more(void) __banked
{
	if (cli_src == CLI_WEB && !webcmd_busy) {
		cli_web_drop();
		return;
	}
	if (con_tx_room() < CON_TX_LINE)	/* the console (or its mirror of the reply) */
		STEP_WAIT(S_CLI_MORE, EV_CON_TX, SCHED_FOREVER);
	if (cli_src == CLI_WEB)
		WEBCMD_CAPTURE_ON();
	cli_job_more();
	if (cli_src == CLI_WEB)
		WEBCMD_CAPTURE_OFF();
	if (cli_job)
		return;			/* stays in CLI_MORE: its next line at the next step */
	if (cli_src == CLI_WEB && cli_web_more)
		STEP_NEXT(S_CLI_NEXT);
	STEP_NEXT(S_CLI_PROMPT);
}

void cli_prompt(void) __banked
{
	if (fl_kind >= FL_FACTORY)		/* factory-reset: no prompt, the job resets */
		STEP_SLEEP(S_CLI_PROMPT, 100);
	if (cli_src == CLI_WEB) {
		if (webcmd_busy) {		/* not if its connection ended */
			httpd_webcmd_end();
			event_post(EV_CMD_DONE);	/* httpd sends the reply on a poll */
		}
	} else {
		print_cmd_prompt();
		con_line_ready = 0;		/* con reads on: type-ahead echoes after the prompt */
	}
	STEP_NEXT(S_CLI_IDLE);
}

/* net: uIP's periodic calls, every tick; and on EV_UPLOAD_DONE a poll of
 * the connection at once, which sends an upload's answer, or a window update
 * if its window was short (net.md item 1) */
static __xdata uint16_t net_due;

void net_start(void) __banked
{
	net_due = sched_now;
	STEP_NEXT(S_NET_TICK);
}

void net_task(void) __banked
{
	if (sched_ev & EV_UPLOAD_DONE) {
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
	task_mask(EV_UPLOAD_DONE);
	task_until(net_due);
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
