/*
 * om_fail.c - om_fail(), in the common area. A handler's message is __code
 * data in the handler's own bank, which a banked om_fail() can't read: the
 * bank window would show the engine's bank. Here it runs with the caller's
 * bank mapped, as the engine's banked calls return to it.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "om.h"

void om_fail(__code const char *why)
{
	om_err_head();
	print_string(why);
	om_err_end();
}
