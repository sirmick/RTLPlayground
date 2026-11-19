#ifndef _FLASH_JOB_H_
#define _FLASH_JOB_H_

#include <stdint.h>

/*
 * The flash job (doc/sched/flash.md, flash_job.c): upload segments, the config
 * save and the factory reset, written in steps. A requester sets fl_req (and
 * fl_src, fl_len for a config save), then flash_job_start(): 0 if the job is
 * busy or no slot is free. At its end the job sets fl_finished and posts
 * EV_UPLOAD_DONE; a factory reset resets the chip instead.
 * No sched.h here: httpd includes it, and the host tests have no sched_gen.h.
 */
#define FL_UPLOAD	1	/* httpd's staged upload segment (httpd.c upload_prog) */
#define FL_CONFIG	2	/* config save: fl_len bytes at fl_src to CONFIG_START */
#define FL_BOOTCNT	3	/* the loader's boot count erased (boot.c loader_confirm) */
#define FL_FACTORY	4	/* factory reset: the default config sector, then a reset */
#define FL_FACTORY_BTN	5	/* the same, from the reset button */

extern __xdata uint8_t fl_req;		/* the request flash_job_start() takes */
extern __xdata uint8_t fl_kind;		/* the job's; 0: free */
extern __xdata uint8_t fl_finished;	/* set when the job has ended */
extern __xdata uint8_t * __xdata fl_src;
extern __xdata uint16_t fl_len;
extern __xdata uint32_t fl_addr;	/* FL_ERASE's sector; config, factory: the next byte */

uint8_t flash_job_start(void) __banked;

/* httpd's side of an upload (httpd.c), one call per FL_PROG / FL_VERIFY step */
#define UP_PROG		0	/* FL_PROG again */
#define UP_ERASE	1	/* erase the sector at fl_addr, then FL_PROG */
#define UP_VERIFY	2	/* FL_VERIFY: read the part back */
#define UP_DONE		3	/* the segment is written, or the answer built */
uint8_t upload_prog(void) __banked;
uint8_t upload_verify(void) __banked;

#endif
