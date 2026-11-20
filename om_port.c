/*
 * om_port.c - port.N for the object model (schema/port.py): apply a port's
 * config to the hardware, check it, read its state, start its cable test.
 * The hardware calls are the ones the old port, mtu, pvid, ingress, isolate
 * and eee commands made.
 *
 * The instance is copied into fixed xdata structs first: SDCC's code for a
 * fixed address is a fraction of its code through a pointer.
 */
#include <stdint.h>
#include <stdbool.h>
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "rtl837x_sfr.h"
#include "rtl837x_phy.h"
#include "rtl837x_port.h"
#include "rtl837x_lldp.h"
#include "rtl837x_cabletest.h"
#include "phy.h"
#include "machine.h"
#include "om_gen.h"

extern __code const struct machine machine;
extern __xdata uint8_t sfr_data[4];
extern __xdata bool lldp_enabled;

static __xdata struct cfg_port r;	/* running: what to program */
static __xdata struct cfg_port o;	/* what the hardware had */
static __xdata uint8_t b, l, chg;

static __code const uint8_t speed_phy[] = {
	PHY_SPEED_AUTO, PHY_SPEED_10M, PHY_SPEED_100M, PHY_SPEED_1G,
	PHY_SPEED_2G5, PHY_SPEED_5G, PHY_SPEED_10G
};
static __code const uint8_t duplex_phy[] = { PHY_DUPLEX_BOTH, PHY_DUPLEX_FULL, PHY_DUPLEX_HALF };
static __code const uint8_t ingress_hw[] = { VLAN_ALL, VLAN_TAGGED, VLAN_UNTAGGED };

uint8_t om_apply_port(uint8_t idx) __banked
{
	l = idx;
	if (!MACHINE_PORT_VALID(l))	/* a logical port this board hasn't (the boot applies them all) */
		return OM_OK;
	memcpy(&r, &cfg_run.port[l], sizeof(r));
	memcpy(&o, &cfg_roll.port[l], sizeof(o));
	if (om_full)			/* the boot: every field written */
		memset(&o, 0xff, sizeof(o));

	if (!machine.is_sfp[l]) {
		chg = r.eee != o.eee;
		if (chg) {
			if (r.eee)
				port_eee_enable(l, OM_TEN_G(l) ? EEE_10G : EEE_2G5);
			else
				port_eee_disable(l);
		}
		if (r.enabled != o.enabled)
			chg = 1;
		if (r.speed != o.speed)
			chg = 1;
		if (r.duplex != o.duplex)
			chg = 1;
		if (chg) {	/* the speed again, which restarts autonegotiation */
			phy_settings.port = l;
			phy_settings.is10g_port = OM_TEN_G(l);
			phy_settings.duplex = duplex_phy[r.duplex];
			phy_settings.speed = r.enabled ? speed_phy[r.speed] : PHY_OFF;
			phy_set_speed();
		}
	}
	if (r.mtu != o.mtu)
		REG_WRITE(RTL8373_REG_MAC_L2_PORT_MAX_LEN + ((uint16_t)l << 8), (r.mtu >> 10) & 0xf,
			  (r.mtu >> 2) & 0xff, ((r.mtu & 0x3) << 6) | ((r.mtu >> 8) & 0x3f), r.mtu & 0xff);
	if (r.pvid != o.pvid)
		port_pvid_set(l, r.pvid);
	if (r.ingress != o.ingress)
		port_ingress_filter(l, ingress_hw[r.ingress]);
	if (r.ingress_filter != o.ingress_filter)
		port_ingress_vlan_filter_set(l, r.ingress_filter);
	if (r.isolate != o.isolate)
		port_isolate(l, r.isolate);
	if (r.lldp != o.lldp) {
		if (r.lldp == E_LLDP_BLOCK) {
			if (lldp_enabled)
				lldp_shutdown((uint16_t)1 << l);
			lldp_logical_port_status &= ~((uint16_t)1 << l);
		} else {
			lldp_logical_port_status |= (uint16_t)1 << l;
		}
	}
	return OM_OK;
}

