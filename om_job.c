/*
 * om_job.c - the object model's cfg job (DESIGN.md 5.2-5.3): apply, an
 * instance a step (om.c's om_apply_next()); apply N's confirm window, a
 * second a step; the rollback when it ends unconfirmed or a handler fails.
 * The cli waits for the walk (om.c apply_wait()), not for the window.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "cmd_parser.h"
#include "sched.h"
#ifndef SCHED_SWEEP	/* the sweep runs before om_gen.h (it takes the scheduler's rows) */
#include "om_gen.h"
#endif

static __xdata uint8_t cfg_slot, cfg_r, cfg_w;
#define CFG_GET_WAIT	50	/* x 100 ms: a rollback waits for a get this long, at most */

uint8_t om_job_start(void) __banked
{
	cfg_w = 0;
	cfg_slot = task_claim(S_CFG_APPLY);
	if (cfg_slot == SCHED_NONE)
		return 0;
	return 1;
}

void om_job_wake(void) __banked
{
	task_wake(cfg_slot);
}

TASK(cfg, job, once, first=apply)
STEP(cfg, apply)
{
	cfg_r = om_apply_next();
	if (cfg_r == OM_A_MORE)
		return STEP_STAY;		/* exit: the next instance at the next position */
	if (cfg_r == OM_A_FAIL) {
		om_undo();
		return S_CFG_UNDO;		/* exit: a handler failed: put running back */
	}
	if (!om_confirm_s) {
		om_settle(cfg_r);
		return STEP_END;		/* exit: applied, no window */
	}
	om_apply_st = E_CFGAPPLY_CONFIRM;
	return STEP_SLEEP(S_CFG_WINDOW, 1000);	/* exit: the window, a second at a time */
}

STEP(cfg, window)
{
	if (om_confirmed) {
		om_settle(OM_A_DONE);
		return STEP_END;		/* exit: confirmed */
	}
	if (om_confirm_s && --om_confirm_s)	/* a second gone: the window ends at N s */
		return STEP_SLEEP(STEP_STAY, 1000);	/* exit: the next second, or confirm's wake */
	if (cli_job == CLI_JOB_OM) {	/* a get or diff is streaming: it uses the engine */
		if (++cfg_w <= CFG_GET_WAIT)
			return STEP_SLEEP(STEP_STAY, 100);	/* exit: roll back when it ends */
		om_cut = 1;		/* stalled (a slow web client): its next line ends it */
	}
	om_undo();
	return S_CFG_UNDO;		/* exit: the window ended unconfirmed */
}

STEP(cfg, undo)
{
	cfg_r = om_apply_next();
	if (cfg_r == OM_A_MORE)
		return STEP_STAY;		/* exit: the next instance at the next position */
	om_settle(cfg_r);
	return STEP_END;			/* exit: rolled back (or failed to) */
}
