#ifndef __PAGE_IMPL_H__
#define __PAGE_IMPL_H__

#include <stdbool.h>

#define JSONQUOTE(x) #x

extern __code const uint8_t * __code const HTTP_RESPONSE_TXT;
extern __code const uint8_t * __code const HTTP_RESPONSE_JSON;

void bool_to_html(char c);
void byte_to_html(uint8_t val);
void itoa_html(uint8_t v);

void str_code_to_html(__code const char *s);
void bool_to_json(char c);
void json_add_array_uint_item(uint8_t x);
void char_to_html(char c);

void itoa16_html(uint16_t v);

void send_bandwidth(void);
void send_storm(void);
void l2_delete(uint16_t idx);
void send_mirror(void);
void send_stp(void) __banked;
void send_lldp(void);
void send_loop(void);

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
