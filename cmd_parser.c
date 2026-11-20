/*
 * A Command parser for RTL Switch configuration
 */

// #define DEBUG
// #define REGDBG 1

#include <8051.h>
#include "cmd_parser.h"
#include "rtl837x_common.h"
#include "rtl837x_port.h"
#include "rtl837x_flash.h"
#include "boot.h"
#include "rtl837x_phy.h"
#include "rtl837x_regs.h"
#include "rtl837x_sfr.h"
#include "rtl837x_stp.h"
#include "rtl837x_lacp.h"
#include "rtl837x_igmp.h"
#include "rtl837x_bandwidth.h"
#include "rtl837x_lldp.h"
#include "rtl837x_storm.h"
#include "rtl837x_trap.h"
#include "rtl837x_cabletest.h"
#include "sfp.h"
#include "dhcp.h"
#include "sessions.h"
#include "dns.h"
#include "ntp.h"
#include "uip/uip.h"

#include "machine.h"
#include "rtl837x_leds.h"
#include "rtl837x_rldp.h"
#include "phy.h"
#include "con_ring.h"
#include "sched.h"
#include "om.h"
#include "om_gen.h"


extern __code const struct machine machine;
extern __xdata bool stp_enabled;
extern __code const uint8_t log_to_phys_port[9];
extern __xdata bool lldp_enabled;

extern volatile __xdata uint32_t ticks;
extern volatile __xdata uint8_t sfr_data[4];

extern __code const uint8_t * __code const greeting;
extern __code const uint8_t * __code const hex;

extern __xdata uint8_t flash_buf[FLASH_BUF_SIZE];
extern __xdata struct flash_region_t flash_region;



extern __xdata struct dhcp_state dhcp_state;

__xdata uint8_t vlan_names[VLAN_NAMES_SIZE];
__xdata uint16_t vlan_ptr;


extern __xdata uint16_t management_vlan;
extern __xdata struct uip_eth_addr uip_ethaddr;
__xdata uint8_t gpio_last_value[8] = { 0 };

// Temporatly for str to hex convertion value.
// Support up to 32_bits.
__xdata uint8_t hexvalue[4] = { 0 };


// Buffer for writing to flash 0x1fd000, copy to 0x1fe000
__xdata uint8_t cmd_buffer[CMD_BUF_SIZE];

__xdata	char save_cmd;

__xdata uint8_t ip[4];
__xdata uint8_t mac_parse_result[6];

// These variables combined create a Fixed-capacity vector/bounded buffer.
// `N_WORDS`: The total number of command arguments that can be tracked.
// `cmd_words_len` stores the number of arguments found inside `cmd_buffer`
// `cmd_words_b` stores the index into `cmd_buffer`, check `cmd_words_len` is index is valid.
#define N_WORDS 15
__xdata uint8_t cmd_words_len;
__xdata uint8_t cmd_words_b[N_WORDS];

__xdata uint8_t cmd_history[CMD_HISTORY_SIZE];
__xdata uint16_t cmd_history_ptr;
__xdata uint8_t cli_job;		/* a dump in progress (cli_job_more) */
__xdata uint8_t cli_job_sleep;
static void cmd_save(void);

// Error set by commands
__xdata uint8_t err_status;

__xdata uint16_t atoi_results_short;
__xdata uint8_t atoi_results_u8;

inline uint8_t isletter(uint8_t l)
{
	// return (l >= 'a' && l <= 'z') || (l >= 'A' && l <= 'Z');

	// Make it lowercase
	l |= 0x20;
	l -= 'a';
	return (l <= ('z'-'a'));
}


inline uint8_t isnumber(uint8_t l)
{
	// return (l >= '0' && l <= '9');
	l -= '0';
	return (l <= ('9'-'0'));
}

inline uint8_t ishexnumber(uint8_t l)
{
	if ((l >= '0' && l <= '9') || (l >= 'a' && l <= 'f') || (l >= 'A' && l <= 'F')) {
		return 1;
	}
	return 0;
}

uint8_t atoi_long_hex(__xdata uint32_t *num, uint8_t idx)
{
	uint8_t err = 1;
	*num = 0;

	while (ishexnumber(cmd_buffer[idx])) {
		err = 0;

		*num <<= 4;

		if (cmd_buffer[idx] - '0' < 10) {
			*num |= cmd_buffer[idx] - '0';
		} else if (cmd_buffer[idx] - 'a' < 6) {
			*num |= cmd_buffer[idx] - 'a' + 10;
		} else if (cmd_buffer[idx] - 'A' < 6) {
			*num |= cmd_buffer[idx] - 'A' + 10;
		} else {
			err = 1;
			break;
		}
		idx++;
	}

	return err;
}

uint8_t atoi_short_hex(__xdata uint16_t *num, uint8_t idx)
{
	__xdata uint32_t temp;
	__xdata uint8_t err = atoi_long_hex(&temp, idx);
	*num = (uint16_t) temp;
	return err;
}

uint8_t cmd_compare(uint8_t start, __code const uint8_t * cmd)
{
	if (cmd_words_len == 0 || start > (cmd_words_len - 1)) {
		return 0;
	}
	uint8_t i = cmd_words_b[start];
	uint8_t j = 0;

	do {
		uint8_t c = cmd[j];
		uint8_t b = cmd_buffer[i];

		// cmd is guaranteed to be NUL-terminated.
        if (c == NUL) {
            if ((b == ' ') || (b == NUL)) {
				// Match
                return 1;
            }
            break;
        }
        if (b != c) {
            break;
        }

		j += 1;
        i += 1;
	} while (i < CMD_BUF_SIZE);

	// No match
	return 0;
}


