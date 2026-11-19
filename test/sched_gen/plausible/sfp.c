#include "sfp.h"

uint8_t sfp_poll(void) __banked
{
	return S_SFP_WAKE;
}

uint8_t sfp_wake(void) __banked
{
	return S_SFP_READ;
}

uint8_t sfp_read(void) __banked
{
	return S_SFP_CONFIG;
}

uint8_t sfp_config(void) __banked
{
	return S_SFP_DDM;
}

uint8_t sfp_ddm(void) __banked
{
	return S_SFP_POLL;
}
