/*
 * st_b2.c - schedtest dummies in BANK2 (pinned by schedtest.mk):
 * sleep_b (37 ms), poster, jobber with its jobs, the flasher job.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "layout.h"
#include "rtl837x_flash.h"
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

void sleep_b(void) __banked
{
	st_lp = &st_log_sleep_b;
	st_aux = 0;
	st_log();
	task_sleep(37);
}

/* Posts EV_ST_POKE every 300 ms: the waiter sees it, or times out */
void poster(void) __banked
{
	event_post(EV_ST_POKE);
	st_lp = &st_log_poster;
	st_aux = 0;
	st_log();
	task_sleep(300);
}

/* ---- jobber: on request, claim jobs until the table is full ---- */

#define JB_STEPS 5
static __xdata uint8_t jb_left[SCHED_SLOTS];	/* steps left, per slot */
static __xdata uint8_t js;

void jobber(void) __banked
{
	if (!st_jobs_req) {
		task_wait(EV_ST_GO, SCHED_FOREVER);
		return;
	}
	st_jobs_req = 0;
	for (;;) {
		js = task_claim(S_JB_RUN);
		if (js == SCHED_NONE) {
			st_jobs_full++;
			break;
		}
		jb_left[js] = JB_STEPS;
		st_jobs_claimed++;
	}
}

void jb_run(void) __banked
{
	if (--jb_left[sched_cur])
		return;			/* yields: runs again at its next position */
	st_jobs_done++;
	task_end();
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

void fl_erase(void) __banked
{
	fl_page = 0;
	fl_off = 0;
	FL_SET_ADDR();
#ifdef ST_NEG_BAD_FLASH
	flash_sector_erase_nowait();	/* returns with the erase running: the sim's checker must object */
#else
	flash_sector_erase();
#endif
	task_next(S_FL_PROG);
}

void fl_prog(void) __banked
{
	for (fi = 0; fi < FL_CHUNK; fi++)
		fl_buf[fi] = fl_page ^ (fl_off + fi);
	FL_SET_ADDR();
	flash_region.len = FL_CHUNK;
	flash_write_bytes(fl_buf);
	fl_off += FL_CHUNK;
	if (fl_off)
		return;
	if (++fl_page == FL_PAGES) {
		fl_page = 0;
		task_next(S_FL_VERIFY);
	}
}

void fl_verify(void) __banked
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
		task_next(S_FL_DONE);
}

void fl_done(void) __banked
{
	st_fl_state = st_fl_bad ? 3 : 2;
	print_string(st_fl_bad ? "flash: verify FAILED\n" : "flash: ok\n");
	task_end();
}
