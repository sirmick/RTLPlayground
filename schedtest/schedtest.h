/*
 * schedtest.h - the scheduler test image (doc/sched/testing.md, layer 2).
 *
 * The real firmware is linked (rtlplayground.c holds the ISRs and the
 * print helpers), but SCHEDTEST's main() skips the switch init and runs the
 * scheduler over the dummy tasks here, one file pinned to each bank. Every
 * dummy logs into xdata under a fixed name, so the simulator's control
 * client can read it by symbol (schedtest/README.md).
 */
#ifndef SCHEDTEST_H
#define SCHEDTEST_H

#include <stdint.h>
#include "sched.h"

/* schedtest's own events, in the spare bits */
#define EV_ST_POKE	0x1000	/* poster -> waiter */
#define EV_ST_GO	0x2000	/* cli -> printer, jobber, slow: look at your request */

/* A timestamp log: the time of each of the last ST_LOG_N runs, as the tick
 * and the fine units into it (sched_fine_read), plus one value per entry;
 * and the shortest and longest gap between successive entries in fine
 * units (0xffff: 400 ms or more). Entry i lives at index i % ST_LOG_N. */
#define ST_LOG_N	64
struct st_log {
	uint16_t n;			/* entries logged (wraps at 65536) */
	uint16_t gap_min, gap_max;
	uint16_t tick[ST_LOG_N];
	uint16_t cnt[ST_LOG_N];
	uint16_t aux[ST_LOG_N];
};

extern __xdata struct st_log st_log_sleep_a, st_log_sleep_b, st_log_sleep_c;
extern __xdata struct st_log st_log_waiter;	/* aux: 1 woken by EV_ST_POKE, 2 timed out */
extern __xdata struct st_log st_log_poster;
extern __xdata struct st_log st_log_isr_ev;	/* aux: con_rx_head at the run */
extern __xdata struct st_log st_log_hot;

extern __xdata struct st_log * __xdata st_lp;	/* the log st_log() writes */
extern __xdata uint16_t st_aux;			/* its aux value */
void st_log(void) __banked;

/* requests from cli; each worker clears its own */
extern __xdata uint16_t st_print_req;	/* lines to print */
extern __xdata uint8_t  st_jobs_req;
extern __xdata uint8_t  st_slow_req;	/* 1 slow (25 ms), 2 hang */
extern __xdata uint8_t  st_hot_on;	/* hot runs (yielding) */

/* what the workers report */
extern __xdata uint16_t st_print_done;	/* lines printed by the last print */
extern __xdata uint16_t st_jobs_claimed, st_jobs_full, st_jobs_done;
extern __xdata uint8_t  st_fl_state;	/* 0 never, 1 running, 2 ok, 3 verify failed, 4 no slot */
extern __xdata uint16_t st_fl_bad;	/* bytes that read back wrong */
extern __xdata uint16_t st_slow_runs;
extern __xdata uint16_t st_con_rx_n, st_con_rx_sum;	/* console bytes taken, their sum */
extern __xdata uint8_t  st_sp_step_max;	/* highest SP seen at a step's entry (hot) */

/* The stack paint: at boot, idata from st_stack_paint_from to 0xff is
 * filled with 0xa5; sleep_c rescans it each second. st_stack_hw is the
 * highest idata address ever written since (0 = none), so the peak stack
 * use is st_stack_hw - 0x7c. */
#define ST_PAINT 0xa5
extern __xdata uint8_t  st_stack_paint_from, st_stack_hw;
void st_stack_paint(void) __banked;

/* deep: a call chain of st_deep_depth banked calls through all three banks,
 * every 20 ms while st_deep_depth != 0, spinning 5-10 ms at the bottom with
 * the console busy so the ISRs land on top of it. st_deep_sp: the highest
 * SP at the bottom. */
extern __xdata uint8_t  st_deep_depth, st_deep_left, st_deep_sp;
extern __xdata uint16_t st_deep_runs;
void deep_1(void) __banked;
void deep_2(void) __banked;
void deep_3(void) __banked;
void st_deep_bottom(void) __banked;

void schedtest_main(void) __banked;	/* from main(): never returns */
void st_flash_start(void) __banked;	/* claim the flasher job */

#endif
