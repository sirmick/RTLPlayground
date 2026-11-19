#include "stp.h"

uint8_t stp_step(void) __banked
{
	return S_STP_SECOND;
}

uint8_t stp_second(void) __banked
{
	return S_STP_STEP;
}
