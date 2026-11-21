/*
 * This is a driver implementation for the Port features for the RTL827x platform
 * This code is in the Public Domain
 */

// #define REGDBG
// #define DEBUG

#include <stdint.h>
#include "rtl837x_common.h"
#include "debug.h"
#include "rtl837x_sfr.h"
#include "rtl837x_regs.h"
#include "rtl837x_port.h"
#include "cmd_parser.h"
#include "rtl837x_phy.h"
#include "phy.h"
#include "machine.h"


extern __code const uint8_t * __code const hex;
extern __code const uint16_t bit_mask[16];
extern __code const struct machine machine;
extern __xdata uint8_t sfr_data[4];
extern __xdata struct machine_runtime machine_detected;


__xdata struct vlan_settings vlan_settings;

#ifndef RTLP_HOST_TEST	/* the host tests' register mock has its own (test/hw_mock.c) */
/* reg until bit 0 of its low byte is clear (a table's execute, STAT_GET,
 * I2C): 0; still set after HW_POLL_TICKS: counted, 1. reg_read() inline:
 * a leaf, reg stays in registers. */
uint8_t hw_poll(uint16_t reg) __banked
{
	hw_start(HW_POLL_TICKS);
	do {
		SFR_REG_ADDR_U16 = reg;
		SFR_EXEC_GO = SFR_EXEC_READ_REG;
		do {
		} while (SFR_EXEC_STATUS != 0);
		if (!(SFR_DATA_0 & 0x01))
			return 0;
	} while (!hw_stuck && (int8_t)(*(volatile __xdata uint8_t *)&ticks - hw_end) <= 0);
	hw_timeout(reg);
	return 1;
}
#endif

// Wait for the TABLE ready (bounded, E4)
static uint8_t wait_table_ready(void)	/* 1: it timed out */
{
	return hw_poll(RTL837X_TBL_CTRL);
}

/* An L2 flush's end: FLUSH_CTRL bits 16-23 clear; bounded (E4) */
static void l2_flush_wait(void)
{
	hw_start(HW_FLUSH_TICKS);
	do {
		reg_read(RTL837x_L2_TBL_FLUSH_CTRL);
		if (!SFR_DATA_16)
			return;
	} while (!hw_expired());
	hw_timeout(RTL837x_L2_TBL_FLUSH_CTRL);
}

bool port_ingress_filter(__xdata uint8_t port, __xdata vlan_ingress_mode_t type) __banked
{
	if (port > CPU_LOGICAL_PORT || type >= VLAN_INVALID) {
		dbg_string("Invalid port or ingress filter type\n");
		return false;
	}

	if (type & 0x1) {
		reg_bit_set(RTL837x_REG_INGRESS, port << 1);
	} else {
		reg_bit_clear(RTL837x_REG_INGRESS, port << 1);
	}
	if (type & 0x2) {
		reg_bit_set(RTL837x_REG_INGRESS, (port << 1) + 1);
	} else {
		reg_bit_clear(RTL837x_REG_INGRESS, (port << 1) + 1);
	}
	return true;
}

/*
 * Define a Primary VLAN ID for a port (a LAG's members have one: om_lag.c)
 */
void port_pvid_set(uint8_t port, __xdata uint16_t pvid) __banked
{
	// r4e1c:00001001 R4e1c-000017d0 r6738:00000000 R6738-00000000 (no filtering)
	uint16_t reg = RTL837x_PVID_BASE_REG + ((port >> 1) << 2);

	reg_read_m(reg);
	if (port & 0x1) {
		REG_WRITE(reg, sfr_data[0], pvid >> 4, (sfr_data[2] & 0x0f) | (pvid << 4), sfr_data[3]);
	} else {
		REG_WRITE(reg, sfr_data[0], sfr_data[1], (sfr_data[2] & 0xf0) | (pvid >> 8), pvid);
	}
}

uint16_t port_pvid_get(uint8_t port) __banked
{
	uint16_t reg = RTL837x_PVID_BASE_REG + ((port >> 1) << 2);
	reg_read_m(reg);
	if (port & 0x1) {
		return (sfr_data[1] << 4) | (sfr_data[2] >> 4);
	} else {
		return ((sfr_data[2] & 0x0f) << 8) | sfr_data[3];
	}
}


void vlan_delete(uint16_t vlan) __banked
{
	if (!vlan || vlan >= 0xfff)
		return;

	REG_WRITE(RTL837x_TBL_DATA_IN_A, 0, 0, 0, 0);
	REG_WRITE(RTL837X_TBL_CTRL, vlan >> 8, vlan, TBL_VLAN, TBL_WRITE | TBL_EXECUTE);
	wait_table_ready();	/* E4: the next command must not find it busy (rtlsim: dropped) */
}


