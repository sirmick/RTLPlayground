/*
 * om_port.c - port.N for the object model (schema/port.py): apply a port's
 * config to the hardware, check it, read its state, start its cable test.
 * The hardware calls are the ones the old port, mtu, pvid, ingress, isolate,
 * eee, bw, storm and sfp commands made. And mirror (schema/mirror.py), the
 * old mirror's.
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
#include "rtl837x_storm.h"
#include "phy.h"
#include "sfp.h"
#include "machine.h"
#include "uip/uip.h"
#include "om_gen.h"

extern __code const struct machine machine;
extern __xdata struct uip_eth_addr uip_ethaddr;
extern __xdata uint8_t sfr_data[4];
extern __xdata bool lldp_was_enabled;

static __xdata struct cfg_port r;	/* running: what to program */
static __xdata struct cfg_port o;	/* what the hardware had */
static __xdata uint8_t b, l, chg;
static __xdata uint32_t bw;
static __xdata uint8_t * __xdata bp;
static __xdata uint8_t on, bit;

static void bit_put(uint16_t reg)	/* bit `bit` of reg: on or off */
{
	if (on)
		reg_bit_set(reg, bit);
	else
		reg_bit_clear(reg, bit);
}

static __code const uint8_t speed_phy[] = {
	PHY_SPEED_AUTO, PHY_SPEED_10M, PHY_SPEED_100M, PHY_SPEED_1G,
	PHY_SPEED_2G5, PHY_SPEED_5G, PHY_SPEED_10G
};
static __code const uint8_t duplex_phy[] = { PHY_DUPLEX_BOTH, PHY_DUPLEX_FULL, PHY_DUPLEX_HALF };
static __code const uint8_t ingress_hw[] = { VLAN_ALL, VLAN_TAGGED, VLAN_UNTAGGED };

/* a bandwidth limit, bw kbit/s, into its register: bit 20 on and the rate
 * in 16 kbit/s steps; 0 is off, the value the old `bw ... off` wrote.
 * Off is the chip's power-on value, so the boot's write leaves a fresh chip
 * as it was and a soft reset clears a limit (unconfirmed: P8 silicon read) */
