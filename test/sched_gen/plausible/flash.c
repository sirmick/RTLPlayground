#include "flash.h"

uint8_t fl_erase(void) __banked
{
	return S_FL_PROG;
}

uint8_t fl_prog(void) __banked
{
	return S_FL_VERIFY;
}

uint8_t fl_verify(void) __banked
{
	return S_FL_DONE;
}

uint8_t fl_done(void) __banked
{
	return STEP_END;
}
