#include "flash.h"

void fl_erase(void) __banked
{
	task_next(S_FL_PROG);
}

void fl_prog(void) __banked
{
	task_next(S_FL_VERIFY);
}

void fl_verify(void) __banked
{
	task_next(S_FL_DONE);
}

void fl_done(void) __banked
{
	task_end();
}
