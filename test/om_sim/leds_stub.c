/* OM_SIM=1 (om.mk): the measurement image, without leds_dump's 1.7 KB to make room */
#include "rtl837x_common.h"
#include "rtl837x_leds.h"

void leds_dump(void) __banked
{
	print_string("leds: not in this image\n");
}