/* Converts ascii-hex array into value.
	returns number of hexvalue[] entries has been written.
	return value = 0 means error.
*/
uint8_t atoi_hex(uint8_t idx)
{
	uint8_t h_idx = 0;
	uint8_t val = 0;
	uint8_t c;

	while(1) {
		c = cmd_buffer[idx];

		if (c == NUL || c == ' ') {
			break;
		}

		if (h_idx == 8) {
			h_idx = 0;
			break;
		}

		// swap hex nibbles
		val = (val >> 4) | (val << 4);

		if ((uint8_t)(c - '0') < 10) {
			val |= c - '0';
		} else {
			c |= 0x20;
			c -= 'a';
			if (c > 5) {
				h_idx = 0;
				break;
			}
			val |= c + 10;
		}

		idx++;
		hexvalue[h_idx >> 1] = val;

		if (h_idx & 1 == 1) {
			val = 0;
		}
		h_idx++;
	}

	if (h_idx & 1) {
		hexvalue[h_idx >> 1] <<= 4;
		hexvalue[3] = hexvalue[3] >> 4 | (hexvalue[2] << 4);
		hexvalue[2] = hexvalue[2] >> 4 | (hexvalue[1] << 4);
		hexvalue[1] = hexvalue[1] >> 4 | (hexvalue[0] << 4);
		hexvalue[0] >>= 4;
	}

	return ((h_idx + 1) >> 1);
}


// Returns 0 or the number of digits taken into account for conversion.
// Stops at any non-digit '0'-'9' char or more than 3 bytes.
uint8_t atoi_byte(uint8_t idx)
{
	uint8_t cnt = 0;
	uint8_t num = 0;

	uint8_t * ptr = &cmd_buffer[idx];

	while (1) {
		uint8_t val = *ptr++ - '0';
		if (val > 9)
			break;
		if (num > 25 || (num == 25 && val > 5) || cnt >= 3)
			return 0;
		num = (num * 10) + val;
		cnt++;
	}

	atoi_results_u8 = num;
	return cnt;
}


// Returns 0 or the number of digits taken into account for conversion.
// Stops at any non-digit '0'-'9' char or bytes is more then 5.
uint8_t atoi_short(uint8_t idx)
{
	uint8_t cnt = 0;
	atoi_results_short = 0;

	uint8_t *ptr = &cmd_buffer[idx];

	while (1) {
		uint8_t val = *ptr++ - '0';
		if (val > 9)
			break;
		if (atoi_results_short > 6553 || (atoi_results_short == 6553 && val > 5) || cnt >= 5)
			return 0;
		atoi_results_short = (atoi_results_short * 10) + val;
		cnt++;
	}

	return cnt;
}

/* Parse, validate and translate phys_to_log_port physical port argument.
 * The CPU-port, i.e. port 0, is not a valid argument.
 * returns 0 when on parser error or invalid value.
 * returns non-zero number of characters consumed.
 * Store the value in atoi_results_u8.
 */
uint8_t cmd_parse_port(uint8_t idx) {
	uint8_t port = cmd_buffer[idx] - '0' - 1;
	if (port > 8)
		return 0;

	port = machine.phys_to_log_port[port];
	if (!MACHINE_PORT_VALID(port))
		return 0;

	atoi_results_u8 = port;
	return 1;
}


// Same as cmd_parse_port() but additionally check for trailing SPACE or NUL.
// returns 0 when on parser error or invalid value or no SPACE or no NUL.
// returns non-zero number of characters consumed including the SPACE.
uint8_t cmd_parse_port_separator(uint8_t idx) {
	uint8_t ret = cmd_parse_port(idx);
	if (ret != 0) {
		idx += ret;
		uint8_t c = cmd_buffer[idx];
		if (c == ' ') {
			ret++;
		} else if (c != NUL)
			ret = 0;
	}
	return ret;
}

// Same as cmd_parse_port_separator() but additionally allow CPU-port.
// returns 0 when on parser error or invalid value or no SPACE or no NUL.
// returns number of characters consumed including the SPACE.
uint8_t cmd_parse_port_cpu_separator(uint8_t idx) {
	uint8_t ret = cmd_parse_port(idx);

	if (ret == 0 && cmd_buffer[idx] == '0') {
		ret = 1;
		atoi_results_u8 = CPU_LOGICAL_PORT;
	}

	if (ret != 0) {
		idx += ret;
		uint8_t c = cmd_buffer[idx];
		if (c == ' ') {
			ret++;
		} else if (c != NUL)
			ret = 0;
	}
	return ret;
}


// check if the cmd_buffer[idx] is a SPACE.
__bit cmd_is_space(uint8_t idx) {
	return cmd_buffer[idx] == ' ';
}

// check if the cmd_buffer[idx] is a SPACE or NUL.
__bit cmd_is_space_or_nul(uint8_t idx) {
	uint8_t c = cmd_buffer[idx];
	return c == ' ' || c == NUL;
}

// Parse an IPv4 address
// returns 0 when on parse error or invalid value or it don't ends with SPACE or NUL.
// returns non-zero number of characters consumed including the SPACE.
uint8_t parse_ip(uint8_t idx)
{
	uint8_t b = 0;
	uint8_t ret;
	uint8_t idx_start = idx;

	while(1) {
		ret = atoi_byte(idx);
		if (ret == 0)
			goto err;

		idx += ret;
		ip[b++] = atoi_results_u8;

		ret = cmd_buffer[idx];
		if (b == 4) {
			if (ret == ' ') {
				idx++;
				break;
			}
			if (ret == NUL)
				break;
			goto err;
		}
		idx++;

		if (ret != '.')
			goto err;
	}
	return idx - idx_start;

err:
	print_string("Error in IP format\n");
	return 0;
}

// Prints an IPv4 address.
void print_ip(__xdata uint8_t * ptr) __banked
{
	uint8_t idx = 0;
	uint8_t num;

	while(1) {
		num = *ptr++;
		itoa(num);
		if (++idx == 4)
			break;

		write_char('.');
	}
}

// Prints a MAC address.
void print_mac(__xdata uint8_t *ptr) __banked
{
	uint8_t idx = 0;
	uint8_t num;

	while (1) {
		num = *ptr++;
		print_byte(num);
		if (++idx == 6)
			break;

		write_char(':');
	}
}

static void cmd_error(__code const char *msg)
{
	err_status = ERR_INVALID_ARGUMENT;
	print_string("Error: ");
	print_string(msg);
}


