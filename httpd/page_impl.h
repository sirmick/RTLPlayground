#ifndef __PAGE_IMPL_H__
#define __PAGE_IMPL_H__

#include <stdint.h>

extern __code const uint8_t * __code const HTTP_RESPONSE_TXT;	/* httpd.c */


/*  Convert only the lower nibble to ascii HEX char.
    For convenience the upper nibble is masked out.
*/
inline char itohex(uint8_t val) {
	// Ignore upper nibble for convenience.
	val &= 0x0f;
	val -= 10;

	// 10 or above
	if ((int8_t)val >= 0)
		val += ('a' - '0' - 10);

	return val + ('0' + 10);
}

#endif
