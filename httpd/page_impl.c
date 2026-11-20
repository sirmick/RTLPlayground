// #define REGDBG 1

#include "rtl837x_sfr.h"
#include "rtl837x_common.h"
#include "om_gen.h"
#include "rtl837x_regs.h"
#include "rtl837x_storm.h"
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
#include "rtl837x_rldp.h"
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

extern __xdata uint16_t lldp_logical_port_status;
extern __xdata bool lldp_enabled;


__code const char * __code const http_storm_names[STORM_TYPES] = { "bcast", "mcast", "ucast", "umcast" };

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


void send_mirror(void)
{
	dbg_string("send_mirror called\n");
	slen = strtox(outbuf, HTTP_RESPONSE_JSON);

	reg_read_m(RTL837x_MIRROR_CTRL);
	uint8_t mPort = sfr_data[3];
	if (mPort & 1) {
		str_code_to_html("{" JSONQUOTE("enabled") ":true," JSONQUOTE("mirrorPort") ":");
	} else {
		str_code_to_html("{" JSONQUOTE("enabled") ":false," JSONQUOTE("mirrorPort") ":");
	}
	itoa_html(machine.log_to_phys_port[mPort >> 1]);

	reg_read_m(RTL837x_MIRROR_CONF);
	uint16_t m = (sfr_data[0] << 8) | sfr_data[1];
	str_code_to_html("," JSONQUOTE("mirror_rx") ":[");
	for (uint8_t i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
		if((m >> i) & 1) {
			json_add_array_uint_item(machine.log_to_phys_port[i]);
		}
	}
	if ((m >> CPU_LOGICAL_PORT) & 1)
		json_add_array_uint_item(CPU_PHYSICAL_PORT);

	str_code_to_html("]," JSONQUOTE("mirror_tx") ":[");
	m = (sfr_data[2] << 8) | sfr_data[3];
	for (uint8_t i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
		if((m >> i ) & 1) {
			json_add_array_uint_item(machine.log_to_phys_port[i]);
		}
	}
	if ((m >> CPU_LOGICAL_PORT) & 1)
		json_add_array_uint_item(CPU_PHYSICAL_PORT);

	char_to_html(']');
	char_to_html('}');
}


void send_lldp(void)
{
	dbg_string("send_lldp called\n");
	slen = strtox(outbuf, HTTP_RESPONSE_JSON);

	str_code_to_html("{" JSONQUOTE("on") ":");
	bool_to_json(lldp_enabled);
	str_code_to_html("," JSONQUOTE("portStatus") ":[");

	for (uint8_t i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
		str_code_to_html("{" JSONQUOTE("port") ":");
		itoa_html(machine.log_to_phys_port[i]);

		str_code_to_html("," JSONQUOTE("name") ":\"");
		for (uint8_t j = 0; j < PORT_NAME_SIZE && cfg_run.port[i].name[j]; j++) {
			json_char_to_html(cfg_run.port[i].name[j]);
		}

		str_code_to_html("\"," JSONQUOTE("permitted") ":");
		bool_to_json(1 & (lldp_logical_port_status >> i));
		char_to_html('}');

		if (i < MACHINE_MAX_PORT)
			char_to_html(',');
	}
	char_to_html(']');
	char_to_html('}');
}

static void port_list_to_json(__xdata uint16_t mask)
{
	__xdata uint8_t first = 1;

	char_to_html('[');
	for (uint8_t i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
		if (!(mask & (1 << i)))
			continue;
		if (!first)
			char_to_html(',');
		first = 0;
		itoa_html(machine.log_to_phys_port[i]);
	}
	char_to_html(']');
}

/* Loop detection: {"on":..,"stp":..,"looped":[ports],"blocked":[ports]} */
void send_loop(void)
{
	slen = strtox(outbuf, HTTP_RESPONSE_JSON);
	str_code_to_html("{" JSONQUOTE("on") ":");
	bool_to_json(rldp_enabled);
	str_code_to_html("," JSONQUOTE("stp") ":");
	bool_to_json(stp_enabled);
	str_code_to_html("," JSONQUOTE("looped") ":");
	port_list_to_json(rldp_enabled ? rldp_looped : 0);
	str_code_to_html("," JSONQUOTE("blocked") ":");
	port_list_to_json(rldp_enabled ? rldp_blocked : 0);
	char_to_html('}');
}


void send_bandwidth(void)
{
	dbg_string("send_bandwidth called\n");
	slen = strtox(outbuf, HTTP_RESPONSE_JSON);
	char_to_html('[');
	for (uint8_t i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
		str_code_to_html("{" JSONQUOTE("port") ":");
		itoa_html(machine.log_to_phys_port[i]);
		str_code_to_html("," JSONQUOTE("iLimited") ":");
		reg_read_m(RTL837X_IGBW_PORT_CTRL + i * 4);
		bool_to_json(sfr_data[1] & 0x10);
		str_code_to_html("," JSONQUOTE("iBW") ":\"0x");
		byte_to_html(sfr_data[1] & 0x0f);
		byte_to_html(sfr_data[2]);
		byte_to_html(sfr_data[3]);
		str_code_to_html("\"," JSONQUOTE("iAction") ":");
		if(reg_bit_test(RTL837X_IGBW_PORT_FC_CTRL, i)){
			str_code_to_html(JSONQUOTE("fc"));
		} else {
			str_code_to_html(JSONQUOTE("drop"));
		}
		reg_read_m(RTL837X_EGBW_PORT_CTRL + i * 1024);
		str_code_to_html("," JSONQUOTE("eLimited") ":");
		bool_to_json(sfr_data[1] & 0x10);
		str_code_to_html("," JSONQUOTE("eBW") ":\"0x");
		byte_to_html(sfr_data[1] & 0x0f);
		byte_to_html(sfr_data[2]);
		byte_to_html(sfr_data[3]);
		char_to_html('"');
		char_to_html('}');
		if (i < MACHINE_MAX_PORT)
			char_to_html(',');
	}
	char_to_html(']');
}



void send_storm(void)
{
	__xdata uint8_t i, t, idx;

	slen = strtox(outbuf, HTTP_RESPONSE_JSON);
	char_to_html('[');
	for (i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
		str_code_to_html("{" JSONQUOTE("port") ":");
		itoa_html(machine.log_to_phys_port[i]);
		char_to_html(',');

		for (t = 0; t < STORM_TYPES; t++) {
			char_to_html('"');
			str_code_to_html(http_storm_names[t]);
			str_code_to_html("\":{" JSONQUOTE("enabled") ":");
			bool_to_json(reg_bit_test(RTL837X_STORM_CTRL + (t << 2), i));

			str_code_to_html("," JSONQUOTE("unit") ":");
			idx = STORM_METER(i, t);

			if (reg_bit_test(RTL837X_METER_MODE + ((idx >> 5) << 2), idx & 0x1f)) {
				str_code_to_html(JSONQUOTE("pps"));
			} else {
				str_code_to_html(JSONQUOTE("kbit/s"));
			}

			str_code_to_html("," JSONQUOTE("rate") ":\"0x");

			reg_read_m(RTL837X_METER_RATE + (STORM_METER(i, t) << 2));
			byte_to_html(sfr_data[1]);
			byte_to_html(sfr_data[2]);
			byte_to_html(sfr_data[3]);

			char_to_html('"');

			char_to_html('}');
			if(t < STORM_TYPES-1)
				char_to_html(',');
		}
		char_to_html('}');
		if (i < MACHINE_MAX_PORT)
			char_to_html(',');
	}
	char_to_html(']');
}

