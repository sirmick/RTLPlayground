/*
 * step_time.h - a step's time budget, the one way every step that bounds its
 * own work checks it (the encoder, the CRC, the flash writes, the cli's
 * web lines). Timer 2's fine clock counts 6.144 us units from the step's
 * start (sched_bank.c sched_step_fine()); a budget is written in us.
 * Standalone (no generated header), so om.c's host build has it too.
 */
#ifndef STEP_TIME_H
#define STEP_TIME_H

#include <stdint.h>

#define SCHED_FINE_NS		6144		/* the fine clock's unit */

uint16_t sched_step_fine(void) __banked;	/* the step so far, fine units */

#define STEP_US(us)	((uint16_t)((us) * 1000UL / SCHED_FINE_NS))	/* us in fine units */
#define STEP_PAST(us)	(sched_step_fine() >= STEP_US(us))		/* the step has run us */

#endif
