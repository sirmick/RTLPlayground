
#include <8051.h>
#include <stdint.h>
#include <stdbool.h>

#include "rtl837x_sfr.h"
#include "rtl837x_regs.h"
#include "rtl837x_common.h"
#include "rtl837x_pins.h"
#include "rtl837x_phy.h"
#include "rtl837x_port.h"
#include "machine.h"
#include "phy.h"
#include "boot.h"
#include "sfp.h"
#include "timer.h"

extern __code const struct machine machine;

// SFP1 b0 = 1 => module missing, b1 = 1 => LOS;
// SFP2 b4 = 1 => module missing, b5 = 1 => LOS;

__xdata uint8_t sfp_pins_last;
__xdata char sfp_module_vendor[2][17];
__xdata char sfp_module_model[2][17];
__xdata char sfp_module_serial[2][17];
__xdata uint8_t sfp_options[2];
__xdata uint8_t sfp_buf[16];	/* scratch for one I2C transaction, the controller reads at most 16 bytes */
__xdata uint8_t sfp_speed[2];
__xdata uint8_t sfp_quirks[2];
__xdata struct timer sfp_wake_timer[2];
__xdata uint8_t sfp_wake_pending[2];

/* The module's identity and diagnostics, read by the sfp tasks (tasks.c), a
 * transaction a step, for the web pages (E2): they don't touch I2C. */
__xdata uint8_t sfp_id[2][SFP_ID_LEN];		/* EEPROM bytes 20..59: vendor, OUI, PN, rev */
__xdata uint8_t sfp_diag[2][SFP_DDM_LEN];	/* diagnostics, 0x51 bytes 184..239 */
__xdata uint8_t sfp_id_ok[2], sfp_diag_ok[2];	/* the caches hold this module's */
__xdata uint8_t sfp_ready[2];			/* read and configured: its DDM is kept */
static __xdata uint8_t sfp_rate[2];		/* SFP_READ -> SFP_CONFIG */
static __xdata uint8_t sfp_rd[2], sfp_dd[2];	/* the next transaction of a read, of a DDM refresh */


__code enum sfp_quirk {
	SFP_QUIRK_DDM = (1 << 0),
};

struct sfp_quirk_entry {
	__code const char *vendor; // Set vendor or model to 0 to act as wildcard
	__code const char *model;
	uint8_t quirks;
};

static __code const struct sfp_quirk_entry sfp_quirk_table[] = {
	{ "QSFPTEK", "QT-SFP+-T", SFP_QUIRK_DDM },
};




static inline uint8_t sfp_rate_to_sds_config(uint8_t rate)
{
	if (rate == 0x1 || rate == 0x2)
		return SDS_100FX;
	if (rate == 0xc || rate == 0xd)
		return SDS_1000BX_FIBER;
	if (rate >= 0x19 && rate <= 0x20)  // Ethernet 2.5 GBit
		return SDS_HSG;
	if (rate >= 0x62 /* && rate < 0x70 */) // Force higher rates to 10G (e.g., 25G, 50G)
		return SDS_10GR;

	return 0xff;
}




/* The bytes `sfp` prints that no other cache keeps: encoding, rate (12,
 * as read, not the speed setting's) and options (92, before the quirks) */
static __xdata uint8_t sfp_raw[2][3];
static __xdata uint8_t * __xdata sp;
static __xdata uint8_t si, so;

/* `sfp`'s lines for a present module, from the caches (E2 review): the
 * read's bytes and the DDM cache (refreshed every 2 s), no I2C */