// Parses a MAC address "aa:bb:cc:dd:ee:ff" (":" or "-" separators) from
// cmd_buffer starting at idx into the global mac_parse_result[6]. Each octet
// must be exactly two hex digits with a separator between octets and nothing
// trailing, so repeated separators, missing digits or a trailing hex digit are
// rejected. Returns the number of bytes consumed, or 0 on error.
uint8_t parse_mac(uint8_t idx)
{
	uint8_t b = 0;
	uint8_t c = ':';

	do {
		if (c != ':' && c != '-')
			goto err;

		__bit again = true;
		uint8_t v = 0;

		while(1) {
			c = cmd_buffer[idx++];
			c -= '0';
			if (c > 9) {
				c |= 0x20;
				c += '0' - 'a';
				if (c > 5)
					goto err;
				c += 10;
			}
			// force swap-instr.
			v = (v << 4) | (v >> 4);
			v |= c;

			if (again)
				again = false;
			else
				break;
		}
		mac_parse_result[b++] = v;

		c = cmd_buffer[idx++];
	} while(b < 6);

	if (c == ' ' || c == NUL)
		return idx;

err:
	return 0;
}

// Sets the management MAC from a "mac <aa:bb:cc:dd:ee:ff>" command (or prints
// the current one with a bare "mac"). Refuses blank/multicast/all-zero-OUI
// addresses so the running MAC stays reachable, and
// re-registers the static L2 management entry so the change applies without a
// reboot. As a regular command a "mac ..." line in the startup config applies
// on every boot (execute_config runs before the final static L2 entry).
void parse_mac_cmd(void)
{
	if (cmd_words_len == 1) {
		print_string("Current MAC: ");
		print_mac(uip_ethaddr.addr);
		write_char('\n');
		return;
	}
	if (cmd_words_len != 2 || !parse_mac(cmd_words_b[1])) {
		cmd_error("mac [<aa:bb:cc:dd:ee:ff>]\n");
		return;
	}
	if ((mac_parse_result[0] & 0x01) || ((mac_parse_result[0] | mac_parse_result[1] | mac_parse_result[2]) == 0)) {
		cmd_error("the MAC must be unicast with a non-zero first three bytes\n");
		return;
	}
	if (memcmp(mac_parse_result, uip_ethaddr.addr, 6) == 0) {
		print_string("MAC unchanged\n");
		return;
	}

	// Swap the static management entry over to the new address: remove the
	// old one (still in uip_ethaddr) before overwriting it.
	port_l2_static_mgmt(uip_ethaddr.addr, management_vlan, true);
	memcpy(uip_ethaddr.addr, mac_parse_result, 6);
	port_l2_static_mgmt(uip_ethaddr.addr, management_vlan, false);

	print_string("MAC set to: ");
	print_mac(uip_ethaddr.addr);
	write_char('\n');
	print_string("Save the startup config to keep it across reboots\n");
}

static uint8_t name_char(uint8_t c)
{
	if (c < 0x20 || c > 0x7e || c == '"' || c == '\\')
		return '.';
	return c;
}


void parse_lag(void)
{
	__xdata uint8_t group;
	__xdata uint16_t members = 0;

	if (cmd_compare(1, "show")) {
		print_string("LAG status:\n");
		for (uint8_t i = 0; i < 4; i++) {
			write_char(' '); write_char('1' + i);
			members = port_lag_members_get(i);
			if (!members) {
				print_string(" disabled\n");
				continue;
			}
			print_string(" member ports: ");
			for (uint8_t j = 0; j < 10; j++) {
				if (members & 1) {
					print_phys_port(j);
					write_char(' ');
				}
				members >>= 1;
			}
			print_string(" (hash: 0x"); 
			reg_read_m(RTL837X_TRK_HASH_CTRL_BASE + (i << 2));
			print_byte(sfr_data[3]);
			print_string(")\n");
		}
		return;
	}

	if (cmd_words_len < 3)
		goto err;

	// Parse group, expect only one number 0-9;
	if (atoi_byte(cmd_words_b[1]) != 1)
		goto err;

	group = atoi_results_u8 - 1;
	if (group > 3)
		goto err;

	if (cmd_compare(2, "d")) {
		lacp_lag_set(group, 0);
		port_lag_members_set(group, 0);
		return;
	}

	uint8_t w = 2;
	__xdata uint8_t lacp_mode = 0;
	if (cmd_words_len > 2 && cmd_compare(2, "lacp")) {
		lacp_mode = 1;
		w = 3;
		if (cmd_words_len > 3 && cmd_compare(3, "off")) {
			lacp_lag_set(group, 0);
			return;
		}
	}
	while (w < cmd_words_len) {
		// Parse port.
		if (cmd_parse_port_separator(cmd_words_b[w++]) == 0)
			goto err;

		members |= ((uint16_t)1) << atoi_results_u8;
	}
	if (lacp_mode) {
		lacp_lag_set(group, members);
	} else {
		lacp_lag_set(group, 0);
		port_lag_members_set(group, members);
	}
	return;
err:
	cmd_error("lag (show | <1-4> (d | [lacp] <port>...))\n");
}


void parse_lag_hash(void)
{
	// Parse group, expect only one number 0-9.
	if (cmd_words_len < 3 || atoi_byte(cmd_words_b[1]) != 1)
		goto err;

	uint8_t group = atoi_results_u8 - 1;
	if (group > 3)
		goto err;

	uint8_t hash = 0;
	uint8_t w = 2;
	while (w < cmd_words_len) {
		if (cmd_compare(w, "spa"))
			hash |= LAG_HASH_SOURCE_PORT_NUMBER;
		else if (cmd_compare(w, "smac"))
			hash |= LAG_HASH_L2_SMAC;
		else if (cmd_compare(w, "dmac"))
			hash |= LAG_HASH_L2_DMAC;
		else if (cmd_compare(w, "sip"))
			hash |= LAG_HASH_L3_SIP;
		else if (cmd_compare(w, "dip"))
			hash |= LAG_HASH_L3_DIP;
		else if (cmd_compare(w, "sport"))
			hash |= LAG_HASH_L4_SPORT;
		else if (cmd_compare(w, "dport"))
			hash |= LAG_HASH_L4_DPORT;
		else {
			print_string("Error: invalid hash type:");
			print_string_x(&cmd_buffer[cmd_words_b[w]]);
			write_char('\n');
			goto err;
		}
		w++;
	}
	port_lag_hash_set(group, hash);
	return;
err:
	cmd_error("laghash <1-4> [smac|dmac|sip|dip|sport|dport]\n");
}


