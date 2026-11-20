/*
 * crash.c - the software watchdog's state and the crash record; see crash.h.
 * Resident: crash_die() runs from the tick ISR's context.
 */
#include <8051.h>
#include <stdint.h>
#include <stddef.h>
#include "rtl837x_sfr.h"
#include "sched.h"
#include "crash.h"
#include "build_id.h"

#ifndef CRASH_RESET
#define CRASH_RESET()	CHIP_RESET_NOW()
#endif
#ifndef CRASH_IDATA
#define CRASH_IDATA(a)	(*(__idata uint8_t *)(a))
#endif

/* The layout for CRASH_MAGIC 0x5c4d: change one, change both (crash.h).
 * Negative arrays, as SDCC's _Static_assert only warns. */
#define CRASH_AT(f, o)	(offsetof(struct crash_rec, f) == (o))
typedef char crash_layout_5c4d[CRASH_MAGIC == 0x5c4d && sizeof(struct crash_rec) == 32 &&
	CRASH_AT(cause, 2) && CRASH_AT(resets, 3) && CRASH_AT(tick, 4) && CRASH_AT(slot, 6) &&
	CRASH_AT(state, 7) && CRASH_AT(step_tick, 8) && CRASH_AT(sp, 10) && CRASH_AT(bank, 11) &&
	CRASH_AT(pc, 12) && CRASH_AT(ring, 14) && CRASH_AT(ring_idx, 22) && CRASH_AT(image_lo, 24) &&
	CRASH_AT(image_hi, 26) && CRASH_AT(sum, 30) ? 1 : -1];

__xdata __at(CRASH_ADDR) struct crash_rec crash_rec;
__xdata struct crash_rec crash_last;
__xdata uint8_t crash_prior;
__xdata uint8_t crash_cause;
__xdata uint8_t crash_pc_sp;
__xdata uint8_t crash_pending;
volatile __xdata uint16_t swdt_cnt;
__xdata uint8_t swdt_on;

static __xdata uint8_t ci;
static __xdata uint16_t csum;

uint16_t crash_sum(void)
{
	csum = 0;
	for (ci = 0; ci < 30; ci++)
		csum += ((__xdata uint8_t *)&crash_rec)[ci];
	return csum;
}

void crash_die(void)
{
	swdt_on = 0;
	crash_rec.magic = CRASH_MAGIC;
	crash_rec.cause = crash_cause;
	crash_rec.resets = crash_prior;
	crash_rec.tick = sched_ticks();
	crash_rec.bank = PSBANK;
	crash_rec.image_lo = CRASH_IMAGE_LO;
	crash_rec.image_hi = CRASH_IMAGE_HI;
	if (crash_pc_sp) {
		crash_rec.sp = crash_pc_sp - 2;
		crash_rec.pc = (CRASH_IDATA(crash_pc_sp) << 8) | CRASH_IDATA(crash_pc_sp - 1);
	} else {
		crash_rec.sp = SP;
		crash_rec.pc = 0;
	}
	crash_rec.sum = crash_sum();
	CRASH_RESET();
}

void crash_reset(uint8_t cause)
{
	crash_cause = cause;
	crash_pc_sp = 0;
	crash_die();
}