void sfp_print(uint8_t sfp) __banked
{
	if (!sfp_ready[sfp]) {			/* not read (yet), or the read failed */
		print_string(" - I2C read failed on this slot\n");
		return;
	}
	sp = sfp_raw[sfp];
	print_string(" - Rate: "); print_byte(sp[1]);
	print_string("  Encoding: "); print_byte(sp[0]);
	write_char('\n');
	so = sp[2];
	sp = sfp_id[sfp];
	// The Vendor-name, Vendor OUI, Vendor PN and Vendor rev ASCII fields (20..59)
	for (si = 0; si < SFP_ID_LEN; si++) {
		if ((si >= 16 && si < 20) || !sp[si])	// Skip Non-ASCII codes (36..39)
			continue;
		write_char(sp[si]);
	}
	print_string("\nOptions: "); print_byte(so); write_char('\n');
	if (!(sfp_options[sfp] & 0x40))
		return;
	if (!sfp_diag_ok[sfp]) {
		print_string("I2C read failed on this slot\n");
		return;
	}
	sp = sfp_diag[sfp] + 224 - SFP_DDM_FIRST;	/* into sfp_buf: fixed addresses print shorter */
	for (si = 0; si < 16; si++)
		sfp_buf[si] = sp[si];
	print_string("Temp: "); print_byte(sfp_buf[0]); print_byte(sfp_buf[1]); write_char('\n');
	print_string("Vcc: "); print_byte(sfp_buf[2]); print_byte(sfp_buf[3]); write_char('\n');
	print_string("TX Bias: "); print_byte(sfp_buf[4]); print_byte(sfp_buf[5]); write_char('\n');
	print_string("TX Power: "); print_byte(sfp_buf[6]); print_byte(sfp_buf[7]); write_char('\n');
	print_string("RX Power: "); print_byte(sfp_buf[8]); print_byte(sfp_buf[9]); write_char('\n');
	print_string("Laser: "); print_byte(sfp_buf[10]); print_byte(sfp_buf[11]); write_char('\n');
	print_string("State: "); print_byte(sfp_buf[14]); write_char('\n');
}


// Normalize a 16-byte EEPROM field in sfp_buf into dst (17 bytes): unprintable
// characters, quotes and backslashes become '.', trailing spaces go; this
// allows simpler comparisons
static __xdata char * __xdata fd;
static __xdata uint8_t fi, fc;
static void sfp_field(void)
{
	fd[16] = NUL;
	for (fi = 0; fi < 16; fi++) {
		fc = sfp_buf[fi];
		if (fc && (fc < 0x20 || fc > 0x7e || fc == '"' || fc == '\\'))
			fc = '.';
		fd[fi] = fc;
	}
	fi = 16;
	while (fi-- > 0 && (fd[fi] == ' ' || fd[fi] == NUL))
		fd[fi] = NUL;
}



void sfp_apply_quirks(uint8_t sfp) __banked __reentrant
{
	sfp_quirks[sfp] = 0;

	for (uint8_t i = 0; i < sizeof(sfp_quirk_table) / sizeof(*sfp_quirk_table); i++) {
		if (!sfp_quirk_table[i].vendor || !strcmp(sfp_module_vendor[sfp], sfp_quirk_table[i].vendor)) {
			if (!sfp_quirk_table[i].model || !strcmp(sfp_module_model[sfp], sfp_quirk_table[i].model)) {
				sfp_quirks[sfp] |= sfp_quirk_table[i].quirks;
			}
		}
	}

	if (sfp_quirks[sfp] & SFP_QUIRK_DDM) {
		if (!(sfp_options[sfp] & 0x40)) {
			// The module reports that DDM is not implemented, but try a dummy read to confirm
			// 0xff would mean a failed I2C read or an impossible (per spec) voltage greater than 6.5V
			if (sfp_read_block(sfp, 226, 1) && sfp_buf[0] != 0xff) {
				sfp_options[sfp] |= 0x40;
			}
		}
	}
}


/* Inititalize SFP GPIOs */
void setup_sfp_gpio(void) __banked
{
	for (uint8_t sfp = 0; sfp < machine.n_sfp; sfp++) {
		gpio_input_setup(machine.sfp_port[sfp].pin_detect);
		gpio_input_setup(machine.sfp_port[sfp].pin_los);
		gpio_output_setup(machine.sfp_port[sfp].pin_tx_disable, 0);
	}
}