void parse_vlan(void)
{
	vlan_settings.vlan = 0;
	vlan_settings.members = 0;
	vlan_settings.tagged = 0;

	if (cmd_words_len < 2)
		goto err;

	// Parse the VLAN number
	if (atoi_short(cmd_words_b[1]) != 0) {
		if (atoi_results_short > 4094)
			goto err;
		vlan_settings.vlan = atoi_results_short;

		/* the management VLAN is mgmt.vlan now: not a name, the VLAN untouched */
		if (cmd_compare(2, "mgmt")) {
			cmd_error("set mgmt.vlan\n");
			return;
		}

		// Other commands vlan 0 is invalid
		if (!vlan_settings.vlan)
			goto err;

		if (cmd_words_len == 3 && cmd_buffer[cmd_words_b[2]] == 'd') {
			vlan_delete(vlan_settings.vlan);
			return;
		}

		uint8_t w = 2;
		uint8_t pos = cmd_words_b[w];
		if (cmd_words_len > w && isletter(cmd_buffer[pos])) {
			uint8_t i = 0;
			while (!cmd_is_space_or_nul(pos + i))
				i++;
			if (vlan_ptr + i > (VLAN_NAMES_SIZE - 5)) {
				print_string("VLAN name table full, name ignored\n");
			} else {
				vlan_name_remove(vlan_settings.vlan);
				__xdata uint8_t *vlan_str = &vlan_names[vlan_ptr];
				*vlan_str++ = hex[(vlan_settings.vlan >> 8) & 0xf];
				*vlan_str++ = hex[(vlan_settings.vlan >> 4) & 0xf] ;
				*vlan_str++ = hex[vlan_settings.vlan & 0xf];
				while(!cmd_is_space_or_nul(pos)) {
					uint8_t c = name_char(cmd_buffer[pos++]);
					write_char(c);
					*vlan_str++ = c;
				}
				*vlan_str++ = ' '; *vlan_str = NUL;
				vlan_ptr = vlan_str - vlan_names;
				print_string("<\n");
			}
			w++;
		}

		uint8_t ret;
		uint8_t idx;
		while (cmd_words_len > w) {
			idx = cmd_words_b[w++];
			ret = cmd_parse_port(idx);
			if (ret == 0)
				goto err;

			idx += ret;
			uint16_t pmask = ((uint16_t)1) << atoi_results_u8;
			vlan_settings.members |= pmask;

			if (cmd_buffer[idx] == 't') {
				vlan_settings.tagged |= pmask;
				idx++;
			}

			if (!cmd_is_space_or_nul(idx))
				goto err;
		}
		vlan_create();
	} else if (cmd_compare(1, "show")) {
		vlan_dump();
	} else {
		goto err;
	}

	if (cmd_words_len >= 3 && isletter(cmd_buffer[cmd_words_b[2]])) {
		print_string("vlan_ptr "); print_short(vlan_ptr); write_char(':');
		write_char('>'); print_string_x(&vlan_names[0]); write_char('<'); write_char('\n');
	}
	return;
err:
	cmd_error("vlan (<vlan-id>|show) [port][t]...\n");
}


bool vlan_ingress_mode_parse(char c, __xdata vlan_ingress_mode_t *mode)
{
	switch (c) {
	case 'u':
		*mode = VLAN_UNTAGGED;
		return true;
	case 't':
		*mode = VLAN_TAGGED;
		return true;
	case 'a':
		*mode = VLAN_ALL;
		return true;
	default:
		*mode = VLAN_INVALID;
		return false;
	}
}

void parse_mirror(void)
{
	__xdata uint8_t mirroring_port = 0;
	__xdata uint16_t rx_pmask = 0;
	__xdata uint16_t tx_pmask = 0;

	if (cmd_compare(1, "status")) {
		reg_read_m(RTL837x_MIRROR_CTRL);
		uint8_t mPort = sfr_data[3];
		if (mPort & 1) {
			print_string("Enabled: ");
		} else {
			print_string("NOT Enabled: ");
		}
		print_string("Mirroring port: ");
		print_phys_port(mPort >> 1);
		reg_read_m(RTL837x_MIRROR_CONF);
		uint16_t m = sfr_data[0];
		m = (m << 8) | sfr_data[1];
		print_string(", Port mask RX: ");
		print_short(m);
		m = sfr_data[2];
		m = (m << 8) | sfr_data[3];
		print_string(", Port mask TX: ");
		print_short(m);
		write_char('\n');
		return;
	} else if (cmd_compare(1, "off")) {
		port_mirror_del();
		return;
	}

	if (cmd_words_len < 2)
		goto err;

	// The first argument is the mirroring (monitor) port
	uint8_t ret = cmd_parse_port(cmd_words_b[1]);
	if (ret == 0)
		goto err;
	mirroring_port = atoi_results_u8;

	uint8_t w = 2;
	uint8_t port;
	while (w < cmd_words_len) {
		uint8_t idx = cmd_words_b[w++];
		ret = cmd_parse_port(idx);
		if (ret == 0)
			goto err;

		idx += ret;
		port = atoi_results_u8;

		ret = cmd_buffer[idx];
		uint16_t pmask = ((uint16_t)1) << port;
		if (ret != 't')
			rx_pmask |= pmask;
		if (ret != 'r')
			tx_pmask |= pmask;
	}
	port_mirror_set(mirroring_port, rx_pmask, tx_pmask);
	return;

err:
	cmd_error("Port/command missing: mirror [status/off/<mirroring port> [port][t/r]]...\n");
	return;
}


