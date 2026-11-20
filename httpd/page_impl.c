// #define REGDBG 1

#include "rtl837x_sfr.h"
#include "rtl837x_common.h"
#include "om_gen.h"
#include "rtl837x_regs.h"
#include "rtl837x_port.h"
#include "rtl837x_flash.h"
#include "rtl837x_pins.h"
#include "sfp.h"
#include "uip.h"
#include "html_data.h"
#include <stdint.h>
#include "phy.h"
#include "machine.h"
#include "rtl837x_stp.h"
#include "rtl837x_leds.h"
#include "page_impl.h"
#include "syslog.h"
#include "dhcp.h"
#include "update_apply.h"


// #define DEBUG
#include "debug.h"



extern __code const struct machine machine;
extern __xdata uint8_t outbuf[TCP_OUTBUF_SIZE];
extern __xdata uint16_t slen;
extern __xdata uint16_t cont_len;
extern __xdata uint32_t cont_addr;
extern __code const uint8_t * __code const hex;

extern __xdata uint8_t sfr_data[4];
extern __xdata uint8_t sfp_pins_last;


extern __xdata struct flash_region_t flash_region;




// Convert uint8_t to ascii HEX char push on html-buffer.
void charhex_to_html(char c)
{
	outbuf[slen++] = itohex(c);
}

void json_char_to_html(uint8_t c)
{
	if (c < 0x20 || c > 0x7e || c == '"' || c == '\\')
		c = '.';
	outbuf[slen++] = c;
}

void itoa16_html(uint16_t v) /* up to five digits (65535) */
{
	uint8_t print_zeros = 0;
	uint8_t d;
	d = v / 10000;
	if (d) { char_to_html('0' + d); print_zeros = 1; }
	d = (v / 1000) % 10;
	if (d || print_zeros) { char_to_html('0' + d); print_zeros = 1; }
	d = (v / 100) % 10;
	if (d || print_zeros) { char_to_html('0' + d); print_zeros = 1; }
	d = (v / 10) % 10;
	if (d || print_zeros) char_to_html('0' + d);
	char_to_html('0' + (v % 10));
}

uint16_t stat_content(void)
{
	dbg_string("stat_content called\n");
	return 0;
}


uint16_t port_status(void)
{
	dbg_string("port_status called\n");
	return 0;
}

/* Converts sfr_data[] into raw hex string.
   Suppress leading zeros.
*/
void sfr_data_to_html(void)
{
 	uint8_t print_zeros = 0;
	uint8_t val = 0;

	for (uint8_t nibble = 0; nibble < 8; nibble++) {
	  	if (!(nibble & 1))
	        val = sfr_data[nibble>>1];
		// force the swap instruction, itohex() ignores the upper nibble.
		val = (val << 4) | (val >> 4);
		// when print_zeros is not zero, we know that a non-zero number has printed.
		// That have to print all the next numbers.
		print_zeros |= val;
		// only care about lower nibble, that is what is printed.
		print_zeros &= 0x0f;
		if (print_zeros)
			charhex_to_html(val);
	}
	if (print_zeros == 0) {
	    char_to_html('0');
	}
}


void l2_delete(uint16_t idx)
{
	slen = strtox(outbuf, HTTP_RESPONSE_JSON);
	dbg_string("L2 DELETE\n");
	dbg_short(idx);
	hw_poll(RTL837X_TBL_CTRL);
	str_code_to_html("{\"result\":");
	// First, search for the entry based on the index
	reg_read_m(RTL837x_TBL_DATA_0);
	sfr_data[1] &= 0xfc;
	sfr_data[2] |= (TBL_LUTREAD_NEXT_L2UC << 6);
	reg_write_m(RTL837x_TBL_DATA_0);

	REG_WRITE(RTL837X_TBL_CTRL, (idx >> 8) & 0xf, idx, TBL_L2_UNICAST, TBL_EXECUTE);
	hw_poll(RTL837X_TBL_CTRL);
	reg_read_m(RTL837x_L2_DATA_OUT_B);
	if (!(SFR_DATA_24 & 0x20)) {
		char_to_html('0');
	} else {
		__bit is_our_mac_addr = uip_ethaddr.addr[0] == SFR_DATA_8 && uip_ethaddr.addr[1] == SFR_DATA_0;
		sfr_data[0] &= 0x3f; // Clear SPA
		reg_write_m(RTL837x_TBL_DATA_IN_B);

		// Second half of MAC is copied
		reg_read_m(RTL837x_L2_DATA_OUT_A);
		if (is_our_mac_addr && uip_ethaddr.addr[2] == SFR_DATA_24 && uip_ethaddr.addr[3] == SFR_DATA_16
		    && uip_ethaddr.addr[4] == SFR_DATA_8 && uip_ethaddr.addr[5] == SFR_DATA_0) {
			// the switch's own entry keeps management reachable
			char_to_html('0');
		} else {
			reg_write_m(RTL837x_TBL_DATA_IN_A);

			reg_read_m(RTL837x_L2_DATA_OUT_C);
			sfr_data[3] &= 0xc0; // Clear age, auth and second part of ports
			sfr_data[1] &= 0xfe; // Clear nosalearn
			reg_write_m(RTL837x_TBL_DATA_IN_C);

			reg_read_m(RTL837x_TBL_DATA_0);
			sfr_data[2] = TBL_L2_UNICAST;
			reg_write_m(RTL837x_TBL_DATA_0);

			REG_WRITE(RTL837X_TBL_CTRL, idx >> 8, idx, TBL_L2_UNICAST, TBL_WRITE | TBL_EXECUTE);
			hw_poll(RTL837X_TBL_CTRL);

			char_to_html('1');
		}
	}
	char_to_html('}');
}




