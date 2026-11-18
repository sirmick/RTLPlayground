/*
 * st_main.c - schedtest's boot, the console editor (con), the command task
 * (cli) and the timestamp log. Banked; place_banks.py picks the bank.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "version.h"
#include "sched.h"
#include "con_ring.h"
#include "crash.h"
#include "schedtest/schedtest.h"

extern __xdata uint8_t serial_reset_ok;

__xdata struct st_log st_log_sleep_a, st_log_sleep_b, st_log_sleep_c;
__xdata struct st_log st_log_waiter, st_log_poster, st_log_isr_ev, st_log_hot;
__xdata struct st_log * __xdata st_lp;
__xdata uint16_t st_aux;

__xdata uint16_t st_print_req;
__xdata uint8_t  st_jobs_req;
__xdata uint8_t  st_slow_req;
__xdata uint16_t st_con_rx_n, st_con_rx_sum;

/* ---- the log ---- */

static __xdata uint8_t li, lp;
static __xdata uint16_t ldt, lg;

void st_log(void) __banked
{
	sched_fine_read();
	li = st_lp->n & (ST_LOG_N - 1);
	if (st_lp->n) {
		lp = (st_lp->n - 1) & (ST_LOG_N - 1);
		ldt = sched_f_tick - st_lp->tick[lp];
		if (ldt >= 80) {
			lg = 0xffff;
		} else {
			lg = sched_f_cnt - st_lp->cnt[lp];
			while (ldt--)
				lg += sched_fine_per_tick;
		}
		if (st_lp->n == 1 || lg < st_lp->gap_min)
			st_lp->gap_min = lg;
		if (st_lp->n == 1 || lg > st_lp->gap_max)
			st_lp->gap_max = lg;
	}
	st_lp->tick[li] = sched_f_tick;
	st_lp->cnt[li] = sched_f_cnt;
	st_lp->aux[li] = st_aux;
	st_lp->n++;
}

/* ---- con: the line editor ---- */

#define ST_LINE 64
static __xdata char ed[ST_LINE];
static __xdata uint8_t ed_len, ed_last, c;
static __xdata char st_line[ST_LINE];
static __xdata uint8_t st_line_ready;

void con_edit(void) __banked
{
	while (con_rx_ready()) {
		if (con_tx_room() < 4) {	/* the echo must not wait */
			task_wait(EV_CON_TX, SCHED_FOREVER);
			return;
		}
		c = con_getc();
		st_con_rx_n++;
		st_con_rx_sum += c;
		if (c == '\r' || c == '\n') {
			if (c == '\n' && ed_last == '\r') {
				ed_last = c;
				continue;
			}
			ed_last = c;
			write_char('\n');
			if (!st_line_ready) {
				for (li = 0; li < ed_len; li++)
					st_line[li] = ed[li];
				st_line[ed_len] = 0;
				st_line_ready = 1;
				event_post(EV_LINE);
			}
			ed_len = 0;
			continue;
		}
		ed_last = c;
		if (c == 8 || c == 127) {
			if (ed_len) {
				ed_len--;
				print_string("\b \b");
			}
		} else if (c >= ' ' && c < 127 && ed_len < ST_LINE - 1) {
			ed[ed_len++] = c;
			write_char(c);
		}
	}
	task_wait(EV_CON_RX, SCHED_FOREVER);
}

/* ---- cli ---- */

static __xdata uint8_t ap;	/* parse position in st_line */

/* st_line starts with w (then a space or the end): step past it */
static uint8_t word(__code const char *w)
{
	ap = 0;
	while (*w) {
		if (st_line[ap] != *w)
			return 0;
		ap++;
		w++;
	}
	if (st_line[ap] && st_line[ap] != ' ')
		return 0;
	while (st_line[ap] == ' ')
		ap++;
	return 1;
}

static __xdata uint16_t num;
static uint8_t number(void)	/* a decimal at ap into num */
{
	num = 0;
	if (st_line[ap] < '0' || st_line[ap] > '9')
		return 0;
	while (st_line[ap] >= '0' && st_line[ap] <= '9')
		num = num * 10 + (st_line[ap++] - '0');
	return 1;
}

void cli_idle(void) __banked
{
	if (st_line_ready)
		task_next(S_CLI_RUN);
	else
		task_wait(EV_LINE, SCHED_FOREVER);
}

static void status(void)
{
	print_string("print ");
	itoa_short(st_print_done);
	print_string(" jobs claimed ");
	itoa_short(st_jobs_claimed);
	print_string(" full ");
	itoa_short(st_jobs_full);
	print_string(" done ");
	itoa_short(st_jobs_done);
	print_string(" flash ");
	itoa(st_fl_state);
	print_string(" bad ");
	itoa_short(st_fl_bad);
	print_string(" slow ");
	itoa_short(st_slow_runs);
	print_string("\ncon rx ");
	itoa_short(st_con_rx_n);
	print_string(" sum ");
	print_short(st_con_rx_sum);
	print_string(" overruns ");
	itoa_short(con_rx_overruns);
	print_string(" step SP max ");
	print_byte(st_sp_step_max);
	print_string("\nsleep_a/b/c runs ");
	itoa_short(st_log_sleep_a.n);
	write_char(' ');
	itoa_short(st_log_sleep_b.n);
	write_char(' ');
	itoa_short(st_log_sleep_c.n);
	print_string(" waiter ");
	itoa_short(st_log_waiter.n);
	print_string(" poster ");
	itoa_short(st_log_poster.n);
	print_string(" isr_ev ");
	itoa_short(st_log_isr_ev.n);
	print_string(" hot ");
	itoa_short(st_log_hot.n);
	write_char('\n');
}

void cli_run(void) __banked
{
	task_next(S_CLI_PROMPT);
	if (!st_line[0])
		return;
	if (word("health")) {
		if (word("health clear")) {
			sched_health_clear();
			return;
		}
		sched_health_tasks();
	} else if (word("status")) {
		status();
	} else if (word("print")) {
		st_print_req = number() ? num : 3000;
		event_post(EV_ST_GO);
	} else if (word("jobs")) {
		st_jobs_req = 1;
		event_post(EV_ST_GO);
	} else if (word("slow")) {
		st_slow_req = 1;
		event_post(EV_ST_GO);
	} else if (word("hang")) {
#ifdef ST_NEG_HANG
		st_slow_req = 2;
		event_post(EV_ST_GO);
#else
		print_string("hang: build with ST_NEG=hang\n");
#endif
	} else if (word("flash")) {
		st_flash_start();
	} else if (word("badflash")) {
#ifdef ST_NEG_BAD_FLASH
		st_flash_start();
#else
		print_string("badflash: build with ST_NEG=bad_flash\n");
#endif
	} else if (word("reset")) {
		crash_reset(CRASH_RESET_CMD);
	} else {
		print_string("health [clear] | status | print [N] | jobs | slow | hang | flash | badflash | reset\n");
	}
}

void cli_prompt(void) __banked
{
	print_string("> ");
	st_line_ready = 0;
	task_next(S_CLI_IDLE);
}

/* ---- boot ---- */

void schedtest_main(void) __banked
{
	print_string("\nschedtest " VERSION_SW "\n");
	crash_boot();		/* interrupts are on: a long line drains as it goes */
	sched_init();
	serial_reset_ok = 1;
	print_string("> ");
	sched_run();
}