/* The module read after the wake-up delay, a transaction a call (SFP_READ):
 * SFP_MORE, SFP_DONE, or SFP_FAIL (said, and the slot marked empty, so the
 * next poll finds the module again and waits for it once more). The output
 * is the old one-call read's, in its order. */
static __xdata uint8_t sfp, ri, rj, rc, pm;	/* the slot, a transaction, a byte, the slot's pin bit */
static __xdata uint8_t * __xdata rp;
static __xdata uint8_t sfp_ddm_wait[2];	/* polls to the next DDM refresh */

static uint8_t sfp_read_failed(void)
{
	print_string("SFP: an I2C read failed, retrying on the next poll\n");
	sfp_pins_last |= 0x01 << (sfp << 2);
	/* the caches hold nothing of a module that may be gone (E2 review) */
	sfp_ready[sfp] = sfp_id_ok[sfp] = sfp_diag_ok[sfp] = 0;
	return SFP_FAIL;
}

/* the transactions of a module read: rate and encoding, the ASCII fields
 * (3), the options, vendor, model, serial; then the quirks' (at most one) */
static __code const uint8_t sfp_rd_reg[8] = { 11, 16, 32, 48, 92, 20, 40, 68 };
static __code const uint8_t sfp_rd_len[8] = { 2, 16, 16, 16, 1, 16, 16, 16 };

uint8_t sfp_read_next(uint8_t slot) __banked
{
	sfp = slot;
	ri = sfp_rd[sfp]++;
	if (ri == 8) {
		sfp_apply_quirks(sfp);
		return SFP_DONE;
	}
	if (!sfp_read_block(sfp, sfp_rd_reg[ri], sfp_rd_len[ri]))
		return sfp_read_failed();
	if (ri == 0) {
		// Reg 11: Encoding, see SFF-8472 and SFF-8024
		// Reg 12: Signalling rate (including overhead) in 100Mbit: 0xd: 1Gbit, 0x67:10Gbit
		sfp_raw[sfp][0] = sfp_buf[0];
		rc = sfp_raw[sfp][1] = sfp_buf[1];
		rj = sfp_speed[sfp];
		if (rj == SFP_SPEED_100M)
			rc = 0x1;
		else if (rj == SFP_SPEED_1G)
			rc = 0xc;
		else if (rj == SFP_SPEED_2G5)
			rc = 0x19;
		else if (rj == SFP_SPEED_10G)
			rc = 0x69;
		sfp_rate[sfp] = rc;
		print_string("  Rate: "); print_byte(rc);  // Normally 1, but 0 for DAC, can be ignored?
		print_string("  Encoding: "); print_byte(sfp_buf[0]);
		print_string("  Module: ");
	} else if (ri <= 3) {	/* bytes 16..63: the ASCII fields printed, 20..59 kept */
		rp = sfp_id[sfp];
		for (rj = 0; rj < 16; rj++) {
			rc = (ri << 4) + rj;
			if (rc < 20 || rc >= 60)
				continue;
			rp[rc - 20] = sfp_buf[rj];
			if ((rc >= 36 && rc < 40) || !sfp_buf[rj]) // Skip Non-ASCII codes
				continue;
			write_char(sfp_buf[rj]);
		}
		if (ri == 3) {
			sfp_id_ok[sfp] = 1;
			print_string("\n\n");
		}
	} else if (ri == 4) {
		sfp_options[sfp] = sfp_raw[sfp][2] = sfp_buf[0];
	} else {		/* vendor, model, serial: trimmed, for the pages and quirks */
		fd = ri == 5 ? sfp_module_vendor[sfp] : ri == 6 ? sfp_module_model[sfp] : sfp_module_serial[sfp];
		sfp_field();
	}
	return SFP_MORE;
}