void parse_sfp(void)
{
	uint8_t slot;

	if (cmd_words_len != 3)		/* the modules: get state.sfp */
		goto err;

	uint8_t idx = cmd_words_b[1];
	uint8_t ret = atoi_byte(idx);
	idx += ret;
	slot = atoi_results_u8 - 1;
	if (ret == 0 || !cmd_is_space(idx) || slot > 1) {
		cmd_error("Illegal SFP slot number\n");
		return;
	}
	if (slot >= machine.n_sfp) {
		cmd_error("SFP slot not present\n");
		return;
	}

	if (cmd_compare(2, "10g")) {
		print_string(" 10G\n");
		sfp_speed[slot] = SFP_SPEED_10G;
	} else if (cmd_compare(2, "2g5")) {
		print_string(" 2.5G\n");
		sfp_speed[slot] = SFP_SPEED_2G5;
	} else if (cmd_compare(2, "1g")) {
		print_string(" 1G\n");
		sfp_speed[slot] = SFP_SPEED_1G;
	} else if (cmd_compare(2, "100m")) {
		print_string(" 100M\n");
		sfp_speed[slot] = SFP_SPEED_100M;
	} else if (cmd_compare(2, "auto")) {
		print_string(" AUTO\n");
		sfp_speed[slot] = SFP_SPEED_AUTO;
	} else {
		goto err;
	}
	sfp_pins_last |= 0x1 << (slot << 2);	/* found again at once, then read */
	sfp_pins(slot);
	return;
err:
	cmd_error("\nUsage:\n\tsfp [1|2] [1g|2g5|10g|100m|auto]\n\t(the modules: get state.sfp)\n");
}


void parse_regget(void)
{
	if (cmd_words_len != 2) {
		goto err;
	}

	uint8_t hex_size = atoi_hex(cmd_words_b[1]);

	if (hex_size == 0 || hex_size > 2) {
		goto err;
	}

	uint16_t reg = hexvalue[0];
	if (hex_size == 2) {
		reg <<= 8;
		reg |= hexvalue[1];
	}

	print_string("REGGET: ");
	print_short(reg);
	print_string(": VAL: ");

	reg_read_m(reg);
	print_sfr_data();
	write_char('\n');
	return;

err:
	cmd_error("regget <hexvalue>\n\tlike: regget 0BB0 or regget 0c");
	return;
}


void parse_regset(void)
{
	if (cmd_words_len != 3) {
		goto err;
	}

	uint8_t hex_size = atoi_hex(cmd_words_b[1]);
	if (hex_size == 0 || hex_size > 2) {
		goto err;
	}

	uint16_t reg = hexvalue[0];
	if (hex_size == 2) {
		reg <<= 8;
		reg |= hexvalue[1];
	}

	hex_size = atoi_hex(cmd_words_b[2]);
	if (hex_size == 0 || hex_size > 4) {
		goto err;
	}

	// zero sfr memory data
	sfr_set_zero();

	// copy data over sfr memory
	uint8_t offset = 4 - hex_size;
	while(hex_size) {
		hex_size -= 1;
		sfr_data[offset + hex_size] = hexvalue[hex_size];
	}
	print_string("REGSET: ");
	print_short(reg);

	reg_write_m(reg);

	print_string(": VAL: ");
	print_sfr_data();
	write_char('\n');
	return;

err:
	cmd_error("regset <hexvalue> <hexvalue>\n\tlike regset 0b abcd1234.");
}


void parse_sdsget(void)
{
	__xdata uint8_t sds_id, page, reg, hex_size;

	if (cmd_words_len != 4) {
		goto err;
	}

	if (!atoi_byte(cmd_words_b[1])) {
		goto err;
	}
	sds_id = atoi_results_u8;

	hex_size = atoi_hex(cmd_words_b[2]);
	if (hex_size != 1) {
		goto err;
	}
	page = hexvalue[0];

	hex_size = atoi_hex(cmd_words_b[3]);
	if (hex_size != 1) {
		goto err;
	}
	reg = hexvalue[0];

	print_string("SDSGET: ");
	itoa(sds_id);
	print_string(":0x");
	print_byte(page);
	print_string(":0x");
	print_byte(reg);
	print_string(": VAL: ");

	sds_read(sds_id, page, reg);
	print_phy_data();
	write_char('\n');
	return;

err:
	cmd_error("sdsget <sds-id> <hex:page> <hex:reg>\n");
	return;
}


void parse_sdsset(void)
{
	__xdata uint8_t sds_id, page, reg, hex_size;
	__xdata uint16_t val;

	if (cmd_words_len != 5) {
		goto err;
	}

	if (!atoi_byte(cmd_words_b[1])) {
		goto err;
	}
	sds_id = atoi_results_u8;

	hex_size = atoi_hex(cmd_words_b[2]);
	if (hex_size != 1) {
		goto err;
	}
	page = hexvalue[0];

	hex_size = atoi_hex(cmd_words_b[3]);
	if (hex_size != 1) {
		goto err;
	}
	reg = hexvalue[0];

	hex_size = atoi_hex(cmd_words_b[4]);
	if (hex_size == 0 || hex_size > 2) {
		goto err;
	}
	val = hexvalue[0];
	if (hex_size == 2) {
		val <<= 8;
		val |= hexvalue[1];
	}

	print_string("SDSSET: ");
	itoa(sds_id);
	print_string(":0x");
	print_byte(page);
	print_string(":0x");
	print_byte(reg);
	print_string(": VAL: ");

	sds_write_v(sds_id, page, reg, val);

	print_short(val);
	write_char('\n');
	return;

err:
	cmd_error("sdsset <sds-id> <hex:page> <hex:reg> <hex:val>\n");
	return;
}


void parse_phyget(void)
{
	__xdata uint8_t phy_id, dev_id, hex_size;
	__xdata uint16_t reg;

	if (cmd_words_len != 4) {
		goto err;
	}

	if (!atoi_byte(cmd_words_b[1])) {
		goto err;
	}
	phy_id = atoi_results_u8;


	if (!atoi_byte(cmd_words_b[2])) {
		goto err;
	}
	dev_id = atoi_results_u8;

	hex_size = atoi_hex(cmd_words_b[3]);
	if (hex_size == 0 || hex_size > 2) {
		goto err;
	}
	reg = hexvalue[0];
	if (hex_size == 2) {
		reg <<= 8;
		reg |= hexvalue[1];
	}

	print_string("PHYGET: ");
	itoa(phy_id);
	print_string(":");
	itoa(dev_id);
	print_string(".");
	print_short(reg);
	print_string(": VAL: ");

	phy_read(phy_id, dev_id, reg);
	print_phy_data();
	write_char('\n');
	return;

err:
	cmd_error("phyget <phy-id> <dev-id> <hex:reg>\n");
	return;
}