#define BW_OFF	0x000fffff
static void bw_write(uint16_t reg)
{
	bp = (__xdata uint8_t *)&bw;
	if (bp[0] | bp[1] | bp[2]) {
		sfr_data[0] = 0;
		sfr_data[1] = 0x10 | (bp[2] >> 4);
		sfr_data[2] = (bp[2] << 4) | (bp[1] >> 4);
		sfr_data[3] = (bp[1] << 4) | (bp[0] >> 4);
		reg_write_m(reg);
	} else {
		REG_SET(reg, BW_OFF);
	}
}

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
		chg = r.eee != o.eee || r.eee_speeds != o.eee_speeds;
		if (chg) {	/* the speeds advertised, capped by the port's */
			b = r.eee ? r.eee_speeds : 0;
			if (!OM_TEN_G(l))
				b &= 7;
			REG_SET(RTL837X_EEE_CTRL_BASE + ((uint16_t)l << 8), r.eee ? EEE_RX_ENABLE | EEE_TX_ENABLE : 0);
			/* 100m, 1g, 10g: PHY_EEE_BIT_100M, _1G, _10G; 2g5, 5g: _2G5, _5G */
			phy_write(l, PHY_MMD_AN, PHY_EEE_ADV, ((b & 3) << 1) | ((b & 0x10) >> 1));
			phy_write(l, PHY_MMD_AN, PHY_EEE_ADV2, (b >> 2) & 3);
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
	} else if (r.speed != o.speed) {	/* the cage's: sfp_speed[] takes auto, 100m.. (no 10m) */
		b = machine.is_sfp[l] - 1;
		sfp_speed[b] = r.speed ? r.speed - 1 : SFP_SPEED_AUTO;
		if (!om_full) {		/* the module found again by the sfp task's next poll, then read */
			sfp_pins_last |= 0x1 << (b << 2);
			sfp_ready[b] = sfp_id_ok[b] = sfp_diag_ok[b] = 0;
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
			if (lldp_was_enabled)
				lldp_shutdown((uint16_t)1 << l);
			lldp_logical_port_status &= ~((uint16_t)1 << l);
		} else {
			lldp_logical_port_status |= (uint16_t)1 << l;
		}
	}
	if (memcmp(&r.bw_in_kbps, &o.bw_in_kbps, 4)) {	/* the rate before the mode */
		bw = r.bw_in_kbps;
		bw_write(RTL837X_IGBW_PORT_CTRL + (l << 2));
	}
	if (r.bw_in_mode != o.bw_in_mode) {
		on = r.bw_in_mode == E_BWMODE_FC;
		bit = l;
		bit_put(RTL837X_IGBW_PORT_FC_CTRL);
	}
	if (memcmp(&r.bw_out_kbps, &o.bw_out_kbps, 4)) {
		bw = r.bw_out_kbps;
		bw_write(RTL837X_EGBW_PORT_CTRL + ((uint16_t)l << 10));
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
	if (((uint8_t)r.bw_in_kbps | (uint8_t)r.bw_out_kbps) & 15) {
		om_fail(OM_E_BW_16);
		return OM_ERR;
	}
	if (machine.is_sfp[l]) {
		if (r.speed == E_SPEED_10M || r.speed == E_SPEED_5G) {
			om_fail(OM_E_SFP_SPEED);
			return OM_ERR;
		}
		return OM_OK;
	}
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

/* port.N.storm.T: meter 24 + idx (idx = port * 4 + type, STORM_METER()).
 * Off is rate 0, the burst at its 0x2000 and kbit/s mode, as the chip
 * powers on (the burst: doc/storm_control.md; unconfirmed: P8 silicon read) */
#define METER_BURST_OFF	0x20	/* sfr_data[2]: 0x00002000 */
static __xdata struct cfg_port_storm sr;

/* STORM_MIDX words, row type * 2 + half: half 0 holds ports 0-4, half 1 ports 5-9,
 * 6 bits per port, each set to STORM_METER(port, type) */
static __code const uint8_t storm_midx[8][4] = {
	{ 0x28, 0x92, 0x07, 0x18 }, { 0x3c, 0xe3, 0x4c, 0x2c },
	{ 0x29, 0x96, 0x17, 0x59 }, { 0x3d, 0xe7, 0x5c, 0x6d },
	{ 0x2a, 0x9a, 0x27, 0x9a }, { 0x3e, 0xeb, 0x6c, 0xae },
	{ 0x2b, 0x9e, 0x37, 0xdb }, { 0x3f, 0xef, 0x7c, 0xef },
};

uint8_t om_apply_port_storm(uint8_t idx) __banked
{
	l = idx >> 2;
	if (!MACHINE_PORT_VALID(l))
		return OM_OK;
	memcpy(&sr, &cfg_run.port_storm[0][0] + idx, sizeof(sr));
	bp = (__xdata uint8_t *)&sr.rate;
	chg = bp[0] | bp[1] | bp[2];		/* a limit */
	on = chg && sr.unit == E_SUNIT_PPS;
	b = STORM_METER_BASE + idx;
	sfr_data[0] = 0;
	sfr_data[1] = bp[2];
	sfr_data[2] = bp[1];
	sfr_data[3] = bp[0];
	reg_write_m(RTL837X_METER_RATE + (b << 2));
	if (!on) {			/* a pps limit's burst is one second of it */
		sfr_data[1] = 0;
		sfr_data[2] = METER_BURST_OFF;
		sfr_data[3] = 0;
	}
	reg_write_m(RTL837X_METER_BURST + (b << 2));
	bit = b & 0x1f;
	bit_put(RTL837X_METER_MODE + ((b >> 5) << 2));
	b = idx & 3;			/* the type */
	if (chg || om_full) {		/* the MIDX row (the boot's: a soft reset can't leave it unlike a power-on) */
		bit = (b << 1) | (l >= 5 ? 1 : 0);
		memcpyc(sfr_data, storm_midx[bit], 4);
		reg_write_m(RTL837X_STORM_MIDX + (bit << 2));
	}
	on = chg;
	bit = l;
	bit_put(RTL837X_STORM_CTRL + (b << 2));
	return OM_OK;
}

uint8_t om_check_port_storm(uint8_t idx) __banked
{
	memcpy(&sr, &cfg_cand.port_storm[0][0] + idx, sizeof(sr));
	if (sr.unit == E_SUNIT_PPS && ((__xdata uint8_t *)&sr.rate)[2] > 0x0f) {
		om_fail(OM_E_STORM_PPS);
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
	S.lp_adv = 0;
	S.eee_lp = 0;
	S.lag = port_lag_of(l) + 1;		/* PORT_LAG_NONE + 1: 0 */
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
			phy_read(l, PHY_MMD_AN, PHY_ANEG_LP_ABILITY);	/* 10h..100f: bits 5-8 */
			S.lp_adv = (SFR_DATA_U16 >> 5) & 0x0f;
			phy_read(l, PHY_MMD31, PHY_MMD31_GANLPAR);	/* 1000h, f: 10, 11 */
			S.lp_adv |= (SFR_DATA_U16 >> 6) & 0x30;
			phy_read(l, PHY_MMD_AN, PHY_ANEG_MGBASE_ADV);	/* 2g5, 5g: 5, 6; 10g: 11 */
			S.lp_adv |= ((SFR_DATA_U16 << 1) & 0xc0) | ((SFR_DATA_U16 >> 3) & 0x100);
			phy_read(l, PHY_MMD_AN, PHY_EEE_LP_ABILITY);	/* 100m, 1g, 10g */
			b = SFR_DATA_U16;
			S.eee_lp = ((b >> 1) & 3) | ((b << 1) & 0x10);
			phy_read(l, PHY_MMD_AN, PHY_EEE_LP_ABILITY2);	/* 2g5, 5g */
			S.eee_lp |= (SFR_DATA_U16 << 2) & 0x0c;
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
	S.cabletest_pair_a_result = ct_r[0];
	S.cabletest_pair_b_result = ct_r[1];
	S.cabletest_pair_c_result = ct_r[2];
	S.cabletest_pair_d_result = ct_r[3];
	S.cabletest_pair_a_length_m = ct_l[0];
	S.cabletest_pair_b_length_m = ct_l[1];
	S.cabletest_pair_c_length_m = ct_l[2];
	S.cabletest_pair_d_length_m = ct_l[3];
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
static __xdata uint16_t ix, want;

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

/* do l2.flush: every port's dynamic entries */
uint8_t om_do_l2_flush(uint8_t idx) __banked
{
	(void)idx;
	port_l2_forget();
	return OM_OK;
}

/* do l2.delete K: entry K, read as state.l2.K is, written back without
 * its ports, age and auth; the switch's own entry keeps management */
uint8_t om_do_l2_delete(uint8_t idx) __banked
{
	(void)idx;
	want = om_key;
	if (om_get_l2(0) || om_key != want) {
		om_fail(OM_E_L2_NONE);
		return OM_ERR;
	}
	if (!memcmp(L.mac, uip_ethaddr.addr, 6)) {
		om_fail(OM_E_L2_OWN);
		return OM_ERR;
	}
	reg_read_m(RTL837x_L2_DATA_OUT_B);
	sfr_data[0] &= 0x3f;			/* SPA */
	reg_write_m(RTL837x_TBL_DATA_IN_B);
	reg_read_m(RTL837x_L2_DATA_OUT_A);
	reg_write_m(RTL837x_TBL_DATA_IN_A);
	reg_read_m(RTL837x_L2_DATA_OUT_C);
	sfr_data[3] &= 0xc0;			/* age, auth, the ports' high bits */
	sfr_data[1] &= 0xfe;			/* nosalearn */
	reg_write_m(RTL837x_TBL_DATA_IN_C);
	reg_read_m(RTL837x_TBL_DATA_0);
	sfr_data[2] = TBL_L2_UNICAST;
	reg_write_m(RTL837x_TBL_DATA_0);
	REG_WRITE(RTL837X_TBL_CTRL, ix >> 8, ix, TBL_L2_UNICAST, TBL_WRITE | TBL_EXECUTE);
	hw_poll(RTL837X_TBL_CTRL);
	return OM_OK;
}

uint8_t om_do_port_cabletest(uint8_t idx) __banked
{
	phy_settings.port = idx;
	return cabletest_run();
}

/* mirror (schema/mirror.py): the sources, then the mirror port and on */
uint8_t om_apply_mirror(uint8_t idx) __banked
{
	(void)idx;
	REG_WRITE(RTL837x_MIRROR_CONF, cfg_run.mirror.rx >> 8, cfg_run.mirror.rx,
		  cfg_run.mirror.tx >> 8, cfg_run.mirror.tx);
	REG_WRITE(RTL837x_MIRROR_CTRL, 0, 0, 0,
		  cfg_run.mirror.enabled ? (cfg_run.mirror.port << 1) | 0x1 : 0);
	return OM_OK;
}


uint8_t om_check_mirror(uint8_t idx) __banked
{
	(void)idx;
	if (cfg_cand.mirror.port == 0xff) {
		if (!cfg_cand.mirror.enabled)
			return OM_OK;
		om_fail(OM_E_MIRROR_PORT);
		return OM_ERR;
	}
	if ((cfg_cand.mirror.rx | cfg_cand.mirror.tx) & ((uint16_t)1 << cfg_cand.mirror.port)) {
		om_fail(OM_E_MIRROR_SOURCE);
		return OM_ERR;
	}
	return OM_OK;
}