/*
 * Reads VLAN information from VLAN table
 * Returns data in sfr_data
 */
int8_t vlan_get(uint16_t vlan) __banked
{
	if (vlan >= 0xfff) // VLAN 4095 is special
		return -1;

	REG_WRITE(RTL837X_TBL_CTRL, vlan >> 8, vlan, TBL_VLAN, TBL_EXECUTE);

	if (wait_table_ready())		/* E4: the table didn't answer */
		return -1;

	reg_read_m(RTL837x_L2_DATA_OUT_A);

	return 0;
}


/*
 * Create a VLAN
 * The arguments are passed in global structure vlan_settings
 * A member that is not tagged, is untagged
 */
void vlan_create(void) __banked
{
	if (!vlan_settings.vlan || vlan_settings.vlan >= 0xfff) {
		dbg_string("\nInvalid VLAN: "); dbg_short(vlan_settings.vlan); dbg_char('\n');
		return;
	}

	// For now, the CPU-port is always a tagged member:
	vlan_settings.members |= 0x0200; // Set 10th bit
	vlan_settings.tagged |= 0x0200;

	uint16_t a = (~vlan_settings.members) ^ vlan_settings.tagged ^ vlan_settings.members;

	// On RTL8372, port-bits 0-2 must be 0, although they are not members
	if (!machine_detected.isRTL8373) {
		a &= 0x1f8;
		vlan_settings.tagged &= 0x3f8;
	}

	// Initialize VLAN table with VLAN 1
	REG_WRITE(RTL837x_TBL_DATA_IN_A, 0x02, (a >> 6) & 0x0f, (a << 2) | (vlan_settings.members >> 8), vlan_settings.members);
	REG_WRITE(RTL837X_TBL_CTRL, vlan_settings.vlan >> 8, vlan_settings.vlan, TBL_VLAN, TBL_WRITE | TBL_EXECUTE);

	wait_table_ready();
}


static __xdata uint16_t vb_v;

/*
 * Configures a default VLAN 1 and enables 4k VLAN tables
 * All ports are made members of the VLAN and VLAN filtering
 * is enabled on all ports
 * PVID is set to 1 for all ports
 * Called upon reboot
 */
void vlan_setup(void) __banked
{

	// Initialize VLAN table for VLAN 1, by disabling that entry
	REG_SET(RTL837x_TBL_DATA_IN_A, machine_detected.isRTL8373? 0x0007ffff : 0x0007e3f8);

	REG_SET(RTL837X_TBL_CTRL, 0x00010303);

	wait_table_ready();

	// Set PVID 1 for every port. TODO: Skip unused ports!
	for (uint8_t i = MACHINE_MIN_PORT; i <= CPU_LOGICAL_PORT; i++) { // CPU MAC is independent of the last user port
		uint16_t reg = RTL837x_PVID_BASE_REG + ((i >> 1) << 2);
#ifdef DEBUG
		print_byte(i); write_char(':'); write_char(' '); print_short(reg); write_char('=');
		print_short(reg);  write_char(' ');
#endif
		reg_read_m(reg);
		if (i & 0x1) {
			REG_WRITE(reg, sfr_data[0], 0, (sfr_data[2] & 0x0f) | 0x10, sfr_data[3]);
		} else {
			REG_WRITE(reg, sfr_data[0], sfr_data[1], sfr_data[2] & 0xf0, 0x01);
		}
#ifdef DEBUG
		reg_read_m(reg);
		write_char(' '); write_char('A'); write_char('>'); print_sfr_data();
#endif

		// EGRESS filtering for port: removal of additional VLAN tag (mode 0x3 for each port)
		reg_bit_clear(RTL837X_VLAN_PORT_EGR_TAG, i << 1);
		reg_bit_clear(RTL837X_VLAN_PORT_EGR_TAG, (i << 1) + 1);
		// Enable INGRESS filtering for port: discard packets not belonging to member VLAN on that port
		port_ingress_vlan_filter_set(i, true);

#ifdef DEBUG
		print_string("\n");
#endif
	}

	// Ingress filtering. 2 bits per port: allow tagged (01) / untagged (10) and all (00)
	REG_SET(RTL837x_REG_INGRESS, 0); // No filtering for all ports

	// Enable 4k VLAN
	REG_SET(RTL837X_VLAN_CTRL, VLAN_CVLAN_FILTER);
	REG_SET(RTL837X_VLAN_L2_LRN_DIS_0, 0);
	REG_SET(RTL837X_VLAN_L2_LRN_DIS_1, 0);

	// Enable VLAN 1: user ports untagged, CPU MAC9 tagged (untag bit 19 clear)
	REG_SET(RTL837x_TBL_DATA_IN_A, machine_detected.isRTL8373? 0x0207ffff : 0x0207e3f8);	// 02: Entry valid, 7...: membership

	REG_SET(RTL837X_TBL_CTRL, 0x00010303);	// Write VLAN 1

	wait_table_ready();

	/* every other VLAN out of the table: a reset can leave entries valid, and
	 * the config (vlan.V) is what the table holds */
	REG_WRITE(RTL837x_TBL_DATA_IN_A, 0, 0, 0, 0);
	vb_v = 2;
	do {
		REG_WRITE(RTL837X_TBL_CTRL, vb_v >> 8, vb_v, TBL_VLAN, TBL_WRITE | TBL_EXECUTE);
		if (wait_table_ready())	/* a stuck table: stop, the boot stays bounded (F1b) */
			break;
	} while (++vb_v != 0xfff);

#ifdef DEBUG
	print_string("\nvlan_setup, REG 0x6738: "); print_reg(0x6738);
	print_string("\nvlan_setup, REG 0x4e10: "); print_reg(0x4e10);
	print_string("\nvlan_setup, REG 0x4e18: "); print_reg(0x4e18);
	print_string("\nvlan_setup, REG 0x4e14: "); print_reg(0x4e14);
	print_string("\nvlan_setup, REG 0x4e30: "); print_reg(0x4e30);
	print_string("\nvlan_setup, REG 0x4e34: "); print_reg(0x4e34);
	print_string("\nvlan_setup, REG 0x4f48: "); print_reg(0x4f48);
	print_string("\nvlan_setup, REG 0x4f4c: "); print_reg(0x4f4c);
#endif

}