void parse_physet(void)
{
	__xdata uint8_t phy_id, dev_id, hex_size;
	__xdata uint16_t reg, val;

	if (cmd_words_len != 5) {
		goto err;
	}

	if (!atoi_byte(cmd_words_b[1])) {
		goto err;
	}
	phy_id = atoi_results_u8;


	if (!atoi_byte(cmd_words_b[2])) {
		goto err;
	}
	dev_id = atoi_results_u8;

	hex_size = atoi_hex(cmd_words_b[3]);
	if (hex_size == 0 || hex_size > 2) {
		goto err;
	}
	reg = hexvalue[0];
	if (hex_size == 2) {
		reg <<= 8;
		reg |= hexvalue[1];
	}

	hex_size = atoi_hex(cmd_words_b[4]);
	if (hex_size == 0 || hex_size > 2) {
		goto err;
	}
	val = hexvalue[0];
	if (hex_size == 2) {
		val <<= 8;
		val |= hexvalue[1];
	}

	print_string("PHYSET: ");
	itoa(phy_id);
	print_string(":");
	itoa(dev_id);
	print_string(".");
	print_short(reg);
	print_string(": VAL: ");

	phy_write(phy_id, dev_id, reg, val);

	print_short(val);
	write_char('\n');
	return;

err:
	cmd_error("physet <phy-id> <dev-id> <hex:reg> <hex:val>\n");
	return;
}


void parse_bw(void)
{
	if (cmd_words_len < 3) // Check for at least 3 arguments
		goto err;

	if (cmd_parse_port_separator(cmd_words_b[2]) == 0)
		goto err;
	uint8_t port = atoi_results_u8;

	if (cmd_compare(1, "status")) {
		bandwidth_status(port);
		return;
	}

	if (cmd_words_len < 4) // Check for at least 4 arguments
		goto err;

	// Ensure first argument is only `in` or `out`.
	__bit is_in = cmd_compare(1, "in") != 0;
	if (!(is_in || cmd_compare(1, "out")))
		goto err;

	if (cmd_compare(3, "drop")) {
		if (is_in) {
			bandwidth_ingress_drop(port);
			return;
		}
		goto err;
	}

	if (cmd_compare(3, "fc")) {
		if (is_in) {
			bandwidth_ingress_fc(port);
			return;
		}
		goto err;
	}

	if (cmd_compare(3, "off")) {
		if (is_in) {
			bandwidth_ingress_disable(port);
		} else  {
			bandwidth_egress_disable(port);
		}
		return;
	}

	__xdata uint32_t bw = 0;
	uint8_t hex_size = atoi_hex(cmd_words_b[3]);
	if (hex_size == 0 || hex_size > 4)
		goto err;

	uint8_t i = 0;
	do {
		hex_size--;
		*(((uint8_t *) &bw) + hex_size) = hexvalue[i++];
	} while (hex_size);

	if (is_in) {
		bandwidth_ingress_set(port, bw);
	} else {
		bandwidth_egress_set(port, bw);
	}
	return;

err:
	cmd_error("bw [in|out|status] <port> [<hexvalue>|off|drop|fc]\n");
}

static __code const char * __code const storm_types[STORM_TYPES] = { "bcast", "mcast", "ucast", "umcast" };

void parse_storm(void)
{
	uint8_t port, type, d;
	__xdata uint32_t rate;
	__xdata uint8_t *p;

	if (cmd_words_len == 1)
		return storm_show();
	if (cmd_words_len < 4 || !cmd_parse_port_separator(cmd_words_b[1]))
		goto err;
	port = atoi_results_u8;

	type = 0;
	while (!cmd_compare(2, storm_types[type])) {
		if (++type == STORM_TYPES)
			goto err;
	}

	if (cmd_words_len == 4 && cmd_compare(3, "off"))
		return storm_off(port, type);
	if (cmd_words_len != 5)
		goto err;

	rate = 0;
	p = &cmd_buffer[cmd_words_b[3]];
	do {
		d = *p++ - '0';
		if (d > 9 || rate > 99999999)
			goto err;
		rate = (rate << 3) + (rate << 1) + d;
	} while (*p != ' ' && *p != NUL);

	if (cmd_compare(4, "pps")) {
		if (!rate || rate > 0xfffff)
			goto err;
		storm_set(port, type, rate, 1);
	} else if (cmd_compare(4, "kbps")) {
		if (!rate || rate > 10000000)
			goto err;
		storm_set(port, type, rate, 0);
	} else {
		goto err;
	}
	return;

err:
	print_string("usage: storm [<port> bcast|mcast|ucast|umcast <rate> pps|kbps|off]\n");
}

void parse_flash(void)
{
	if (cmd_words_len >= 2) {
		uint8_t c = cmd_buffer[cmd_words_b[1]];
		if (c == 's') {
			print_string("\nSECURITY REGISTERS\n");
			// The following will only show something else than 0xff if it was programmed for a managed switch
			print_string("Region 1: ");
			flash_region.addr = 0x0001000;
			flash_region.len = 40;
			flash_read_security();
			print_string("\nRegion 2: ");
			flash_region.addr = 0x0002000;
			flash_region.len = 40;
			flash_read_security();
			print_string("\nRegion 3: ");
			flash_region.addr = 0x0003000;
			flash_region.len = 40;
			flash_read_security();
		} else if (c == 'j') {
			print_string("\nJEDEC ID\n");
			flash_read_jedecid();
		} else if (c == 'u') {
			print_string("\nUNIQUE ID (note: only 4 bytes are likely correct here!)\n");
			flash_read_uid();
		} else if (c == 'r') {
			__xdata uint8_t i;
			__xdata uint32_t monitor_addr = 0x0;
			__xdata int16_t monitor_len = 0x100;
			if (cmd_words_len >= 3) {
				uint8_t err = atoi_long_hex(&monitor_addr, cmd_words_b[2]);
				if (err) {
					print_string("Invalid monitor address\n");
					return;
				}
			}
			if (cmd_words_len >= 4) {
				uint8_t err = atoi_short_hex(&monitor_len, cmd_words_b[3]);
				if (err) {
					print_string("Invalid monitor len\n");
					return;
				}
			}

			do {
				flash_region.addr = monitor_addr;
				flash_region.len = 16;
				print_byte(flash_region.addr >> 16);
				print_byte(flash_region.addr >> 8);
				print_byte(flash_region.addr >> 0);
				write_char(' ');
				flash_read_bulk(flash_buf);
				for (i = 0; i < 16; i++) {
					if (i % 8 == 0) write_char(' ');
					print_byte(flash_buf[i]);
					write_char(' ');
				}
				print_string("   |");
				for (i = 0; i < 16; i++) {
					if (flash_buf[i] >=0x20 && flash_buf[i] <= 0x7f) {
						write_char(flash_buf[i]);
					} else {
						write_char('.');
					}
				}
				print_string("|\n");
				monitor_addr += 16;
				monitor_len -= 16;
			 } while (monitor_len > 0);
		}
		else
		{
			cmd_error("flash [s|j|u|r <addr> <len>]\n"
				  "  s: read security registers\n"
				  "  j: read JEDEC ID\n"
				  "  u: read unique ID\n"
				  "  r: read flash content\n");
		}
	}
}

