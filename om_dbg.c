/*
 * om_dbg.c - state.dbg for the object model (schema/state_dbg.py): raw
 * registers, read whole. Built with OM_DBG=1 only.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "om_gen.h"

extern __xdata uint8_t sfr_data[4];

static __xdata uint8_t i;

static void word_to(__xdata uint8_t *d)	/* sfr_data, big endian, into a Hex32 */
{
	for (i = 0; i < 4; i++)
		d[i] = sfr_data[3 - i];
}

uint8_t om_get_dbg(uint8_t idx) __banked
{
	(void)idx;
	reg_read_m(RTL837X_REG_SDS_MODES);
	word_to((__xdata uint8_t *)&om_st.dbg.sds_modes);
	reg_read_m(RTL837X_REG_GPIO_00_31_INPUT);
	word_to((__xdata uint8_t *)&om_st.dbg.gpio_0);
	reg_read_m(RTL837X_REG_GPIO_00_31_INPUT + 4);
	word_to((__xdata uint8_t *)&om_st.dbg.gpio_1);
	return OM_OK;
}

uint8_t om_get_dbg_igmp_port(uint8_t idx) __banked
{
	reg_read_m(RTL837X_IGMP_PORT_CFG + (idx << 2));
	word_to((__xdata uint8_t *)&om_st.dbg_igmp_port.cfg);
	return OM_OK;
}
