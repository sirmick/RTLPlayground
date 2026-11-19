/* Swept, never compiled, for ST_NEG=bad_spec: no step names BAD_LOST and it
 * is no task's first, so the generator fails the build. */
#include "sched.h"

TASK(bad, fixed, once)
STEP(bad, step)
{
}

STEP(bad, lost)
{
}