// Parse command into words
// cmd_words_len contains the number of words found.
// cmd_words_b[] contains only start of a word offset.
// Returns the parsing status via `err_status`-variable.
void cmd_tokenize(void) __banked
{
#ifdef DEBUG
	print_string("Tokenizing command\n");
	print_string_x(&cmd_buffer[0]);
	write_char('<'); write_char('\n');
#endif
	err_status = ERR_OK;
	uint8_t line_ptr = 0;
	uint8_t is_white = 1;
	uint8_t word = 0;
	uint8_t c = 0;

	while(1) {
		c = cmd_buffer[line_ptr];
		
		if (c == NUL) {
			// Store the word count
			cmd_words_len = word;
			break;
		}

		if (line_ptr == CMD_BUF_SIZE - 1) {
			err_status = ERR_CMD_TOO_LONG;
			return;
		}

		if (is_white && c != ' ') {
			is_white = 0;

			cmd_words_b[word++] = line_ptr;
			if (word >= N_WORDS) {
				cmd_words_len = 0;
				print_string("\nSyntax error: too many arguments.");
				err_status = ERR_TOO_MANY_ARGUMENTS;
				return;
			}
		} else if (c == ' ') {
			is_white = 1;
		}

		line_ptr++;
	}
}

#ifdef DEBUG_OBJ
// Print GPIO status
void print_gpio_status(void) {
	for (uint8_t idx = 0; idx < 2; idx++) {
		reg_read(RTL837X_REG_GPIO_00_31_INPUT + (idx * 4));
		print_string("GPIO ");
		write_char(idx + '0');
		write_char(':');
		write_char(' ');

		print_byte(SFR_DATA_24);
		print_byte(SFR_DATA_16);
		print_byte(SFR_DATA_8);
		print_byte(SFR_DATA_0);

		write_char(' ');
		print_byte( gpio_last_value[(idx *4)] ^ SFR_DATA_24);
		gpio_last_value[(idx *4)] = SFR_DATA_24;
		print_byte( gpio_last_value[(idx *4) + 1] ^ SFR_DATA_16);
		gpio_last_value[(idx *4) + 1] = SFR_DATA_16;
		print_byte( gpio_last_value[(idx *4) + 2] ^ SFR_DATA_8);
		gpio_last_value[(idx *4) + 2] = SFR_DATA_8;
		print_byte( gpio_last_value[(idx *4) + 3] ^ SFR_DATA_0);
		gpio_last_value[(idx *4) + 3] = SFR_DATA_0;
		write_char('\n');
	}
}
#endif

// Identify command
void cmd_parser(void) __banked
{
#ifdef DEBUG
	print_long(ticks);
	print_string("Parsing command\n");
	print_string_x(&cmd_buffer[0]);
	write_char('<'); write_char('\n');
	print_string("CMD-words: ");
	print_byte(cmd_words_len); write_char(' ');
	print_byte(cmd_words_b[0]); write_char(' ');
	print_byte(cmd_words_b[1]); write_char(' ');
	print_byte(cmd_words_b[2]); write_char(' ');
	print_byte(cmd_words_b[3]); write_char(' ');
	print_byte(cmd_words_b[4]); write_char(' ');
	print_byte(cmd_words_b[5]); write_char(' ');
	print_byte(cmd_words_b[6]); write_char('\n');
#endif
	if (cmd_words_len >= 1) {
		if (cmd_compare(0, "get") || cmd_compare(0, "set") || cmd_compare(0, "del")
		    || cmd_compare(0, "diff") || cmd_compare(0, "apply") || cmd_compare(0, "discard")
		    || cmd_compare(0, "do") || cmd_compare(0, "confirm")
		    || cmd_compare(0, "save") || cmd_compare(0, "load")) {
			om_cmd();
		} else if (cmd_compare(0, "sfp")) {
			parse_sfp();
		} else if (cmd_compare(0, "flash")) {
			parse_flash();
		} else if (cmd_compare(0, "dns")) {
			dns_parse();
		} else if (cmd_compare(0, "l2")) {
			if (cmd_compare(1, "forget"))
				port_l2_forget();
			else
				port_l2_learned();
		} else if (cmd_compare(0, "mac")) {
			parse_mac_cmd();
		} else if (cmd_compare(0, "igmp")) {
			if (cmd_compare(1, "on"))
				igmp_enable();
			else if (cmd_compare(1, "off"))
				igmp_setup();
			else if (cmd_compare(1, "show"))
				igmp_show();
			else {
				cmd_error("igmp on|off|show\n");
			}
		} else if (cmd_compare(0, "stp")) {
			stp_parse();
		} else if (cmd_compare(0, "lacp")) {
			if (cmd_compare(1, "off"))	/* the state: get state.lag state.lacp */
				lacp_off();
			else
				cmd_error("lacp off; a group starts LACP with: lag <1-4> lacp <port>...\n");
		} else if (cmd_compare(0, "vlan")) {
			parse_vlan();
		} else if (cmd_compare(0, "mirror")) {
			parse_mirror();
		} else if (cmd_compare(0, "lag")) {
			parse_lag();
		} else if (cmd_compare(0, "laghash")) {
			parse_lag_hash();
		} else if (cmd_compare(0, "sds")) {
			print_reg(RTL837X_REG_SDS_MODES);
			write_char('\n');
#ifdef DEBUG_OBJ
		} else if (cmd_compare(0, "gpio")) {
			print_gpio_status();
		} else if (cmd_compare(0, "leds")) {
			leds_dump();
#endif
		} else if (cmd_compare(0, "loop")) {
			if (cmd_words_len == 2 && cmd_compare(1, "on"))
				rldp_set(1);
			else if (cmd_words_len == 2 && cmd_compare(1, "off"))
				rldp_set(0);
			else if (cmd_words_len == 1 || (cmd_words_len == 2 && cmd_compare(1, "show")))
				rldp_show();
			else
				cmd_error("loop [on|off|show]\n");
		} else if (cmd_compare(0, "regget")) {
			parse_regget();
		} else if (cmd_compare(0, "regset")) {
			parse_regset();
		} else if (cmd_compare(0, "sdsget")) {
			parse_sdsget();
		} else if (cmd_compare(0, "sdsset")) {
			parse_sdsset();
		} else if (cmd_compare(0, "phyget")) {
			parse_phyget();
		} else if (cmd_compare(0, "physet")) {
			parse_physet();
		} else if (cmd_compare(0, "bw")) {
			parse_bw();
		} else if (cmd_compare(0, "storm")) {
			parse_storm();
		} else if (cmd_compare(0, "lldp")) {
			if (cmd_compare(1, "on")) {
				print_string("LLDP enabled\n");
				lldp_enabled = 1;
				trap_rma_set(RTL837X_RMA0E_CONF, RTL837X_RMA_ACT_TRAP);
			} else {
				print_string("LLDP disabled\n");
				if (lldp_enabled)
					lldp_shutdown(lldp_logical_port_status);
				lldp_enabled = 0;
				trap_rma_set(RTL837X_RMA0E_CONF, RTL837X_RMA_ACT_FORWARD);
			}
		}
		else {
			cmd_error("Unknown command\n");
		}


		if (save_cmd && cmd_words_len && err_status == ERR_OK)
			cmd_save();	/* the editor's recall (cmd_editor.c) */
	}
}

