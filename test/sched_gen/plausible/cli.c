#include "cli.h"

void cli_idle(void) __banked
{
	task_next(S_CLI_RUN);
}

void cli_run(void) __banked
{
	task_next(S_L2_FIRST); task_next(S_CLI_PROMPT); task_claim(S_FL_ERASE);
}

void cli_prompt(void) __banked
{
	task_next(S_CLI_IDLE);
}

void l2_first(void) __banked
{
	task_next(S_L2_MORE);
}

void l2_more(void) __banked
{
	task_next(S_CLI_PROMPT);
}
