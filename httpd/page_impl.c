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
#include "rtl837x_lacp.h"
#include "rtl837x_rldp.h"
#include "rtl837x_leds.h"
#include "page_impl.h"
#include "syslog.h"
#include "dhcp.h"
#include "update_apply.h"


// #define DEBUG
#include "debug.h"

#define L2_MAX_TRANSFER 30


extern __code const struct machine machine;
extern __xdata uint8_t outbuf[TCP_OUTBUF_SIZE];
extern __xdata uint16_t slen;
extern __xdata uint16_t cont_len;
extern __xdata uint32_t cont_addr;
extern __code const uint8_t * __code const hex;

extern __xdata uint8_t sfr_data[4];
extern __xdata uint8_t sfp_pins_last;
extern __xdata uint8_t vlan_names[VLAN_NAMES_SIZE];


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


void reg_to_html(uint16_t reg)
{
	reg_read_m(reg);
	sfr_data_to_html();
}


void reg_to_html_long(uint16_t reg)
{
	reg_read_m(reg);
	byte_to_html(sfr_data[0]);
	byte_to_html(sfr_data[1]);
	byte_to_html(sfr_data[2]);
	byte_to_html(sfr_data[3]);
}


/* The 64-bit counter of the last STAT_GET as one number: the low word keeps
 * its leading zeros whenever the high word is set. */
void counter_to_html(void)
{
	reg_read_m(RTL837X_STAT_V_HIGH);
	if (SFR_DATA_24 | SFR_DATA_16 | SFR_DATA_8 | SFR_DATA_0) {
		sfr_data_to_html();
		reg_to_html_long(RTL837X_STAT_V_LOW);
	} else {
		reg_to_html(RTL837X_STAT_V_LOW);
	}
}



void send_vlan(uint16_t vlan)
{
	slen = strtox(outbuf, HTTP_RESPONSE_JSON);
	dbg_string("sending VLAN\n");
	vlan_get(vlan);
	str_code_to_html("{" JSONQUOTE("untagged_members") ":[");

	// in xdata, as #305 did for the old send_vlan(): internal RAM is full
	__xdata uint32_t m = ((uint32_t)sfr_data[0] << 24) |
				((uint32_t)sfr_data[1] << 16) |
				((uint32_t)sfr_data[2] <<  8) |
				((uint32_t)sfr_data[3]);

	__xdata uint16_t members = (uint16_t)(m & 0x3FF);
	__xdata uint16_t untagged = (uint16_t)(((m >> 10) & 0x3FF) & members);

	for (uint8_t i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
		if(members & (1<<i)) {
			if((untagged & (1<<i))) {
				itoa_html(machine.log_to_phys_port[i]);
				json_char_to_html(',');
			}
		}
	}
	if(outbuf[slen - 1] == ',') {
		slen--;
	}

	str_code_to_html("]," JSONQUOTE("tagged_members") ":[");
	for (uint8_t i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
		if(members & (1<<i)) {
			if(!(untagged & (1<<i))) {
				itoa_html(machine.log_to_phys_port[i]);
				json_char_to_html(',');
			}
		}
	}
	if(outbuf[slen - 1] == ','){
		slen--;
	}

	str_code_to_html("]," JSONQUOTE("name") ":\"");
	__xdata uint16_t n = vlan_name(vlan);
	if (n== 0xffff) {
		dbg_string("VLAN has no name\n");
	} else {
		while(vlan_names[n] && vlan_names[n] != ' ')
			json_char_to_html(vlan_names[n++]);
	}

	str_code_to_html("\"," JSONQUOTE("pvid") ":[");
	for (uint8_t i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
		if (port_pvid_get(i) == vlan) { // i is _logical port_
			json_add_array_uint_item(machine.log_to_phys_port[i]);
		}
	}
	char_to_html(']');
	char_to_html('}');
}

/* Send counters
 * Only accepts physical port 1..9.
 * Returns an error if the port physical don't exists.
 */
bool send_counters(uint8_t phys_port)
{
	uint8_t phys_port_idx = phys_port - 1;
	if (phys_port_idx > (MACHINE_MAX_PORT - MACHINE_MIN_PORT))
		goto err;
	uint8_t log_port = machine.phys_to_log_port[phys_port_idx];

	dbg_string("send_counters called: "); dbg_byte(phys_port_idx); dbg_char('\n');
	slen = strtox(outbuf, HTTP_RESPONSE_JSON);
	dbg_string("sending counters\n"); dbg_byte(phys_port_idx);

	char_to_html('[');
	for (uint8_t counter = 0; counter < 0x37; counter++) {
		STAT_GET(counter, log_port);
		str_code_to_html("\"0x");
		counter_to_html();
		char_to_html('\"');
		if (counter != 0x36)
			char_to_html(',');
	}
	char_to_html(']');

	return false;

err:
	dbg_string("Error: counters: phy_port_idx don't exists\n");
	return true;
}


