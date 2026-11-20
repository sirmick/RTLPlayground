// #define REGDBG
// #define DEBUG

#include "rtl837x_common.h"
#include "rtl837x_sfr.h"
#include "rtl837x_regs.h"
#include "rtl837x_bandwidth.h"
#include "machine.h"


extern __xdata uint8_t sfr_data[4];

void bandwidth_setup(void) __banked
{
	print_string("bandwidth_setup called\n");
	// Exclude all packets possibly for the CPU port, but do not include bypassed packets or Inter-Frame-Gap into bandwidth
	REG_SET(RTL837X_IGBW_CTRL, IGBW_ADM_DHCP | IGBW_ADM_ARPREQ | IGBW_ADM_RMA | IGBW_ADM_BPDU | IGBW_ADM_RTKPKT | IGBW_ADM_IGMP);

	// We do not count IFG for Egress and allways allow CPU-traffic
	REG_SET(RTL837X_EGBW_CTRL, EGBW_CPUMODE);

	print_string("RTL837X_IGBW_CTRL: "); print_reg(RTL837X_IGBW_CTRL); write_char('\n');
	print_string("RTL837X_EGBW_CTRL: "); print_reg(RTL837X_EGBW_CTRL); write_char('\n');
	print_string("bandwidth_setup done\n");
}