/* SFP_CONFIG: the SerDes for the module's rate; its DDM is kept from now on */
void sfp_configure(uint8_t slot) __banked
{
	sfp = slot;
	sds_config(machine.sfp_port[sfp].sds, sfp_rate_to_sds_config(sfp_rate[sfp]));
	sfp_ready[sfp] = 1;
	sfp_dd[sfp] = 0;
	sfp_ddm_wait[sfp] = 0;
}

/* A configured module with DDM (SFF-8472 option bit 6) whose cache is due,
 * every SFP_DDM_POLLS polls (or at once after SFP_CONFIG): 1 */
uint8_t sfp_ddm_wanted(uint8_t slot) __banked
{
	sfp = slot;
	if (!sfp_ready[sfp] || !(sfp_options[sfp] & 0x40) || sfp_ddm_wait[sfp]--)
		return 0;
	sfp_ddm_wait[sfp] = SFP_DDM_POLLS;
	return 1;
}

/* SFP_DDM: the next of the 4 transactions that refresh the slot's cache */
uint8_t sfp_ddm_next(uint8_t slot) __banked
{
	sfp = slot;
	ri = sfp_dd[sfp]++;
	rc = ri == 3 ? SFP_DDM_LEN - 48 : 16;
	if (!sfp_read_block(sfp, SFP_DDM_FIRST + (ri << 4), rc)) {
		sfp_diag_ok[sfp] = 0;
		sfp_dd[sfp] = 0;
		return SFP_FAIL;
	}
	rp = sfp_diag[sfp] + (ri << 4);
	for (rj = 0; rj < rc; rj++)
		rp[rj] = sfp_buf[rj];
	if (ri < 3)
		return SFP_MORE;
	sfp_diag_ok[sfp] = 1;
	sfp_dd[sfp] = 0;
	return SFP_DONE;
}

/* One slot's detect and LOS pins: insertion (the wake-up delay starts),
 * removal, light. 1 when the delay has passed for a module still in: the
 * read starts over (SFP_READ), before the LOS check as in the old poll,
 * which read it right there. */
uint8_t sfp_pins(uint8_t slot) __banked
{
	sfp = slot;
	pm = 0x01 << (sfp << 2);
	if (!gpio_pin_test(machine.sfp_port[sfp].pin_detect)) {
		if (sfp_pins_last & pm) {
			sfp_pins_last &= ~pm;
			print_string("\n<MODULE INSERTED>  Slot: "); write_char('1' + sfp);
			timer_set(&sfp_wake_timer[sfp], SFP_WAKE_TICKS);
			sfp_wake_pending[sfp] = 1;
			sfp_ready[sfp] = sfp_id_ok[sfp] = sfp_diag_ok[sfp] = 0;
		} else if (sfp_wake_pending[sfp] && timer_expired(&sfp_wake_timer[sfp])) {
			sfp_wake_pending[sfp] = 0;
			sfp_rd[sfp] = 0;
			return 1;
		}
	} else if (!(sfp_pins_last & pm)) {
		sfp_pins_last |= pm;
		sfp_wake_pending[sfp] = 0;
		sfp_ready[sfp] = sfp_id_ok[sfp] = sfp_diag_ok[sfp] = 0;
		print_string("\n<MODULE REMOVED>  Slot: "); write_char('1' + sfp); write_char('\n');
	}

	pm <<= 1;
	if (!gpio_pin_test(machine.sfp_port[sfp].pin_los)) {
		if (sfp_pins_last & pm) { // 0x2 0x08
			sfp_pins_last &= ~pm;
			print_string("\n<SFP-RX OK>  Slot: "); write_char('1' + sfp); write_char('\n');
		}
	} else if (!(sfp_pins_last & pm)) {
		sfp_pins_last |= pm;
		print_string("\n<SFP-RX LOS>  Slot: "); write_char('1' + sfp); write_char('\n');
	}
	return 0;
}

