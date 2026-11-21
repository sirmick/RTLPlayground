/*
 * flash_job.c - the flash job (doc/sched/flash.md): an upload
 * segment, the config save, the factory reset. Each step's flash operation
 * waits for the flash in the common area (rtl837x_flash.c) before the step
 * returns, so no banked code runs while the flash is busy. FL_ERASE is the
 * one step of ~40 ms (a sector), inside the 50 ms step limit (decision 25).
 *
 * An upload segment starts in FL_PROG: httpd.c's upload_prog() parses the
 * staged segment into pages and programs them, asking for FL_ERASE at each
 * sector, and for FL_VERIFY (upload_verify(), the read-back CRC) when the
 * part has ended. The config save and a factory reset start in FL_ERASE
 * (CONFIG_START), then FL_PROG writes FL_CHUNK bytes a step: the save's
 * from om.c's encoder (om_save_chunk()), then FL_VERIFY reads it back
 * (om_save_end()); a factory reset's from the default config sector. The
 * loader's boot count (loader_confirm()) is one FL_ERASE.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_flash.h"
#include "loader/layout.h"
#include "flash_job.h"
#include "sched.h"
#include "crash.h"
#include "om.h"

#define FL_CHUNK	512	/* bytes a config/factory FL_PROG chunk holds */
/* a chunk is programmed an op at a time (to the next 4 B boundary) while
 * the step is under FL_W_FINE, the rest at the next step: an op is ~1 ms on
 * the 4+2's W25Q40CL (the bench, 2026-11-21: a save's FL_PROG step grew
 * ~210 us a byte), ~50 us on the 8x's W25Q16JV */
#define FL_W_FINE	((uint16_t)(7000000UL / SCHED_FINE_NS))

extern __xdata struct flash_region_t flash_region;
extern __xdata uint8_t flash_buf[FLASH_BUF_SIZE];

__xdata uint8_t fl_req, fl_kind, fl_finished, fl_poll;
__xdata uint16_t fl_len;
__xdata uint32_t fl_addr;
static __xdata uint32_t fl_from;	/* factory: the next default config byte */
static __xdata uint16_t fl_n;
static __xdata uint16_t fl_wn, fl_wp, fl_p;	/* the chunk's bytes still to program, from flash_buf + fl_wp */

/* a += n on a 32-bit flash address, by 16-bit halves (memory.md rule 3:
 * a 32-bit add spills to internal RAM) */
#define FL_ADVANCE(a, n) do { \
	if ((((__xdata uint16_t *)&(a))[0] += (n)) < (n)) \
		((__xdata uint16_t *)&(a))[1]++; \
} while (0)

uint8_t flash_job_start(void) __banked
{
	if (fl_kind || (fl_req != FL_UPLOAD && up_active))
		return 0;
	if (task_claim(fl_req == FL_UPLOAD ? S_FL_PROG : S_FL_ERASE) == SCHED_NONE)
		return 0;
	fl_kind = fl_req;
	fl_finished = 0;
	fl_wn = 0;
	if (fl_kind == FL_BOOTCNT) {
		fl_addr = LDR_BOOTCNT;
	} else if (fl_kind != FL_UPLOAD) {
		fl_addr = CONFIG_START;
		if (fl_kind >= FL_FACTORY) {	/* the config, not the help blob after it */
			fl_from = DEFAULT_CONFIG_START;
			fl_len = DEFAULT_CONFIG_LEN;
		}
	}
	return 1;
}

/* the sector at fl_addr */
TASK(fl, job, once, first=prog)
STEP(fl, erase)
{
	flash_region.addr = fl_addr;
	flash_sector_erase();
	if (fl_kind == FL_BOOTCNT)
		return S_FL_DONE;
	return S_FL_PROG;
}

STEP(fl, prog)
{
	if (fl_kind == FL_UPLOAD) {
		fl_n = upload_prog();
		if (fl_n == UP_ERASE)
			return S_FL_ERASE;
		if (fl_n == UP_VERIFY)
			return S_FL_VERIFY;
		if (fl_n == UP_DONE)
			return S_FL_DONE;
		return STEP_STAY;
	}
	if (!fl_wn) {		/* the next chunk */
		fl_n = fl_len > FL_CHUNK ? FL_CHUNK : fl_len;
		if (fl_kind == FL_SAVE) {
			fl_n = om_save_chunk();	/* as many: running doesn't change meanwhile */
		} else {
			flash_region.addr = fl_from;
			flash_region.len = fl_n;
			flash_read_bulk(flash_buf);
			FL_ADVANCE(fl_from, fl_n);
		}
		fl_len -= fl_n;		/* a save's step whose walk ran out of time first: none */
		fl_wn = fl_n;
		fl_wp = 0;
	}
	while (fl_wn) {		/* its ops while the step has time, one at least */
		fl_p = 4 - ((uint8_t)fl_addr & 3);
		if (fl_p > fl_wn)
			fl_p = fl_wn;
		flash_region.addr = fl_addr;
		flash_region.len = fl_p;
		flash_write_bytes(flash_buf + fl_wp);
		FL_ADVANCE(fl_addr, fl_p);
		fl_wp += fl_p;
		fl_wn -= fl_p;
		if (fl_wn && sched_step_fine() >= FL_W_FINE)
			return STEP_STAY;
	}
	if (fl_len)
		return STEP_STAY;
	if (fl_kind == FL_SAVE)
		return S_FL_VERIFY;
	return S_FL_DONE;
}

STEP(fl, verify)
{
	if (fl_kind == FL_SAVE) {
		if (om_save_end())
			return STEP_STAY;
		return S_FL_DONE;
	}
	if (upload_verify() == UP_DONE)
		return S_FL_DONE;
	return STEP_STAY;
}

STEP(fl, done)
{
	/* more of an upload came meanwhile, while this or another kind of job
	 * ran (upload_go() leaves a running job to take it): FL_PROG */
	if (fl_kind < FL_FACTORY && upload_done()) {
		fl_kind = FL_UPLOAD;
		return S_FL_PROG;
	}
	if (fl_kind >= FL_FACTORY) {
		print_string("Written default config to flash\n");
		if (fl_kind == FL_FACTORY_BTN) {
			print_string("Now resetting...\n");
			reset_chip(CRASH_BUTTON);
		}
		crash_pending = CRASH_FACTORY;	/* do sys.factory_reset: cli resets, after its reply */
		fl_kind = 0;
		return STEP_END;
	}
	fl_kind = 0;
	fl_finished = 1;
	fl_poll = 1;
	task_wake(T_NET);			/* the answer or a window update at once */
	return STEP_END;
}
