/*
 * flash_job.c - the flash job (doc/sched/flash.md): an upload
 * segment, the config save, the factory reset. Each step's flash operation
 * waits for the flash in the common area (rtl837x_flash.c) before the step
 * returns, so no banked code runs while the flash is busy. FL_ERASE is the
 * one step over 20 ms (a sector, ~40 ms).
 *
 * An upload segment starts in FL_PROG: httpd.c's upload_prog() parses the
 * staged segment into pages and programs them, asking for FL_ERASE at each
 * sector, and for FL_VERIFY (upload_verify(), the read-back CRC) when the
 * part has ended. A config save and a factory reset start in FL_ERASE
 * (CONFIG_START), then FL_PROG copies FL_CHUNK bytes a step; the loader's
 * boot count (loader_confirm()) is one FL_ERASE.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_flash.h"
#include "loader/layout.h"
#include "flash_job.h"
#include "sched.h"

#define FL_CHUNK	512	/* bytes a config/factory FL_PROG step programs: ~9 ms */

extern __xdata struct flash_region_t flash_region;
extern __xdata uint8_t flash_buf[FLASH_BUF_SIZE];

__xdata uint8_t fl_req, fl_kind, fl_finished;
__xdata uint8_t * __xdata fl_src;
__xdata uint16_t fl_len;
__xdata uint32_t fl_addr;
static __xdata uint32_t fl_from;	/* factory: the next default config byte */
static __xdata uint16_t fl_n;

/* a += fl_n on a 32-bit flash address, by 16-bit halves (memory.md rule 3:
 * a 32-bit add spills to internal RAM) */
#define FL_ADVANCE(a) do { \
	if ((((__xdata uint16_t *)&(a))[0] += fl_n) < fl_n) \
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
	if (fl_kind == FL_BOOTCNT) {
		fl_addr = LDR_BOOTCNT;
	} else if (fl_kind != FL_UPLOAD) {
		fl_addr = CONFIG_START;
		if (fl_kind != FL_CONFIG) {
			fl_from = DEFAULT_CONFIG_START;
			fl_len = CONFIG_LEN;
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
	fl_n = fl_len > FL_CHUNK ? FL_CHUNK : fl_len;
	if (fl_kind != FL_CONFIG) {
		flash_region.addr = fl_from;
		flash_region.len = fl_n;
		flash_read_bulk(flash_buf);
		FL_ADVANCE(fl_from);
		fl_src = flash_buf;
	}
	flash_region.addr = fl_addr;
	flash_region.len = fl_n;
	flash_write_bytes(fl_src);
	FL_ADVANCE(fl_addr);
	fl_src += fl_n;
	fl_len -= fl_n;
	if (!fl_len)
		return S_FL_DONE;
	return STEP_STAY;
}

STEP(fl, verify)
{
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
		if (fl_kind == FL_FACTORY_BTN)
			print_string("Now resetting...\n");
		reset_chip();
	}
	fl_kind = 0;
	fl_finished = 1;
	event_post(EV_UPLOAD_DONE);
	return STEP_END;
}