void send_l2(uint16_t idx)
{
	slen = strtox(outbuf, HTTP_RESPONSE_JSON);
	dbg_string("sending L2\n");
	dbg_short(idx);
	__xdata uint8_t entries_left = L2_MAX_TRANSFER;

	hw_poll(RTL837X_TBL_CTRL);

	/* The L2 table in the ASIC can hold up to 4096 (0x1000) entries, which
	 * are accessed using an index. The index is the hash of the MAC address
	 * and forwarding ID (basically VID). The hash-table is 4-way associative,
	 * i.e. for a given hash value, 4 entries with that same hash can be stored
	 * (i.e. the hash points to a bucket with up to 4 entries).
	 * When the table or a hash bucket is full, further entries will lead to
	 * L2 flooding.
	 * To find all entries, we start with entry-index 0 and iteratively search for
	 * the next entry (with the next higher index), until we arrive again at the first
	 * entry. The indices are sorted, so if an entry has a smaller index than
	 * the previous one, we know that we have wrapped around the entire table.
	 */
	__xdata uint16_t entry = idx & 0xfff;
	__xdata uint16_t first_entry = 0xffff; // An illegal entry index
	__bit first = true;
	char_to_html('[');
	while (1) {
		entries_left--;
		uint8_t port = 0;
		uint8_t lag;
		reg_read_m(RTL837x_TBL_DATA_0);
		REG_WRITE(RTL837x_TBL_DATA_0, sfr_data[0], sfr_data[1] & 0xfc, sfr_data[2] | (TBL_LUTREAD_NEXT_L2UC << 6), sfr_data[3]);

		REG_WRITE(RTL837X_TBL_CTRL, entry >> 8, entry, TBL_L2_UNICAST, TBL_EXECUTE);
		hw_poll(RTL837X_TBL_CTRL);

		reg_read_m(RTL837x_L2_DATA_OUT_B);
		__bit valid = (sfr_data[0] & 0x20) != 0;
		if (valid) {
			/* separator + 74-byte worst-case entry + closing "]" */
			if (slen + 76 > TCP_OUTBUF_SIZE)
				break;
			if (!first)
				char_to_html(',');
			first = false;

			// VLAN, taken from the read above instead of reading the register twice
			str_code_to_html("{" JSONQUOTE("vlan") ":\"");
			charhex_to_html(sfr_data[0] & 0x0f);
			byte_to_html(sfr_data[1]);

			// MAC
			str_code_to_html("\"," JSONQUOTE("mac") ":\"");
			byte_to_html(sfr_data[2]); char_to_html(':');
			byte_to_html(sfr_data[3]); char_to_html(':');
			port = (sfr_data[0] >> 6) & 0x3;
			reg_read_m(RTL837x_L2_DATA_OUT_A);
			byte_to_html(sfr_data[0]); char_to_html(':');
			byte_to_html(sfr_data[1]); char_to_html(':');
			byte_to_html(sfr_data[2]); char_to_html(':');
			byte_to_html(sfr_data[3]);

			// type
			reg_read_m(RTL837x_L2_DATA_OUT_C);
			if (sfr_data[1] & 0x1)
				str_code_to_html("\"," JSONQUOTE("type") ":" JSONQUOTE("s") "," JSONQUOTE("port") ":");
			else
				str_code_to_html("\"," JSONQUOTE("type")":" JSONQUOTE("l") "," JSONQUOTE("port") ":");

			port |= (sfr_data[3] & 0x3) << 2;

			if ( port == CPU_LOGICAL_PORT )
				itoa_html(CPU_PHYSICAL_PORT);
			else
				itoa_html(machine.log_to_phys_port[port]);

			str_code_to_html("," JSONQUOTE("lag") ":");
			lag = port_lag_of(port);
			itoa_html(lag == PORT_LAG_NONE ? 0 : lag + 1);
		}

		// Index
		reg_read_m(RTL837x_TBL_DATA_0);
		entry = (((uint16_t)sfr_data[2] & 0x0f) << 8) | sfr_data[3];
		if (valid) {
			str_code_to_html("," JSONQUOTE("idx") ":\"");
			byte_to_html(entry >> 8);
			byte_to_html(entry);
			char_to_html('"');
			char_to_html('}');
		}
		entry += 1; // We want the next entry following after the current entry

		if (first_entry == 0xffff)
			first_entry = entry;
		else if (first_entry == entry || !entries_left)
			break;
	}
	char_to_html(']');
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


void send_lag(void)
{
	dbg_string("send_lag called\n");
	slen = strtox(outbuf, HTTP_RESPONSE_JSON);

	str_code_to_html("{" JSONQUOTE("lags") ":[");
	for (uint8_t l=0; l < 4; l++) {
		bool isLACP = lacp_lag_ports[l] > 0;
		str_code_to_html("{" JSONQUOTE("lagNum") ":");
		itoa_html(l);
		str_code_to_html("," JSONQUOTE("type") ":");

		if (isLACP) {
			str_code_to_html(JSONQUOTE("lacp"));
		} else {
			str_code_to_html(JSONQUOTE("static"));
		}

		str_code_to_html("," JSONQUOTE("members") ":[");
		if(isLACP) {
			/* LACP configured members */
			for (uint8_t i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
				if((lacp_lag_ports[l] >> i) & 1) {
					json_add_array_uint_item(machine.log_to_phys_port[i]);
				}
			}
		} else {
			/* Static Members */
			uint16_t ports = port_lag_members_get(l);
			for (uint8_t i = 16; i > 0; i--) {
				if(!!(ports & 0x8000)) {
					json_add_array_uint_item(machine.log_to_phys_port[i-1]);
				}
				ports <<= 1;
			}	
		}
		char_to_html(']');
		if(isLACP) {
			str_code_to_html("," JSONQUOTE("active_members") ":[");
			/* LACP Active Members */
			for (uint8_t i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
				if((lacp_members_last[l] >> i) & 1) {
					json_add_array_uint_item(machine.log_to_phys_port[i]);
				}
			}
			str_code_to_html("]," JSONQUOTE("valid") ":");
			bool_to_json(lacp_agg_valid[l]);
			str_code_to_html("," JSONQUOTE("aggregator") ":\"");
			for (uint8_t j = 0; j < MAC_ADDR_LEN; j++)
				byte_to_html(lacp_agg_sys[l][j]);
			char_to_html('"');
		}
		str_code_to_html("," JSONQUOTE("hash") ":\"");
		reg_read_m(RTL837X_TRK_HASH_CTRL_BASE + (l << 2));
		sfr_data_to_html();

		char_to_html('"');
		char_to_html('}');
		char_to_html(',');
	}
	slen -=1; // remove comma
	str_code_to_html("]," JSONQUOTE("ports") ":[");
	for (uint8_t i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
		str_code_to_html("{" JSONQUOTE("p") ":");
		itoa_html(machine.log_to_phys_port[i]);
		str_code_to_html("," JSONQUOTE("lag") ":");
		itoa_html(lacp_port_lag[i]);
		str_code_to_html("," JSONQUOTE("a") ":\"");
		byte_to_html(lacp_actor_state[i]);
		str_code_to_html("\"," JSONQUOTE("pt") ":\"");
		byte_to_html(lacp_partner_state[i]);
		str_code_to_html("\"," JSONQUOTE("rs") ":");
		itoa_html(lacp_rx_state[i]);
		str_code_to_html("," JSONQUOTE("rx") ":\"");
		byte_to_html(lacp_rx_count[i] >> 8);
		byte_to_html(lacp_rx_count[i]);
		str_code_to_html("\"," JSONQUOTE("psys") ":\"");
		for (uint8_t j = 0; j < MAC_ADDR_LEN; j++)
			byte_to_html(lacp_partner_sys[i][j]);
		str_code_to_html("\"},");
	}
	slen -= 1; // remove comma
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


void send_vlanlist(void)
{
	/* Worst case per entry: {"id":4094,"name":"<117-char name>"} = 138 bytes
	 * (name bound: CMD_BUF_SIZE=128 minus command prefix); +1 for closing ']'.
	 * At worst case ~18 VLANs fit; typical configs with short names fit many more. */
	__xdata uint16_t i;
	__xdata uint16_t n;
	uint8_t first = 1;

	slen = strtox(outbuf, HTTP_RESPONSE_JSON);
	str_code_to_html("{" JSONQUOTE("vlan") ":[");

	for (i = vlan_next(1); i < 4095; i = vlan_next(i + 1)) {	/* E4: the bitmap */
		if (slen + 141 > TCP_OUTBUF_SIZE) /* comma + 138-byte worst-case entry + closing "]}" */
			break;

		if (!first)
			char_to_html(',');
		first = 0;

		str_code_to_html("{" JSONQUOTE("id") ":");
		itoa16_html(i);
		str_code_to_html("," JSONQUOTE("name") ":\"");

		n = vlan_name(i);
		if (n != 0xffff) {
			while (vlan_names[n] && vlan_names[n] != ' ')
				json_char_to_html(vlan_names[n++]);
		}

		char_to_html('"');
		char_to_html('}');

	}
	char_to_html(']');
	char_to_html('}');
}