uint8_t om_check_port(uint8_t idx) __banked
{
	l = idx;
	memcpy(&r, &cfg_cand.port[l], sizeof(r));
	if (!om_vlan_known(r.pvid)) {
		if (om_booting) {	/* boot: VLAN 1, and VLAN 1 back if it is gone */
			cfg_cand.port[l].pvid = 1;
			om_boot_note(OM_E_PVID_ONE);
			if (om_vlan_known(1))
				return OM_OK;
			om_boot_ref = O_CFG_VLAN;
			return OM_ERR;
		}
		om_fail(OM_E_PVID);
		return OM_ERR;
	}
	if (machine.is_sfp[l])
		return OM_OK;
	if (r.duplex != E_DUPLEX_AUTO && r.speed != E_SPEED_10M && r.speed != E_SPEED_100M) {
		om_fail(OM_E_DUPLEX);
		return OM_ERR;
	}
	if ((r.speed == E_SPEED_5G || r.speed == E_SPEED_10G) && !OM_TEN_G(l)) {
		om_fail(OM_E_SPEED_10G);
		return OM_ERR;
	}
	return OM_OK;
}

/* port l's bit in a status register read as sfr_data[] (big endian) */
static uint8_t bit_of(void)
{
	return (sfr_data[3 - (l >> 3)] >> (l & 7)) & 1;
}

/* a STAT word, little endian, at dst */
static __xdata uint8_t * __xdata dst;
static void stat_word(uint16_t reg)
{
	reg_read_m(reg);
	for (b = 0; b < 4; b++)
		dst[b] = sfr_data[3 - b];
}

/* a 64-bit counter: its low word, then its high word */
static void stat_pair(void)
{
	stat_word(RTL837X_STAT_V_LOW);
	dst += 4;
	stat_word(RTL837X_STAT_V_HIGH);
}

extern __xdata uint8_t ct_state, ct_port, ct_valid, ct_have;
extern __xdata uint8_t ct_result[4];
extern __xdata uint16_t ct_len[4];
static __xdata uint8_t ct_r[4];
static __xdata uint16_t ct_l[4];

#define S	om_st.port

uint8_t om_get_port(uint8_t idx) __banked
{
	l = idx;
	S.media = machine.is_sfp[l] ? E_MEDIA_SFP : E_MEDIA_COPPER;
	reg_read_m(RTL837X_REG_LINKS_STS);
	S.link = (sfr_data[(l >> 3) + 1] >> (l & 7)) & 1;
	S.speed = E_LSPEED_UNKNOWN;
	S.duplex = E_LDUPLEX_FULL;
	S.ms_role = E_ROLE_SLAVE;
	if (S.link) {
		reg_read_m(l < 8 ? RTL837X_REG_LINKS : RTL837X_REG_LINKS_89);
		b = sfr_data[3 - ((l & 7) >> 1)];
		if (l & 1)
			b >>= 4;
		b &= 0xf;
		if (b < E_LSPEED_UNKNOWN)
			S.speed = b;
		if (!machine.is_sfp[l]) {
			phy_read(l, PHY_MMD31, PHY_MMD31_PHYSR);
			S.duplex = (SFR_DATA_U16 >> 3) & 1;
			reg_read_m(RTL837X_MAC_MSTR_SLV_STS);
			S.ms_role = bit_of();
		}
	}
	reg_read_m(RTL837X_MAC_TX_PAUSE_STS);
	S.pause_tx = bit_of();
	reg_read_m(RTL837X_MAC_RX_PAUSE_STS);
	S.pause_rx = bit_of();
	reg_read_m(RTL8373_PHY_EEE_ABLTY);
	S.eee_active = bit_of();

	reg_read(RTL837X_L2_LRN_PORT_CONSTRT_CNT + (l << 2));
	S.macs = SFR_DATA_U16 & L2_LRN_PORT_CNT_MASK;

	STAT_GET(STAT_COUNTER_TX_PKTS, l);
	dst = S.tx_good;
	stat_pair();
	STAT_GET(STAT_COUNTER_RX_PKTS, l);
	dst = S.rx_good;
	stat_pair();
	STAT_GET(STAT_COUNTER_ERR_PKTS, l);	/* tx errors low, rx errors high */
	dst = (__xdata uint8_t *)&S.tx_bad;
	stat_word(RTL837X_STAT_V_LOW);
	dst = (__xdata uint8_t *)&S.rx_bad;
	stat_word(RTL837X_STAT_V_HIGH);

	/* the cable test keeps one result: the last port tested */
	S.cabletest_state = E_CTSTATE_IDLE;
	memset(ct_r, E_CTRES_UNKNOWN, 4);
	memset((__xdata uint8_t *)ct_l, 0, 8);
	if (ct_port == l) {
		if (ct_state) {
			S.cabletest_state = E_CTSTATE_RUNNING;
		} else if (ct_have) {
			S.cabletest_state = ct_valid ? E_CTSTATE_DONE : E_CTSTATE_TIMEOUT;
			memcpy(ct_r, ct_result, 4);
			memcpy(ct_l, ct_len, 8);
		}
	}
	S.cabletest_a_result = ct_r[0];
	S.cabletest_b_result = ct_r[1];
	S.cabletest_c_result = ct_r[2];
	S.cabletest_d_result = ct_r[3];
	S.cabletest_a_length_m = ct_l[0];
	S.cabletest_b_length_m = ct_l[1];
	S.cabletest_c_length_m = ct_l[2];
	S.cabletest_d_length_m = ct_l[3];
	return OM_OK;
}