/*
 * Forget the dynamic L2 entries learned on one port.
 */
void port_l2_forget_port(uint8_t port) __banked
{
	REG_SET(RTL837x_L2_TBL_FLUSH_CNF, 0x0);	/* port-based, dynamic entries */
	REG_SET(RTL837x_L2_TBL_FLUSH_CTRL, L2_TBL_FLUSH_EXEC | (((uint16_t)1) << port));
	l2_flush_wait();
}


/*
 * Forget all dynamic L2 learned entries
 */
uint8_t port_l2_forget(void) __banked
{
	// Configure the entries to be flushed:
	// port-based (bits 0-1 are 0 and dynamic entries, bit 2 specifies dynamic entries
	REG_SET(RTL837x_L2_TBL_FLUSH_CNF, 0x0);

	// Flush L2 table for all ports by setting the ports and the flush-exec bit (bit 16)
	uint16_t mask = PMASK_6;
	if (machine_detected.isRTL8373)
		mask = PMASK_9;
	REG_SET(RTL837x_L2_TBL_FLUSH_CTRL, L2_TBL_FLUSH_EXEC | mask);

	// Wait for flush completed
	l2_flush_wait();

	return 0;
}


/*
 * Static L2 multicast entry for the link-local group 01:80:C2:00:00:<mac_last>
 * in VLAN `vid`, with member portmask `pmask` (bit 9 = CPU port).
 */

void port_l2mc_set(uint8_t mac_last, __xdata uint16_t vid, __xdata uint16_t pmask) __banked
{
	wait_table_ready();

	REG_WRITE(RTL837x_TBL_DATA_IN_A, 0xc2, 0x00, 0x00, mac_last);
	REG_WRITE(RTL837x_TBL_DATA_IN_B, 0x20 | (vid >> 8) | ((pmask & 0x3) << 6), vid, 0x01, 0x80);
	REG_WRITE(RTL837x_TBL_DATA_IN_C, 0, 0, 0, pmask >> 2);
	REG_WRITE(RTL837X_TBL_CTRL, 0, 0, TBL_L2_UNICAST, TBL_WRITE | TBL_EXECUTE);

	wait_table_ready();
}


/*
 * Basic L2 configuration such as time to forget an entry
 */
