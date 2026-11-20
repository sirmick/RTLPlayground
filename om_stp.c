/*
 * om_stp.c - stp, stp.lag.L, stp.port.N for the object model (schema/stp.py):
 * apply them to rtl837x_stp.c's config, check them. The calls are the ones
 * the old stp command made. A LAG member's stp.port.N is stored and does
 * nothing while the port is in the LAG (stp_ent_id() 0: it isn't an entity).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "rtl837x_common.h"
#include "rtl837x_stp.h"
#include "om_gen.h"

extern __xdata bool stp_enabled;

typedef char stp_lag_is_port[sizeof(struct cfg_stp_lag) == sizeof(struct cfg_stp_port) &&
	offsetof(struct cfg_stp_lag, p2p) == offsetof(struct cfg_stp_port, p2p) &&
	E_STPEDGE_ON == 1 && E_STPP2P_ON == 1 && E_STPP2P_OFF == 2 &&
	STP_PF_BPDUGUARD == E_STPGUARD_BPDU << 3 && STP_PF_ROOTGUARD == E_STPGUARD_ROOT << 3 &&
	STP_PF_FILTER == 0x20 && STP_PF_ADMEDGE == E_STPEDGE_ON << 1 ? 1 : -1];

static __xdata struct cfg_stp g, h;
static __xdata struct cfg_stp_port r, o;
static __xdata uint8_t e, f;

uint8_t om_apply_stp(uint8_t idx) __banked
{
	(void)idx;
	memcpy(&g, &cfg_run.stp, sizeof(g));
	memcpy(&h, &cfg_roll.stp, sizeof(h));
	stp_rstp = g.version == E_STPVER_RSTP;
	stp_prio = g.priority << 4;	/* n * 4096, as the BPDU's high byte */
	stp_hello_s = g.hello_s;
	stp_maxage_s = g.maxage_s;
	stp_fwddelay_s = g.fwd_delay_s;
	stp_txhold = g.txhold;
	stp_pcost_short = g.pathcost;
	stp_bpdu_filter = g.bpdu;
	if (g.enabled != h.enabled) {
		stp_enabled = g.enabled;
		if (g.enabled)
			stp_setup();
		else
			stp_off();
	} else if (!g.enabled && g.bpdu != h.bpdu) {
		stp_off();			/* flood or filter them now */
	}
	if (g.priority != h.priority)
		stp_prio_apply();
	return OM_OK;
}

/* DESIGN 6.20.5 */
uint8_t om_check_stp(uint8_t idx) __banked
{
	(void)idx;
	memcpy(&g, &cfg_cand.stp, sizeof(g));
	if ((uint16_t)2 * (g.fwd_delay_s - 1) >= g.maxage_s && g.maxage_s >= (uint16_t)2 * (g.hello_s + 1))
		return OM_OK;
	om_fail(OM_E_STP_TIMES);
	return OM_ERR;
}

uint8_t om_do_stp_counters_clear(uint8_t idx) __banked
{
	(void)idx;
	stp_counters_clear();
	return OM_OK;
}

/* r (running) and o (rollback) into entity e: its flags keep the runtime
 * bits. An edge that changes drops the operational edge (an admin edge is
 * one at once); enabled off and on clears a tripped guard. A running entity
 * whose enabled or edge changes starts over (stp_setup, before stp.port in
 * the apply order, used the old flags). */
static void ent_apply(void)
{
	f = stp_pflags[e] & (STP_PF_OPEREDGE | STP_PF_TRIPPED);
	if (r.edge != o.edge) {
		f &= ~STP_PF_OPEREDGE;
		if (r.edge == E_STPEDGE_ON)
			f |= STP_PF_OPEREDGE;
	}
	if (r.enabled) {
		f |= STP_PF_ENABLED;
		if (!o.enabled)
			f &= ~STP_PF_TRIPPED;
	}
	if (r.edge == E_STPEDGE_AUTO)
		f |= STP_PF_AUTOEDGE;
	f |= (r.edge << 1 & STP_PF_ADMEDGE) | r.guard << 3 | r.filter << 5;
	stp_pflags[e] = f;
	stp_pcost[e] = r.cost;
	stp_pprio[e] = r.priority;
	stp_pp2p[e] = r.p2p;
	if ((r.enabled == o.enabled && r.edge == o.edge) || !stp_enabled)
		return;
	stp_lag_map();
	f = r.enabled;			/* off or an admin edge: forwarding; else listen first */
	if (r.edge == E_STPEDGE_ON)
		f = 0;			/* (a bool argument would take a bit of IRAM) */
	if (stp_ent_id(e))
		stp_port_admin(e, f);
}

/* a port priority is a multiple of 16 */
static uint8_t ent_check(void)
{
	if (!(r.priority & 15))
		return OM_OK;
	om_fail(OM_E_STP_PRIO);
	return OM_ERR;
}

static void ent_mcheck(void)
{
	if (stp_enabled && stp_ent_id(e))
		stp_port_mcheck(e);
}

/* a tripped BPDU guard (only while STP runs: stp_off clears it): the entity
 * starts over, as at its link up */
static void ent_untrip(void)
{
	if (stp_pflags[e] & STP_PF_TRIPPED)
		stp_ent_restart(e);
}

uint8_t om_apply_stp_port(uint8_t idx) __banked
{
	e = idx;
	memcpy(&r, &cfg_run.stp_port[e], sizeof(r));
	memcpy(&o, &cfg_roll.stp_port[e], sizeof(o));
	ent_apply();
	return OM_OK;
}

uint8_t om_apply_stp_lag(uint8_t idx) __banked
{
	e = STP_LAG_BASE + idx;
	memcpy(&r, &cfg_run.stp_lag[idx], sizeof(r));
	memcpy(&o, &cfg_roll.stp_lag[idx], sizeof(o));
	ent_apply();
	return OM_OK;
}

uint8_t om_check_stp_port(uint8_t idx) __banked
{
	memcpy(&r, &cfg_cand.stp_port[idx], sizeof(r));
	return ent_check();
}

uint8_t om_check_stp_lag(uint8_t idx) __banked
{
	memcpy(&r, &cfg_cand.stp_lag[idx], sizeof(r));
	return ent_check();
}

uint8_t om_do_stp_port_mcheck(uint8_t idx) __banked
{
	e = idx;
	ent_mcheck();
	return OM_OK;
}

uint8_t om_do_stp_lag_mcheck(uint8_t idx) __banked
{
	e = STP_LAG_BASE + idx;
	ent_mcheck();
	return OM_OK;
}

uint8_t om_do_stp_port_clear_guard(uint8_t idx) __banked
{
	e = idx;
	ent_untrip();
	return OM_OK;
}

uint8_t om_do_stp_lag_clear_guard(uint8_t idx) __banked
{
	e = STP_LAG_BASE + idx;
	ent_untrip();
	return OM_OK;
}