/* state.port.N.mib.K: STAT index K's raw word */
uint8_t om_get_port_mib(uint8_t idx) __banked
{
	b = om_ix1;
	STAT_GET(b, idx);
	dst = om_st.port_mib.v;
	stat_pair();
	return OM_OK;
}

/* state.l2: the first entry at or after om_key (om.h), by its table index */
#define L	om_st.l2
static __xdata uint16_t ix;

uint8_t om_get_l2(uint8_t idx) __banked
{
	(void)idx;
	if (om_key >> 12 || hw_poll(RTL837X_TBL_CTRL))
		return OM_ERR;
	reg_read_m(RTL837x_TBL_DATA_0);
	sfr_data[1] &= 0xfc;
	sfr_data[2] |= TBL_LUTREAD_NEXT_L2UC << 6;
	reg_write_m(RTL837x_TBL_DATA_0);
	REG_WRITE(RTL837X_TBL_CTRL, om_key >> 8, om_key, TBL_L2_UNICAST, TBL_EXECUTE);
	if (hw_poll(RTL837X_TBL_CTRL))	/* E4: a stuck table ends the walk */
		return OM_ERR;
	reg_read_m(RTL837x_TBL_DATA_0);
	ix = ((uint16_t)(sfr_data[2] & 0x0f) << 8) | sfr_data[3];
	reg_read_m(RTL837x_L2_DATA_OUT_B);
	if (!(sfr_data[0] & 0x20) || ix < om_key)	/* none, or the search wrapped */
		return OM_ERR;
	om_key = ix;
	L.vlan = ((uint16_t)(sfr_data[0] & 0x0f) << 8) | sfr_data[1];
	L.mac[0] = sfr_data[2];
	L.mac[1] = sfr_data[3];
	l = sfr_data[0] >> 6;
	reg_read_m(RTL837x_L2_DATA_OUT_A);
	memcpy(L.mac + 2, sfr_data, 4);
	reg_read_m(RTL837x_L2_DATA_OUT_C);
	L.static_ = sfr_data[1] & 1;
	l |= (sfr_data[3] & 3) << 2;
	L.port = l;
	L.lag = port_lag_of(l) + 1;		/* PORT_LAG_NONE + 1: 0 */
	return OM_OK;
}

uint8_t om_do_port_cabletest(uint8_t idx) __banked
{
	phy_settings.port = idx;
	return cabletest_run();
}