void port_l2_setup(void) __banked
{
	// A soft reset keeps the L2 table, its static entries too (a management
	// entry for another MAC or VLAN): the boot flushes both kinds, the CPU's
	// with them (L2_TBL_FLUSH_MODE FLUSH_TYPE, bit 2). Later flushes
	// (port_l2_forget) take only the dynamic ones.
	REG_SET(RTL837x_L2_TBL_FLUSH_CNF, 0x4);
	REG_SET(RTL837x_L2_TBL_FLUSH_CTRL, L2_TBL_FLUSH_EXEC | PMASK_CPU | (machine_detected.isRTL8373 ? PMASK_9 : PMASK_6));
	l2_flush_wait();

	for (uint8_t i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++) {
		// Limit the number of automatically learned MAC-Entries per port to 0x1040
		uint8_t idx = (i << 2);
		REG_SET(RTL837X_L2_LRN_PORT_CONSTRAINT + idx, 0x00001040);

		// All ports may communicate with each other and CPU-Port
		uint16_t mask = PMASK_CPU | PMASK_6;
		if (machine_detected.isRTL8373)
			mask = PMASK_CPU | PMASK_9;
		REG_SET(RTL837X_PORT_ISOLATION_BASE + idx, mask);
	}
	// When maximim entries learned, then simply flood the packet
	reg_bit_set(RTL837X_L2_LRN_PORT_CONSTRT_ACT, 0);

}


uint16_t port_link_mask(void) __banked
{
	reg_read_m(RTL837X_REG_LINKS_STS);
	return ((uint16_t)sfr_data[2] << 8) | sfr_data[1];
}

void port_isolate(uint8_t port, __xdata uint16_t pmask) __banked
{
	if (port <= MACHINE_MAX_PORT)
		REG_SET(RTL837X_PORT_ISOLATION_BASE + (port << 2), pmask);
}



void port_eee_enable(__xdata uint8_t port,__xdata uint8_t speed) __banked
{

	if (machine.is_sfp[port])
		return;

	REG_SET(RTL837X_EEE_CTRL_BASE + (port << 8), EEE_RX_ENABLE | EEE_TX_ENABLE);
	// Enable all speeds up to the specified speed
	if (speed & EEE_100) {
			// Enable EEE advertisement for 100BASE-T via EEE Advertisement Reg
			phy_write(port, PHY_MMD_AN, PHY_EEE_ADV, PHY_EEE_BIT_100M);
			return;
	}
	if (speed & EEE_1000) {
			// Disable EEE advertisement for 2.5GBASE-T via EEE Advertisement Reg 2
			phy_write(port, PHY_MMD_AN, PHY_EEE_ADV2, 0);
			// Enable EEE advertisement for 100/1000BASE-T via EEE Advertisement Reg
			phy_write(port, PHY_MMD_AN, PHY_EEE_ADV, PHY_EEE_BIT_1G | PHY_EEE_BIT_100M);
			return;
	}
	if (speed & EEE_2G5) {
			// Enable EEE advertisement for 100/1000BASE-T via EEE Advertisement Reg
			phy_write(port, PHY_MMD_AN, PHY_EEE_ADV, PHY_EEE_BIT_1G | PHY_EEE_BIT_100M);
			// Enable EEE advertisement for 2.5GBASE-T via EEE Advertisement Reg 2
			phy_write(port, PHY_MMD_AN, PHY_EEE_ADV2, PHY_EEE_BIT_2G5);
			return;
	}
	if (speed & EEE_5G) {
			// Enable EEE advertisement for 100/1000BASE-T via EEE Advertisement Reg
			phy_write(port, PHY_MMD_AN, PHY_EEE_ADV, PHY_EEE_BIT_1G | PHY_EEE_BIT_100M);
			// Enable EEE advertisement for 2.5GBASE-T via EEE Advertisement Reg 2
			phy_write(port, PHY_MMD_AN, PHY_EEE_ADV2, PHY_EEE_BIT_2G5 | PHY_EEE_BIT_5G);
			return;
	}
	if (speed & EEE_10G) {
			// Enable EEE advertisement for 100/1000BASE-T via EEE Advertisement Reg
			phy_write(port, PHY_MMD_AN, PHY_EEE_ADV, PHY_EEE_BIT_10G | PHY_EEE_BIT_1G | PHY_EEE_BIT_100M);
			// Enable EEE advertisement for 2.5GBASE-T via EEE Advertisement Reg 2
			phy_write(port, PHY_MMD_AN, PHY_EEE_ADV2, PHY_EEE_BIT_2G5 | PHY_EEE_BIT_5G);
			return;
	}

}


static __xdata uint8_t eee_speed;	/* port_eee_enable_all()'s */

/* all ports' speed: the 10G ports' own; 2.5G for the others' 10G */
static uint8_t eee_port_speed(uint8_t i)
{
	if ((i == 3 && machine.n_10g) || (i == 8 && machine.n_10g == 2) || !(eee_speed & EEE_10G))
		return eee_speed;
	return (eee_speed & EEE_NORESET) | EEE_2G5;
}

