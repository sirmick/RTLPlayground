/*
 * om_lag.c - lag.L for the object model (schema/lag.py): apply a LAG to the
 * trunk registers or to LACP, and check it. The calls are the ones the old
 * lag and laghash commands made; LACP writes the trunk's members itself, as
 * it aggregates them.
 */
#include <stdint.h>
#include <stdbool.h>
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "rtl837x_sfr.h"
#include "rtl837x_port.h"
#include "rtl837x_lacp.h"
#include "om_gen.h"

typedef char laghash_is_hw[1 << E_LAGHASH_SPA == LAG_HASH_SOURCE_PORT_NUMBER && 1 << E_LAGHASH_DPORT == LAG_HASH_L4_DPORT ? 1 : -1];

static __xdata struct cfg_lag r, o;
static __xdata uint16_t m, pv, f, d;
static __xdata uint8_t l, j;

/* the members in use: none while off */
#define USED(x)	((x).mode == E_LAGMODE_OFF ? 0 : (x).members)

uint8_t om_apply_lag(uint8_t idx) __banked
{
	l = idx;
	memcpy(&r, &cfg_run.lag[l], sizeof(r));
	memcpy(&o, &cfg_roll.lag[l], sizeof(o));
	m = USED(r);
	if (r.mode != o.mode || m != USED(o)) {
		for (j = 0; j < 4; j++) {	/* its ports out of any other trunk first: never in two */
			pv = port_lag_members_get(j);
			if (j != l && (pv & m))
				port_lag_members_set(j, pv & ~m);
		}
		if (r.mode == E_LAGMODE_LACP) {
			lacp_lag_set(l, m);
		} else {
			lacp_lag_set(l, 0);
			port_lag_members_set(l, m);
		}
	}
	if (r.hash != o.hash)
		REG_WRITE(RTL837X_TRK_HASH_CTRL_BASE + (l << 2), 0, 0, 0, r.hash);
	return OM_OK;
}

/* a hash field; no port in another LAG that isn't off; one PVID */
uint8_t om_check_lag(uint8_t idx) __banked
{
	l = idx;
	memcpy(&r, &cfg_cand.lag[l], sizeof(r));
	if (!r.hash) {
		om_fail(OM_E_LAG_HASH);
		return OM_ERR;
	}
	m = USED(r);
	for (j = 0; j < 4; j++) {
		memcpy(&o, &cfg_cand.lag[j], sizeof(o));
		if (j != l && (USED(o) & m)) {
			om_fail(OM_E_LAG_PORT);
			return OM_ERR;
		}
	}
	pv = 0xffff;			/* the first member's; d: it and the members that differ */
	d = 0;
	for (j = 0; m; j++, m >>= 1) {
		if (!(m & 1))
			continue;
		if (pv == 0xffff) {
			pv = cfg_cand.port[j].pvid;
			f = (uint16_t)1 << j;
		} else if (pv != cfg_cand.port[j].pvid) {
			d |= f | (uint16_t)1 << j;
		}
	}
	if (d) {
		om_val[0] = d;
		om_val[1] = d >> 8;
		om_fail_ports(OM_E_LAG_PVID);
		return OM_ERR;
	}
	return OM_OK;
}
