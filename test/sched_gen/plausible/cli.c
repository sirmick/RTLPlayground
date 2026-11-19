#include "cli.h"

uint8_t cli_idle(void) __banked
{
	return S_CLI_RUN;
}

uint8_t cli_run(void) __banked
{
	task_claim(S_FL_ERASE);
	if (l2)
		return S_L2_FIRST;
	return S_CLI_PROMPT;
}

uint8_t cli_prompt(void) __banked
{
	return S_CLI_IDLE;
}

uint8_t l2_first(void) __banked
{
	return S_L2_MORE;
}

uint8_t l2_more(void) __banked
{
	return S_CLI_PROMPT;
}
