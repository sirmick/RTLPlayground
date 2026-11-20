/*
 * om_sys.c - sys for the object model (schema/sys.py, schema/state_sys.py):
 * the config (hostname, led: what the old hostname and led commands
 * did); state.sys, the build, the board and the chip;
 * state.loader; and the actions reset, factory_reset and rnd. state.sys.reset is sched_health.c's
 * (it reads the scheduler's names). The boot's version lines are printed
 * from here too, from the same strings.
 */
#include <stdint.h>
#include <stdbool.h>
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "rtl837x_sfr.h"
#include "machine.h"
#include "version.h"
#include "crash.h"
#include "flash_job.h"
#include "loader/layout.h"
#include "uip/uip.h"
#include "update_apply.h"
#include "om_gen.h"
#include "rtl837x_leds.h"
#include "boot.h"

extern __code const struct machine machine;
extern __xdata struct machine_runtime machine_detected;
extern __xdata struct uip_eth_addr uip_ethaddr;
extern __xdata uint8_t sfr_data[4];
extern __xdata uint32_t flash_size;
extern __xdata bool button_last;
extern __xdata uint8_t cmd_capture;	/* a web command's output is being captured */
extern __xdata char hostname[24];

static __code const char s_version[] = VERSION_SW;
static __code const char s_date[] = BUILD_DATE;
static __code const char hexd[] = "0123456789abcdef";

/* they fit their fields (SDCC's _Static_assert only warns: a negative array size it refuses) */
typedef char version_fits[sizeof(s_version) <= sizeof(om_st.sys.version) ? 1 : -1];
typedef char date_fits[sizeof(s_date) <= sizeof(om_st.sys.build_date) ? 1 : -1];
typedef char pcb_fits[sizeof(MACHINE_PCB_INFO) <= sizeof(om_st.sys.pcb) ? 1 : -1];

static __xdata uint8_t l, b;
static __xdata uint16_t v;
static __xdata uint8_t * __xdata dp;

#define S	om_st.sys
#define UP	((__xdata uint8_t *)&om_st.sys.uptime_s)

/* sys.hostname: one word (DHCP option 12, LLDP's system name) */
uint8_t om_check_sys(uint8_t idx) __banked
{
	(void)idx;
	for (l = 0; l < sizeof(cfg_cand.sys.hostname); l++)
		if (cfg_cand.sys.hostname[l] == ' ') {
			om_fail(OM_E_HOSTNAME_SPACE);
			return OM_ERR;
		}
	return OM_OK;
}

uint8_t om_apply_sys(uint8_t idx) __banked
{
	(void)idx;
	memcpy(hostname, cfg_run.sys.hostname, sizeof(hostname));
	set_hostname_default();		/* empty: from the MAC */
	l = cfg_run.sys.led;
	if (l != cfg_roll.sys.led)	/* the LED registers only for a new setting */
		leds_off(l != E_LED_ON, l == E_LED_OFF);
	return OM_OK;
}

/* sys.hostname, empty: the name in use */
uint8_t om_derive_sys(uint8_t idx) __banked
{
	(void)idx;
	memcpy(om_val, hostname, sizeof(hostname));
	return OM_OK;
}

void print_sw_version(void) __banked
{
	print_string("Software version: ");
	print_string(s_version);
	print_string("\nBuild date: ");
	print_string(s_date);
	print_string("\nCompiler: SDCC " STR(__SDCC_VERSION_MAJOR) "." STR(__SDCC_VERSION_MINOR) "."
		     STR(__SDCC_VERSION_PATCH) " #" STR(__SDCC_REVISION));
	print_string("\nToolchain: " SDCC_TOOLCHAIN);
	print_string("\nHardware: ");
	print_string(machine.machine_name);
	write_char('\n');
}

uint8_t om_get_sys(uint8_t idx) __banked
{
	(void)idx;
	memcpyc((__xdata uint8_t *)S.version, (__code const uint8_t *)s_version, sizeof(s_version));
	memcpyc((__xdata uint8_t *)S.build_date, (__code const uint8_t *)s_date, sizeof(s_date));
	memcpyc((__xdata uint8_t *)S.machine, (__code const uint8_t *)machine.machine_name, 30);
	S.machine[30] = 0;
	memcpyc((__xdata uint8_t *)S.pcb, (__code const uint8_t *)MACHINE_PCB_INFO, sizeof(MACHINE_PCB_INFO));
	S.chip = machine_detected.isRTL8373;
	S.ports = OM_ISOLATE_DEFAULT & ~PMASK_CPU;
	S.sfp_ports = 0;
	v = 1;
	for (l = 0; l < 9; l++) {
		if (machine.is_sfp[l])
			S.sfp_ports |= v;
		v <<= 1;
	}
	S.ports_10g = 0;		/* OM_TEN_G(): logical 3, and 8 with two */
	if (machine.n_10g)
		S.ports_10g = machine.n_10g == 2 ? 0x108 : 0x008;
	S.image_size = IMAGE_SIZE;
	reg_read_m(RTL837X_REG_SEC_COUNTER);	/* big endian, into little */
	UP[0] = sfr_data[3];
	UP[1] = sfr_data[2];
	UP[2] = sfr_data[1];
	UP[3] = sfr_data[0];
	dp = (__xdata uint8_t *)S.mac;	/* aa:bb:cc:dd:ee:ff */
	for (b = 0; b < 6; b++) {
		l = uip_ethaddr.addr[b];
		*dp++ = hexd[l >> 4];
		*dp++ = hexd[l & 15];
		*dp++ = ':';
	}
	dp[-1] = 0;
	reg_read(RTL837X_TM_RESULT);	/* raw / 128 C, in tenths as `temp` rounded it */
	v = SFR_DATA_U16;
	if (v & 0x8000)
		v = 0;
	S.temp_c = (v * 5) >> 6;
	S.flash_size = flash_size;
	S.poe = E_POE_NONE;
#ifdef MACHINE_POE
#ifdef LAYOUT_COMPACT
	if (flash_size < MDATA_START + MDATA_LEN)
		S.poe = E_POE_NO_ROOM;
	else
#endif
		S.poe = poe_state + E_POE_NO_IMAGE;
#endif
	S.button = button_last;
	return OM_OK;
}

uint8_t om_get_loader(uint8_t idx) __banked
{
	(void)idx;
	om_st.loader.present = has_loader;
	return OM_OK;
}

/* the reset after the reply: the cli's prompt step, or httpd once it is ACKed */
uint8_t om_do_sys_reset(uint8_t idx) __banked
{
	(void)idx;
	crash_pending = cmd_capture ? CRASH_WEB : CRASH_RESET_CMD;
	return OM_OK;
}

/* the flash job writes the default config; then the same reset, cause factory */
uint8_t om_do_sys_factory_reset(uint8_t idx) __banked
{
	(void)idx;
	fl_req = FL_FACTORY;
	if (flash_job_start())
		return OM_OK;
	om_fail(OM_E_FLASH_BUSY);
	return OM_ERR;
}

uint8_t om_do_sys_rnd(uint8_t idx) __banked
{
	(void)idx;
	print_string("event sys.rnd ");
	/* a new random number needs the bit set each time */
	reg_bit_set(RTL837X_RLDP_RLPP, RLDP_RND_EN);
	reg_read_m(RTL837X_RAND_NUM1);
	print_byte(sfr_data[2]);
	print_byte(sfr_data[3]);
	reg_read_m(RTL837X_RAND_NUM0);
	for (b = 0; b < 4; b++)
		print_byte(sfr_data[b]);
	write_char('\n');
	return OM_OK;
}
