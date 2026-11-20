#include <stdint.h>
#include <stdbool.h>
#include "rtl837x_common.h"
#include "rtl837x_sfr.h"
#include "rtl837x_cabletest.h"
#include "om_gen.h"
#include "rtl837x_phy.h"
#include "phy.h"
#include "machine.h"
#include "timer.h"


#define RTCT_CTRL		0xa422
#define RTCT_CTRL_START		0x00f1
#define RTCT_CTRL_DONE		0x8000
#define RTCT_SRAM_ADDR		0xa436
#define RTCT_SRAM_DATA		0xa438
#define RTCT_SRAM_STATUS	0x8026
#define RTCT_SRAM_LEN		0x8028

#define RTCT_ST_BUSY		0x01
#define RTCT_ST_MISMATCH	0x06
#define RTCT_ST_OPEN		0x08
#define RTCT_ST_SHORT		0x10
#define RTCT_ST_OK		0x20
#define RTCT_ST_DONE		0x40
#define RTCT_ST_CROSS_SHORT	0x80

#define RTCT_LEN_ZERO		150
#define RTCT_LEN_PER_50CM	43

#define PHY_POWER		0xa610
#define PHY_POWER_OFF		0x0800

#define BMCR_PDOWN_1G_FULL	0x0940
#define BMCR_1G_FULL		0x0140
#define BMCR_ANRESTART		0x0200

#define CT_IDLE			0
#define CT_SILENCE		1
#define CT_RUN			2
#define CT_POLL_TICKS		(SYS_TICK_HZ / 20)
#define CT_POLLS		100

extern __code const struct machine machine;
extern __xdata struct machine_runtime machine_detected;

__xdata uint8_t ct_state = CT_IDLE;
__xdata uint8_t ct_port;
__xdata struct timer ct_timer;
__xdata uint8_t ct_polls;
__xdata uint8_t ct_valid;
__xdata uint8_t ct_have = 0;
__xdata uint8_t ct_pair;
__xdata uint8_t ct_flags;
__xdata uint16_t ct_bmcr;
__xdata uint8_t ct_result[4];
__xdata uint16_t ct_len[4];

void rtl_phy_cable_test_start(uint8_t port) __banked
{
	phy_write(port, PHY_MMD31, PHY_MMD31_FEDCR, BMCR_1G_FULL);
	phy_write(port, PHY_MMD31, RTCT_CTRL, RTCT_CTRL_START);
}

bool rtl_phy_cable_test_get_status(uint8_t port) __banked
{
	phy_read(port, PHY_MMD31, RTCT_CTRL);
	if (!(SFR_DATA_U16 & RTCT_CTRL_DONE))
		return false;

	for (ct_pair = 0; ct_pair < 4; ct_pair++) {
		// The flags are the low byte of the word, not the high one.
		phy_write(port, PHY_MMD31, RTCT_SRAM_ADDR, RTCT_SRAM_STATUS + (ct_pair << 2));
		phy_read(port, PHY_MMD31, RTCT_SRAM_DATA);
		ct_flags = SFR_DATA_U16;
		if (!(ct_flags & RTCT_ST_DONE))
			return false;
		if (ct_flags & RTCT_ST_OK)
			ct_result[ct_pair] = CABLE_RESULT_OK;
		else if (ct_flags & RTCT_ST_OPEN)
			ct_result[ct_pair] = CABLE_RESULT_OPEN;
		else if (ct_flags & RTCT_ST_SHORT)
			ct_result[ct_pair] = CABLE_RESULT_SAME_SHORT;
		else if (ct_flags & RTCT_ST_BUSY)
			ct_result[ct_pair] = CABLE_RESULT_UNSPEC;
		else if (ct_flags & RTCT_ST_CROSS_SHORT)
			ct_result[ct_pair] = CABLE_RESULT_CROSS_SHORT;
		else if (ct_flags & RTCT_ST_MISMATCH)
			ct_result[ct_pair] = CABLE_RESULT_MISMATCH;
		else
			ct_result[ct_pair] = CABLE_RESULT_UNSPEC;

		phy_write(port, PHY_MMD31, RTCT_SRAM_ADDR, RTCT_SRAM_LEN + (ct_pair << 2));
		phy_read(port, PHY_MMD31, RTCT_SRAM_DATA);
		ct_len[ct_pair] = SFR_DATA_U16;
		ct_len[ct_pair] = ct_len[ct_pair] > RTCT_LEN_ZERO ? ct_len[ct_pair] - RTCT_LEN_ZERO : 0;
		ct_len[ct_pair] = ct_len[ct_pair] * 5 / RTCT_LEN_PER_50CM;
	}
	return true;
}

/* the test's end, as the state lines the object model shows */
static void cabletest_event(void)
{
	print_string("event state.port.");
	itoa_short(machine.log_to_phys_port[ct_port]);
	print_string(ct_valid ? ".cabletest.state done\n" : ".cabletest.state timeout\n");
}

uint8_t cabletest_run(void) __banked
{
	if (ct_state) {
		om_fail(OM_E_CT_RUNNING);
		return OM_ERR;
	}
	if (!machine_detected.isRTL8373 && (phy_settings.port < 4 || phy_settings.port > 7)) {
		om_fail(OM_E_CT_PHY);
		return OM_ERR;
	}

	// A test started on a port that is off never ends and blocks the PHY until reset.
	phy_read(phy_settings.port, PHY_MMD31, PHY_POWER);
	if (SFR_DATA_U16 & PHY_POWER_OFF) {
		om_fail(OM_E_CT_OFF);
		return OM_ERR;
	}

	ct_port = phy_settings.port;
	phy_read(ct_port, PHY_MMD31, PHY_MMD31_FEDCR);
	ct_bmcr = SFR_DATA_U16;

	// Forcing the speed leaves the link up; only a power-down silences the partner.
	phy_write(ct_port, PHY_MMD31, PHY_MMD31_FEDCR, BMCR_PDOWN_1G_FULL);
	timer_set(&ct_timer, SYS_TICK_HZ);
	ct_have = 0;
	ct_state = CT_SILENCE;
	return OM_OK;
}

void cabletest_tick(void) __banked
{
	if (ct_state == CT_SILENCE) {
		if (!timer_expired(&ct_timer))
			return;
		phy_read(ct_port, PHY_MMD31, PHY_POWER);
		if (SFR_DATA_U16 & PHY_POWER_OFF) {
			phy_write(ct_port, PHY_MMD31, PHY_MMD31_FEDCR, ct_bmcr);
			ct_state = CT_IDLE;
			ct_have = 1;
			ct_valid = 0;
			cabletest_event();
			return;
		}
		rtl_phy_cable_test_start(ct_port);
		ct_polls = CT_POLLS;
		timer_set(&ct_timer, CT_POLL_TICKS);
		ct_state = CT_RUN;
		return;
	}
	if (ct_state != CT_RUN || !timer_expired(&ct_timer))
		return;
	timer_restart(&ct_timer);

	ct_valid = rtl_phy_cable_test_get_status(ct_port);
	if (!ct_valid && --ct_polls)
		return;

	phy_write(ct_port, PHY_MMD31, RTCT_CTRL, 0);
	phy_write(ct_port, PHY_MMD31, PHY_MMD31_FEDCR, ct_bmcr | BMCR_ANRESTART);
	ct_have = 1;
	ct_state = CT_IDLE;
	cabletest_event();
}
