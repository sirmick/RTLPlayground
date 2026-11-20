/*
 * Loop detection with the RTL837x's RLDP engine.
 *
 * The ASIC sends a probe on every port once a period and reports a port whose
 * probe comes back, and the port it came back on, but it blocks nothing.
 * While STP is off, rldp_tick() blocks one port of each looped pair, the
 * higher one, and unblocks it when the loop is gone; the probes still cross a
 * blocked port, so the engine keeps seeing the loop until it is removed.
 * While STP is on, STP owns the port states and a loop is only reported.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "rtl837x_sfr.h"
#include "rtl837x_rldp.h"
#include "machine.h"
#include "om_gen.h"

extern __xdata uint8_t sfr_data[4];

#define RLDP_PERIOD_MS	500

__xdata uint8_t rldp_enabled;
__xdata uint16_t rldp_looped;		/* LOOPED at the last tick */
__xdata uint16_t rldp_blocked;		/* ports this module put in blocking */


static uint16_t reg_read_u16(uint16_t reg)
{
	reg_read_m(reg);
	return ((uint16_t)sfr_data[2] << 8) | sfr_data[3];
}


/* Sets the STP state of a port: 0b11 forwarding, 0b01 blocking */
static void port_state(uint8_t port, uint8_t state)
{
	reg_read_m(RTL837X_MSTP_STATES);
	sfr_data[3 - (port >> 2)] &= ~(uint8_t)(0b11 << ((port << 1) & 0x7));
	sfr_data[3 - (port >> 2)] |= (uint8_t)(state << ((port << 1) & 0x7));
	reg_write_m(RTL837X_MSTP_STATES);
}


static uint8_t loop_pair(uint8_t port)
{
	reg_read_m(RTL837X_RLDP_LOOP_PAIR + ((port >> 3) << 2));
	return (sfr_data[3 - ((port & 7) >> 1)] >> ((port & 1) << 2)) & 0x0f;
}


static void unblock_all(void)
{
	for (uint8_t p = MACHINE_MIN_PORT; p <= MACHINE_MAX_PORT; p++) {
		if (rldp_blocked & (1 << p))
			port_state(p, 0b11);
	}
	rldp_blocked = 0;
}


/* loop (schema/loop.py) */
uint8_t om_apply_loop(uint8_t idx) __banked
{
	__xdata uint16_t pmask = 0;

	(void)idx;
	if (cfg_run.loop.enabled) {
		if (!rldp_enabled)
			rldp_blocked = 0;
		for (uint8_t p = MACHINE_MIN_PORT; p <= MACHINE_MAX_PORT; p++)
			pmask |= 1 << p;
		REG_SET(RTL837X_RLDP_PERIOD, ((uint32_t)RLDP_PERIOD_MS << 16) | RLDP_PERIOD_MS);
		REG_SET(RTL837X_RLDP_TX_PMSK, pmask);
		/* Read-modify-write: get_random_32() uses RLDP_RND_EN in here */
		reg_read_m(RTL837X_RLDP_RLPP);
		sfr_data[3] |= RLDP_EN | RLDP_COMP_ID | (1 << RLDP_RND_EN) | RLDP_PERIODIC;
		reg_write_m(RTL837X_RLDP_RLPP);
	} else {
		reg_read_m(RTL837X_RLDP_RLPP);
		sfr_data[3] &= ~(RLDP_EN | RLDP_PERIODIC);
		reg_write_m(RTL837X_RLDP_RLPP);
		if (!stp_enabled)
			unblock_all();
		rldp_blocked = 0;
	}
	rldp_looped = 0;
	REG_SET(RTL837X_RLDP_LOOP, 0x3ff);
	REG_SET(RTL837X_RLDP_LEAVE_LOOP, 0x3ff);
	rldp_enabled = cfg_run.loop.enabled;
	return OM_OK;
}


/* Once a second while loop detection is on */
void rldp_tick(void) __banked
{
	__xdata uint16_t looped = reg_read_u16(RTL837X_RLDP_LOOPED) & 0x3ff;
	__xdata uint16_t block = 0;

	for (uint8_t p = MACHINE_MIN_PORT; p <= MACHINE_MAX_PORT; p++) {
		if (!(looped & (1 << p)))
			continue;
		uint8_t q = loop_pair(p);
		if (q > p)
			continue;		/* the pair's other entry blocks q */
		block |= 1 << p;
		if (!(rldp_looped & (1 << p))) {
			print_string("Loop: port "); print_phys_port(q);
			if (q != p) {
				print_string(" to port "); print_phys_port(p);
			}
			print_string(stp_enabled ? " (STP is on: not blocked)\n" : ", blocking port ");
			if (!stp_enabled) {
				print_phys_port(p); write_char('\n');
			}
		}
	}
	for (uint8_t p = MACHINE_MIN_PORT; p <= MACHINE_MAX_PORT; p++) {
		if ((rldp_looped & (1 << p)) && !(looped & (1 << p))) {
			print_string("Loop on port "); print_phys_port(p); print_string(" gone\n");
		}
	}
	rldp_looped = looped;

	if (stp_enabled) {
		/* stp_setup() wrote every port state; nothing is ours any more */
		rldp_blocked = 0;
		return;
	}
	for (uint8_t p = MACHINE_MIN_PORT; p <= MACHINE_MAX_PORT; p++) {
		uint16_t m = 1 << p;
		if ((block & m) && !(rldp_blocked & m))
			port_state(p, 0b01);
		else if (!(block & m) && (rldp_blocked & m))
			port_state(p, 0b11);
	}
	rldp_blocked = block;
}


/* state.loop (schema/state_svc.py): none while detection is off */
uint8_t om_get_loop(uint8_t idx) __banked
{
	(void)idx;
	om_st.loop.looped = rldp_enabled ? rldp_looped : 0;
	om_st.loop.blocked = rldp_enabled ? rldp_blocked : 0;
	return OM_OK;
}
