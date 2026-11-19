/*
 * st_b2.c - schedtest dummies in BANK2 (pinned by schedtest.mk):
 * sleep_b (37 ms), poster, jobber with its jobs, the flasher job.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "layout.h"
#include "rtl837x_flash.h"
#include "boot.h"
#include "loader/layout.h"
#include "sched.h"
#include "schedtest/schedtest.h"

extern __xdata struct flash_region_t flash_region;

__xdata uint16_t st_jobs_claimed, st_jobs_full, st_jobs_done;
__xdata uint8_t  st_fl_state;
__xdata uint16_t st_fl_bad;

void deep_2(void) __banked
{
	if (--st_deep_left)
		deep_3();
	else
		st_deep_bottom();
}

TASK(sleep_b, fixed, once)
STEP(sleep_b, step)
{
	st_lp = &st_log_sleep_b;
	st_aux = 0;
	st_log();
	return STEP_SLEEP(S_SLEEP_B_STEP, 37);
}

/* Posts EV_ST_POKE every 300 ms: the waiter sees it, or times out. A beat
 * that doesn't drift: the low-level task_until, from the last due tick. */
static __xdata uint16_t po_due;
static __xdata uint8_t po_n;
TASK(poster, fixed, once)
STEP(poster, step)
{
	event_post(EV_ST_POKE);
	st_lp = &st_log_poster;
	st_aux = 0;
	st_log();
	if ((int16_t)(sched_now - po_due) > 60)	/* first run, or fell behind */
		po_due = sched_now;
	po_due += 60;				/* 300 ms */
	if (po_n < LDR_CONFIRM_S * 10 / 3 && ++po_n == LDR_CONFIRM_S * 10 / 3)
		loader_confirm();		/* booted: as the firmware's loop does */
	task_until(po_due);
	return STEP_STAY;
}

/* ---- jobber: on request, claim jobs until the table is full ---- */

#define JB_STEPS 5

TASK(jobber, fixed, once)
STEP(jobber, step)
{
	if (!st_jobs_req)
		return STEP_WAIT(S_JOBBER_STEP, EV_ST_GO, SCHED_FOREVER);
	st_jobs_req = 0;
	while (task_claim_arg(S_JB_RUN, JB_STEPS) != SCHED_NONE)	/* arg: its steps */
		st_jobs_claimed++;
	st_jobs_full++;
	return STEP_STAY;
}

TASK(jb, job, once)
STEP(jb, run)
{
	if (--task_arg())
		return STEP_STAY;	/* yields: runs again at its next position */
	st_jobs_done++;
	return STEP_END;
}

/* ---- the flasher job: erase, program and verify a sector of the upload
 * area. Each step waits for its flash operation in the common area
 * (rtl837x_flash.c), so no banked code runs while the flash is busy. ---- */

#define FL_PAGES 16				/* 16 x 256 B = the 4 KiB sector */
#define FL_CHUNK 128				/* bytes programmed per step: flash_write_bytes
						 * issues one 4-byte program per ~70 us (rtlsim
						 * 224393b), so ~2.3 ms a step */
#define FL_HI	 (UPLOAD_START >> 16)		/* the sector at UPLOAD_START */
static __xdata uint8_t fl_page, fl_off, fi;
static __xdata uint8_t fl_buf[256];

#define FL_SET_ADDR() do { \
	((__xdata uint16_t *)&flash_region.addr)[0] = ((uint16_t)fl_page << 8) | fl_off; \
	((__xdata uint16_t *)&flash_region.addr)[1] = FL_HI; \
} while (0)

void st_flash_start(void) __banked
{
	if (task_claim(S_FL_ERASE) == SCHED_NONE) {
		st_fl_state = 4;
		print_string("flash: no free slot\n");
		return;
	}
	st_fl_state = 1;
	st_fl_bad = 0;
}

TASK(fl, job, once, first=erase)
STEP(fl, erase)
{
	fl_page = 0;
	fl_off = 0;
	FL_SET_ADDR();
#ifdef ST_NEG_BAD_FLASH
	flash_sector_erase_nowait();	/* returns with the erase running: the sim's checker must object */
#else
	flash_sector_erase();
#endif
	return S_FL_PROG;
}

STEP(fl, prog)
{
	for (fi = 0; fi < FL_CHUNK; fi++)
		fl_buf[fi] = fl_page ^ (fl_off + fi);
	FL_SET_ADDR();
	flash_region.len = FL_CHUNK;
	flash_write_bytes(fl_buf);
	fl_off += FL_CHUNK;
	if (fl_off)
		return STEP_STAY;
	if (++fl_page == FL_PAGES) {
		fl_page = 0;
		return S_FL_VERIFY;
	}
	return STEP_STAY;
}

STEP(fl, verify)
{
	FL_SET_ADDR();
	flash_region.len = 256;
	flash_read_bulk(fl_buf);
	for (fi = 0; ; fi++) {
		if (fl_buf[fi] != (uint8_t)(fl_page ^ fi))
			st_fl_bad++;
		if (fi == 255)
			break;
	}
	if (++fl_page == FL_PAGES)
		return S_FL_DONE;
	return STEP_STAY;
}

STEP(fl, done)
{
	st_fl_state = st_fl_bad ? 3 : 2;
	print_string(st_fl_bad ? "flash: verify FAILED\n" : "flash: ok\n");
	return STEP_END;
}
