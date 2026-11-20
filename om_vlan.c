/*
 * om_vlan.c - vlan.V for the object model (schema/vlan.py): apply an entry
 * to the VLAN table, check it, and the VLAN checks of port.N.pvid and
 * mgmt.vlan. The table calls are the ones the old vlan command made.
 *
 * Keyed: the handlers get the slot; its key is the VID, 0 a free slot.
 */
#include <stdint.h>
#include <stdbool.h>
#include "rtl837x_common.h"
#include "rtl837x_port.h"
#include "om_gen.h"

static __xdata struct cfg_vlan r;
static __xdata uint16_t k;
static __xdata uint8_t s;

/* VID k in the candidate (c 1) or in running (c 0): 1 if so */
static __xdata uint8_t c;
static uint8_t has_k(void)
{
	__xdata struct cfg_vlan *p = c ? cfg_cand.vlan : cfg_run.vlan;

	for (s = MAX_VLANS; s; s--, p++)
		if (p->key == k)
			return 1;
	return 0;
}

/* the slot's old VID out of the table, unless running has it in another
 * slot; the new one written: members, tagged, the CPU (vlan_create) */
uint8_t om_apply_vlan(uint8_t idx) __banked
{
	memcpy(&r, &cfg_run.vlan[idx], sizeof(r));
	k = cfg_roll.vlan[idx].key;
	c = 0;
	if (k && k != r.key && !has_k())
		vlan_delete(k);
	if (r.key) {
		vlan_settings.vlan = r.key;
		vlan_settings.members = r.members;
		vlan_settings.tagged = r.tagged;
		vlan_create();
	}
	return OM_OK;
}

uint8_t om_check_vlan(uint8_t idx) __banked
{
	memcpy(&r, &cfg_cand.vlan[idx], sizeof(r));
	if (!r.key)
		return OM_OK;
	if (r.tagged & ~r.members) {
		om_fail(OM_E_VLAN_TAGGED);
		return OM_ERR;
	}
	if (r.name[0]) {
		if (!(((uint8_t)r.name[0] | 0x20) >= 'a' && ((uint8_t)r.name[0] | 0x20) <= 'z'))
			goto bad_name;
		for (s = 0; r.name[s]; s++)
			if (r.name[s] == ' ')
				goto bad_name;
	}
	return OM_OK;
bad_name:
	om_fail(OM_E_VLAN_NAME);
	return OM_ERR;
}

/* port.N.pvid, mgmt.vlan: 0, or a VLAN of the candidate */
uint8_t om_vlan_known(uint16_t vid) __banked
{
	k = vid;
	if (!k)
		return 1;
	c = 1;
	return has_k();
}