// Copy the command in cmd_buffer to the history.
static void cmd_save(void)
{
	// Find end of the cmd-buffer, looking for the NUL-byte.
	uint8_t i = cmd_words_b[cmd_words_len - 1];
	do {
		i++;
	} while(cmd_buffer[i] != NUL);

	cmd_history_ptr = (cmd_history_ptr + i) & CMD_HISTORY_MASK;
	__xdata uint16_t p = cmd_history_ptr;
	cmd_history[cmd_history_ptr] = '\n';
	cmd_history_ptr = (cmd_history_ptr + 1) & CMD_HISTORY_MASK;
	do {
		i--;
		cmd_history[--p & CMD_HISTORY_MASK] = cmd_buffer[i];
	} while (i);
}

void clear_command_history(void) __banked
{
	for (cmd_history_ptr = 0; cmd_history_ptr < CMD_HISTORY_SIZE; cmd_history_ptr++)
		cmd_history[cmd_history_ptr] = 0;
	cmd_history_ptr = 0;
	return;
}


void execute_config(void) __banked
{
	__xdata uint8_t dflt = 0;

	if (loader_recovery()) {
		print_string("Recovery boot: using the default configuration\n");
		dflt = 1;
	}

	save_cmd = 0;

	// The saved config is binary records (om.c, DESIGN.md 15.3), loaded into
	// the candidate. Anything else (blank, bad, old command text) boots on the
	// default config and is left as it is until a save.
	if (om_boot_load(dflt) != E_CFGFORMAT_OK && !dflt)
		om_boot_load(1);
	om_boot_apply();
	// Start saving commands to cmd_history
	clear_command_history();
	save_cmd = 1;
}

/*
 * Dumps as jobs (E1, console.md "Chunked output"). A command whose output
 * grows with a table sets cli_job and its cursor instead of printing it all;
 * cli_job_more() prints the next line. The cli task calls it one line a
 * step (CLI_MORE); any other caller runs the job to its end at once
 * (cmd_finish), as before.
 */
static __xdata uint8_t job_more;

void cli_job_more(void) __banked
{
	switch (cli_job) {
	case CLI_JOB_L2:
		job_more = port_l2_line();
		break;
	case CLI_JOB_OM:
	case CLI_JOB_OMAPPLY:
		job_more = om_line();
		break;
	case CLI_JOB_STPFDB:
		job_more = stp_fdb_line();
		break;
	default:
		job_more = 0;
	}
	if (!job_more)
		cli_job = 0;
}

void cmd_finish(void) __banked
{
	while (cli_job) {
		cli_job_more();
		if (cli_job_sleep) {
			cli_job_sleep = 0;
			delay(2);
		}
	}
}

/*
 * Several commands, one line at a time (a web /cmd's body, from cli).
 * If a command is too long or can't be tokenized, the remaining commands
 * are not executed. exec_line() runs the next command and returns 1, or
 * returns 0 when the text is done, with the status in err_status: the first
 * failure, or the error that stopped it.
 */
static __xdata uint8_t * __xdata ec_p;
static __xdata uint8_t ec_failed, ec_idx, ec_c;

void exec_begin(__xdata uint8_t *p) __banked
{
	ec_p = p;
	ec_failed = ERR_OK;
	err_status = ERR_OK;
}

uint8_t exec_line(void) __banked
{
	ec_idx = 0;
	while (1) {
		ec_c = *ec_p;
		if (!ec_c || ec_c == '\n' || ec_c == '\r') {
			if (ec_idx) {
				cmd_buffer[ec_idx] = NUL;
				cmd_tokenize();
				if (err_status != ERR_OK)
					return 0;
				cmd_parser();
				if (ec_failed == ERR_OK)
					ec_failed = err_status;
				return 1;	/* at the separator: the next call goes on from it */
			}
			if (!ec_c) {
				err_status = ec_failed;
				return 0;
			}
		} else if (ec_idx < (CMD_BUF_SIZE - 1)) {
			cmd_buffer[ec_idx++] = ec_c;
		} else {
			cmd_buffer[CMD_BUF_SIZE - 1] = NUL;
			print_string("ERROR: Command too long: ");
			print_string_x(cmd_buffer);
			write_char('\n');
			err_status = ERR_CMD_TOO_LONG;
			return 0;
		}
		ec_p++;
	}
}
