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
#include "rtl837x_igmp.h"
#include "rtl837x_trap.h"
#include "rtl837x_cabletest.h"
#include "dhcp.h"
#include "sessions.h"
#include "dns.h"
#include "ntp.h"
#include "uip/uip.h"

#include "machine.h"
#include "rtl837x_leds.h"
#include "phy.h"
#include "con_ring.h"
#include "sched.h"
#include "om.h"
#include "om_gen.h"


extern __code const struct machine machine;
extern __code const uint8_t log_to_phys_port[9];

extern volatile __xdata uint32_t ticks;
extern volatile __xdata uint8_t sfr_data[4];

extern __code const uint8_t * __code const greeting;
extern __code const uint8_t * __code const hex;

extern __xdata uint8_t flash_buf[FLASH_BUF_SIZE];
extern __xdata struct flash_region_t flash_region;



extern __xdata struct dhcp_state dhcp_state;


extern __xdata uint16_t management_vlan;
extern __xdata struct uip_eth_addr uip_ethaddr;

// Temporatly for str to hex convertion value.
// Support up to 32_bits.
#if OM_DBG
__xdata uint8_t hexvalue[4] = { 0 };
__xdata uint8_t atoi_results_u8;
#endif


// Buffer for writing to flash 0x1fd000, copy to 0x1fe000
__xdata uint8_t cmd_buffer[CMD_BUF_SIZE];

__xdata	char save_cmd;

__xdata uint8_t mac_parse_result[6];

// These variables combined create a Fixed-capacity vector/bounded buffer.
// `N_WORDS`: The total number of command arguments that can be tracked.
// `cmd_words_len` stores the number of arguments found inside `cmd_buffer`
// `cmd_words_b` stores the index into `cmd_buffer`, check `cmd_words_len` is index is valid.
#define N_WORDS 15
__xdata uint8_t cmd_words_len;
__xdata uint8_t cmd_words_b[N_WORDS];

__xdata uint8_t cmd_history[CMD_HISTORY_SIZE];
__xdata uint8_t cmd_history_ptr;	/* 8 bits: CMD_HISTORY_SIZE is 256 */
__xdata uint8_t cli_job;		/* a dump in progress (cli_job_more) */
__xdata uint8_t cli_job_sleep;
__xdata uint8_t cli_job_one;
static void cmd_save(void);

// Error set by commands
__xdata uint8_t err_status;

#if OM_DBG	/* the raw pokes' parsers (OM_DBG=0: release) */
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
#endif

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


#if OM_DBG
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
#endif


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

#if OM_DBG
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

#endif

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
		if (om_cmd()) {
		} else if (cmd_compare(0, "sfp")) {
			cmd_error("\nUsage: get state.sfp; a cage's speed: port.N.speed\n");
		} else if (cmd_compare(0, "dns")) {
			dns_parse();
		} else if (cmd_compare(0, "mac")) {
			parse_mac_cmd();
#if OM_DBG
		} else if (cmd_compare(0, "flash")) {
			parse_flash();
#if OM_DBG_LEDS
		} else if (cmd_compare(0, "leds")) {
			leds_dump();
#endif
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
#endif
		} else {
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
	cmd_history_ptr = 0;
	do
		cmd_history[cmd_history_ptr] = 0;
	while (++cmd_history_ptr);		/* 8 bits: all 256 */
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
	case CLI_JOB_OM:
	case CLI_JOB_OMAPPLY:
		job_more = om_line();
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
