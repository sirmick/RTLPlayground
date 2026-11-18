#include "sfp.h"

void sfp_poll(void) __banked
{
	task_next(S_SFP_WAKE);
}

void sfp_wake(void) __banked
{
	task_next(S_SFP_READ);
}

void sfp_read(void) __banked
{
	task_next(S_SFP_CONFIG);
}

void sfp_config(void) __banked
{
	task_next(S_SFP_DDM);
}

void sfp_ddm(void) __banked
{
	task_next(S_SFP_POLL);
}
