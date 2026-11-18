#include "stp.h"

void stp_step(void) __banked
{
	task_next(S_STP_SECOND);
}

void stp_second(void) __banked
{
	task_next(S_STP_STEP);
}
