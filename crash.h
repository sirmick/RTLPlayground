/*
 * crash.h - the software watchdog and the crash record (doc/sched/watchdog.md).
 *
 * The record sits at a fixed xdata address above every XSEG (app and loader),
 * which the C start-up does not clear, so it survives a chip reset. The
 * scheduler keeps the slot, state, step start and a ring of the last states
 * current before each step (CRASH_NOTE); crash_die() fills in the rest and
 * the sum, and resets the chip. The next boot prints it once (crash_boot),
 * if this image wrote it: the record carries the build's identity
 * (CRASH_IMAGE_*, build_id.h, generated per build by sched.mk).
 *
 * From an ISR (the watchdog in isr_timer2, ^R ^R in isr_serial) the path is a
 * jump, not a call: a call anywhere in an ISR makes SDCC save every register
 * on every entry. `pushes` is the number of bytes that ISR's prologue pushes,
 * which puts the interrupted PC at SP - pushes - 1 (low) and SP - pushes
 * (high); tools/isr_frame.py reads it from the ISR's .asm.
 */
#ifndef CRASH_H
#define CRASH_H

#include <stdint.h>

#ifndef CRASH_ADDR			/* schedtest ST_CRASH=8000: -DCRASH_ADDR=0x8000 */
#define CRASH_ADDR	0xbfe0		/* last 32 bytes of the 48 KiB xdata */
#endif
#define CRASH_MAGIC	0x5c4d

#define CRASH_SWDT	1		/* software watchdog */
#define CRASH_CTRL_R	2		/* ^R ^R on the console */
#define CRASH_RESET_CMD	3		/* the reset command */
/*			4		   reserved, was an update: the image the update boots
					   isn't the one that wrote the record; the loader
					   reports an update (watchdog.md) */

#define SWDT_TICKS	1000		/* 5 s */

struct crash_rec {
	uint16_t magic;		/*  0 */
	uint8_t  cause;		/*  2 */
	uint8_t  resets;	/*  3: recorded resets before this one since power-on */
	uint16_t tick;		/*  4: tick at the reset */
	uint8_t  slot;		/*  6: running slot (sched_cur) */
	uint8_t  state;		/*  7: its state */
	uint16_t step_tick;	/*  8: tick when that step started */
	uint8_t  sp;		/* 10: SP of the interrupted code */
	uint8_t  bank;		/* 11: PSBANK */
	uint16_t pc;		/* 12: PC of the interrupted code, 0 if not from an ISR */
	uint8_t  ring[8];	/* 14: the last 8 states run */
	uint8_t  ring_idx;	/* 22: next ring entry */
	uint8_t  spare;		/* 23 */
	uint16_t image_lo;	/* 24: the image that wrote it (build_id.h) */
	uint16_t image_hi;	/* 26 */
	uint8_t  spare2[2];	/* 28 */
	uint16_t sum;		/* 30: 16-bit sum of bytes 0-29 */
};

extern __xdata __at(CRASH_ADDR) struct crash_rec crash_rec;
extern __xdata struct crash_rec crash_last;	/* the record found at boot, for health */
extern __xdata uint8_t  crash_prior;		/* recorded resets before this boot */
extern __xdata uint8_t  crash_cause;		/* set before crash_die() */
extern __xdata uint8_t  crash_pc_sp;		/* address of the PC's high byte, 0 = none */
extern volatile __xdata uint16_t swdt_cnt;	/* ticks left */
extern __xdata uint8_t  swdt_on;

/* Before each step: slot, state, start tick, ring. */
#define CRASH_NOTE(sl, st) do { \
	crash_rec.slot = (sl); \
	crash_rec.state = (st); \
	crash_rec.step_tick = sched_now; \
	crash_rec.ring[crash_rec.ring_idx & 7] = (st); \
	crash_rec.ring_idx = (crash_rec.ring_idx + 1) & 7; \
} while (0)

#define swdt_kick()	(swdt_cnt = SWDT_TICKS)

#ifndef CRASH_JUMP
#define CRASH_JUMP()	__asm__("ljmp _crash_die")
#endif

/* In an ISR: record and reset, never returns. */
#define CRASH_FROM_ISR(c, pushes) do { \
	crash_cause = (c); \
	crash_pc_sp = SP - (pushes); \
	CRASH_JUMP(); \
} while (0)

/* In isr_timer2, after the tick. */
#define SWDT_ISR(pushes) do { \
	if (swdt_on && !--swdt_cnt) \
		CRASH_FROM_ISR(CRASH_SWDT, pushes); \
} while (0)

void crash_die(void);			/* fill the record, reset; never returns */
void crash_reset(uint8_t cause);	/* from main code: record and reset */
uint16_t crash_sum(void);		/* of crash_rec's bytes 0-29 */
void crash_boot(void) __banked;		/* print and consume a valid record */

#endif