/* at boot (EEE_NORESET): no PHY reset */
void port_eee_enable_all(__xdata uint8_t speed) __banked
{
	eee_speed = speed;
	for (uint8_t i = MACHINE_MIN_PORT; i <= MACHINE_MAX_PORT; i++)
		port_eee_enable(i, eee_port_speed(i));
}



/*
 * Reads the member port bitmask of a Link Aggregation Group.
 * The groups have numbers 0-3; bit n is set when logical port n is a member.
 * The bitmask reflects what the hardware holds, so it covers groups set up
 * statically and groups a protocol brought up, without either having to say so.
 */
uint16_t port_lag_members_get(uint8_t lag) __banked
{
	reg_read(RTL837X_TRK_MBR_CTRL_BASE + (lag << 2));
	return ((uint16_t)SFR_DATA_8 << 8) | SFR_DATA_0;
}

uint8_t port_lag_of(uint8_t port) __banked
{
	for (uint8_t lag = 0; lag < 4; lag++)
		if ((port_lag_members_get(lag) >> port) & 1)
			return lag;
	return PORT_LAG_NONE;
}


/*
 * A Link Aggregation Group's members (groups 0-3): bit n logical port n.
 * Its hash is lag.L.hash's (om_lag.c).
 */
void port_lag_members_set(__xdata uint8_t lag, __xdata uint16_t members) __banked
{
	REG_WRITE(RTL837X_TRK_MBR_CTRL_BASE + (lag << 2), 0, 0, members >> 8, members & 0xff);
}

/** Set the ingress VLAN filtering */
bool port_ingress_vlan_filter_set(uint8_t port, __xdata bool enabled) __banked
{
	if (!MACHINE_PORT_VALID(port) && port != CPU_LOGICAL_PORT) {
		return false;
	}
	if (enabled)
		reg_bit_set(RTL837X_VLAN_PORT_IGR_FLTR, port);
	else
		reg_bit_clear(RTL837X_VLAN_PORT_IGR_FLTR, port);

	return true;
}

// C1 bit 0: static (no aging); age at C3[4:2] must be non-zero or the
// entry is invisible to lookup
static void l2_mgmt_entry_fill(__xdata uint8_t *mac, __xdata uint16_t vlan)
{
	REG_WRITE(RTL837x_TBL_DATA_IN_A, mac[2], mac[3], mac[4], mac[5]);
	REG_WRITE(RTL837x_TBL_DATA_IN_B, 0x20 | ((CPU_LOGICAL_PORT & 0x3) << 6) | (vlan >> 8),
		  vlan & 0xff, mac[0], mac[1]);
	REG_WRITE(RTL837x_TBL_DATA_IN_C, 0x00, 0x01, 0x00, (7 << 2) | (CPU_LOGICAL_PORT >> 2));
}

static void l2_mgmt_tbl_prepare(void)
{
	reg_read_m(RTL837x_TBL_DATA_0);
	sfr_data[2] &= 0x3f;
	sfr_data[1] &= 0xf8;
	reg_write_m(RTL837x_TBL_DATA_0);
}

/** Pin the management MAC to the CPU port; remove_entry deletes it */
void port_l2_static_mgmt(__xdata uint8_t *mac, __xdata uint16_t vlan, __xdata bool remove_entry) __banked
{
	wait_table_ready();

	// vlan 0 = management VLAN disabled: untagged management resolves in
	// VLAN 1; clients in other VLANs keep their learned entry and age as before
	if (!vlan)
		vlan = 1;
	l2_mgmt_entry_fill(mac, vlan);
	l2_mgmt_tbl_prepare();
	if (remove_entry) {
		// a search miss returns index 0, not a slot
		REG_WRITE(RTL837X_TBL_CTRL, 0x00, 0x00, TBL_L2_UNICAST, TBL_EXECUTE);
		wait_table_ready();
		reg_read_m(RTL837x_TBL_DATA_0);
		if (!(sfr_data[2] & 0x10))
			return;
		sfr_data[1] |= 0x04; // CLEAR-entry
		reg_write_m(RTL837x_TBL_DATA_0);
		__xdata uint16_t idx = (((uint16_t)sfr_data[2] & 0x0f) << 8) | sfr_data[3];
		REG_WRITE(RTL837X_TBL_CTRL, idx >> 8, idx & 0xff, TBL_L2_UNICAST, TBL_WRITE | TBL_EXECUTE);
	} else {
		// method-0 write: the hardware hashes the key and places the entry
		REG_WRITE(RTL837X_TBL_CTRL, 0x00, 0x00, TBL_L2_UNICAST, TBL_WRITE | TBL_EXECUTE);
	}

	wait_table_ready();
}
