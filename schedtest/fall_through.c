/* For ST_NEG=fall_through: a step that can fall off its end. SDCC warns
 * (59, "must return value"); for a step source that is an error (sched.mk),
 * so the build fails. */
#include <stdint.h>
#include "sched.h"

extern __xdata uint8_t st_slow_req;

TASK(ft, fixed, once)
STEP(ft, step)
{
	if (st_slow_req)
		return STEP_STAY;
}
