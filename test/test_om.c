/*
 * test_om.c - the object model's engine (om.c) on the host, against the
 * tables generated from the real schema and test/om_schema (build/om_t: every
 * object kind and type no real object uses yet). The handlers are stubs that
 * record their calls; the engine is the firmware source.
 *
 * Text in, text out: every case is a command line and the exact output.
 * Then a round-trip property (random valid values set as text, read back
 * as text and as bytes) and a fuzz loop (random lines; no crash, every
 * command ends in ok or err).
 */
#include <stdio.h>
#include <stdlib.h>
#include "support.h"
#include "rtl837x_common.h"
#include "machine.h"
#include "cmd_parser.h"
#include "om_gen.h"
#include "cmd_editor.h"

/* ---- the environment om.c links against ---------------------------------- */

/* an 8x-like board: nine ports, the ninth an SFP; physical n is logical n-1 */
const struct machine machine = {
	.machine_name = "test",
	.isRTL8373 = 1,
	.n_sfp = 1,
	.n_10g = 0,
	.log_to_phys_port = {1, 2, 3, 4, 5, 6, 7, 8, 9},
	.phys_to_log_port = {0, 1, 2, 3, 4, 5, 6, 7, 8},
	.is_sfp = {0, 0, 0, 0, 0, 0, 0, 0, 1},
};

uint8_t cmd_words_len;
uint8_t cmd_words_b[15];
uint8_t cli_job_sleep, cli_job_one, cmd_capture;
static uint16_t t_tx_room = 2048;	/* the console's TX ring: free */
uint16_t con_tx_room(void) { return t_tx_room; }

void itoa_short(uint16_t v)
{
	char b[6];
	int n = 0;
	do { b[n++] = '0' + v % 10; v /= 10; } while (v);
	while (n) write_char(b[--n]);
}

void print_byte(uint8_t a)
{
	static const char hex[] = "0123456789abcdef";
	write_char(hex[a >> 4]);
	write_char(hex[a & 15]);
}

void memcpyc(uint8_t *dst, const uint8_t *src, uint16_t len) { memcpy(dst, src, len); }

/* the flash (flash_dev.c's model) and the flash job's FL_SAVE, as flash_job.c runs it */
#include "rtl837x_flash.h"
#include "flash_job.h"
#include "layout.h"
#include "flash_dev.h"
#include <setjmp.h>
static flashdev_t fl;
static jmp_buf fl_cut;		/* a power cut: the CPU dies where it is */
struct flash_region_t flash_region;
uint8_t flash_buf[FLASH_BUF_SIZE];
void flash_read_bulk(uint8_t *dst) { fd_read(&fl, flash_region.addr, dst, flash_region.len); }
void flash_sector_erase(void) { fd_erase_sector(&fl, flash_region.addr); if (fl.powercut) longjmp(fl_cut, 1); }
static int old_ops;		/* program as before 4 B ops were cut at a page */
void flash_write_bytes(uint8_t *p) { fd_write_ops(&fl, flash_region.addr, p, flash_region.len, !old_ops); if (fl.powercut) longjmp(fl_cut, 1); }

uint8_t fl_req, fl_kind;
uint16_t fl_len;
static int fl_busy;		/* the job is taken by something else */

uint8_t flash_job_start(void)
{
	if (fl_kind || fl_busy)
		return 0;
	fl_kind = fl_req;
	return 1;
}

uint32_t fl_addr;

static void fl_run(void)	/* the job to its end, as flash_job.c steps it */
{
	if (fl_kind != FL_SAVE)
		return;
	fl_addr = CONFIG_START;
	flash_region.addr = fl_addr;
	flash_sector_erase();
	do {
		uint16_t n = om_save_chunk();
		flash_region.addr = fl_addr;
		flash_region.len = n;
		flash_write_bytes(flash_buf);
		fl_addr += n;
		fl_len -= n;
	} while (fl_len);
	while (om_save_end())
		;
	fl_kind = 0;
}

/* cmd_parser.c's tokenizer, as far as om.c needs it */
static void tokenize(void)
{
	uint8_t i = 0, white = 1;
	cmd_words_len = 0;
	for (; cmd_buffer[i]; i++) {
		if (white && cmd_buffer[i] != ' ')
			cmd_words_b[cmd_words_len++] = i;
		white = cmd_buffer[i] == ' ';
	}
}

void cmd_tokenize(void) { tokenize(); }

void cmd_finish(void)
{
	while (cli_job && om_line())
		;
	cli_job = 0;
}

/* ---- handler stubs ------------------------------------------------------- */

static int applied[OM_PORTS], checks, fail_check_on = -1, ct_calls, gets;
static struct cfg_port applied_old[OM_PORTS];

static int fail_apply_on = -1;
static struct cfg jset_before;	/* running before a window ends mid-J_SET */
static int vlan_checks;		/* the pvid and mgmt.vlan checks: the vlan tests' own */
static struct cfg_port applied_new[OM_PORTS];

uint8_t om_apply_port(uint8_t idx)
{
	applied[idx]++;
	applied_old[idx] = cfg_roll.port[idx];
	applied_new[idx] = cfg_run.port[idx];
	if (idx == fail_apply_on) {
		fail_apply_on = -1;	/* once: the rollback's own apply works */
		return OM_ERR;
	}
	return OM_OK;
}

uint8_t om_check_port(uint8_t idx)
{
	checks++;
	if (vlan_checks && !om_vlan_known(cfg_cand.port[idx].pvid)) {	/* as om_port.c */
		if (om_booting) {
			cfg_cand.port[idx].pvid = 1;
			om_boot_note(OM_E_PVID_ONE);
			if (om_vlan_known(1))
				return OM_OK;
			om_boot_ref = O_CFG_VLAN;
			return OM_ERR;
		}
		om_fail(OM_E_PVID);
		return OM_ERR;
	}
	if (idx == fail_check_on) {
		om_fail(OM_E_TEST);
		return OM_ERR;
	}
	return OM_OK;
}

static uint64_t st_tx_good = 1234567;
static uint16_t st_len = 123;

uint8_t om_get_port(uint8_t idx)
{
	gets++;
	memset(&om_st.port, 0, sizeof(om_st.port));
	om_st.port.link = idx == 1;
	om_st.port.speed = E_LSPEED_2G5;
	om_st.port.media = machine.is_sfp[idx] ? E_MEDIA_SFP : E_MEDIA_COPPER;
	om_st.port.macs = 7;
	memcpy(om_st.port.tx_good, &st_tx_good, 8);	/* little endian on x86 too */
	om_st.port.cabletest_pair_a_length_m = st_len;
	om_st.port.cabletest_pair_a_result = E_CTRES_OPEN;
	return OM_OK;
}

uint8_t om_do_port_cabletest(uint8_t idx)
{
	ct_calls++;
	return OM_OK;
}

/* sys, state.sys (om_sys.c, sched_health.c) */
static int sys_resets, sys_factory, sys_rnd;

uint8_t om_get_sys(uint8_t idx)
{
	memset(&om_st.sys, 0, sizeof(om_st.sys));
	strcpy(om_st.sys.version, "v0.1.0-test");
	om_st.sys.chip = E_CHIP_RTL8373;
	om_st.sys.ports = 0x1ff;
	om_st.sys.sfp_ports = 0x100;
	om_st.sys.uptime_s = 70000;
	om_st.sys.temp_c = 415;
	om_st.sys.flash_size = 0x200000;
	return OM_OK;
}

uint8_t om_get_sys_reset(uint8_t idx)
{
	memset(&om_st.sys_reset, 0, sizeof(om_st.sys_reset));
	om_st.sys_reset.cause = E_RCAUSE_WEB;
	om_st.sys_reset.slot = E_SSLOTN_B;		/* test_sched.c's tasks: a, b, w */
	om_st.sys_reset.states_7 = E_SSTATE_A_STEP;
	om_st.sys_reset.states_8 = E_SSTATE_B_STEP;
	return OM_OK;
}

uint8_t om_get_loader(uint8_t idx) { om_st.loader.present = 1; return OM_OK; }

uint8_t om_do_sys_reset(uint8_t idx) { sys_resets++; return OM_OK; }
uint8_t om_do_sys_factory_reset(uint8_t idx)
{
	sys_factory++;
	om_fail(OM_E_FLASH_BUSY);
	return OM_ERR;
}
uint8_t om_do_sys_rnd(uint8_t idx) { sys_rnd++; return OM_OK; }

/* the services (om_svc.c): what each apply saw */
static int t_syslog;
static struct cfg_syslog syslog_new;
uint8_t om_apply_syslog(uint8_t idx) { t_syslog++; syslog_new = cfg_run.syslog; return OM_OK; }
static int t_dns, t_ntp, t_web, t_mgmt, t_renew, t_sysa;
uint8_t om_apply_sys(uint8_t idx) { t_sysa++; return OM_OK; }
uint8_t om_check_sys(uint8_t idx) { if (strchr(cfg_cand.sys.hostname, ' ')) { om_fail(OM_E_HOSTNAME_SPACE); return OM_ERR; } return OM_OK; }
uint8_t om_derive_sys(uint8_t idx) { strcpy((char *)om_val, "RTLPlayground-0a0b0c"); return OM_OK; }
uint8_t om_apply_mgmt(uint8_t idx) { t_mgmt++; return OM_OK; }
uint8_t om_check_mgmt(uint8_t idx)	/* as om_svc.c */
{
	if (vlan_checks && !om_vlan_known(cfg_cand.mgmt.vlan)) {
		if (om_booting) {
			cfg_cand.mgmt.vlan = 0;
			om_boot_note(OM_E_MGMT_VLAN_ANY);
			return OM_OK;
		}
		om_fail(OM_E_MGMT_VLAN);
		return OM_ERR;
	}
	return OM_OK;
}

/* the VLAN table (rtl837x_port.c), under om_vlan.c: the calls, as text */
#include "rtl837x_port.h"
struct vlan_settings vlan_settings;
static char vlan_ops[4096];	/* 64 VLANs made at a boot: 13 B each */
void vlan_create(void)
{
	sprintf(vlan_ops + strlen(vlan_ops), "+%u %03x/%03x ", vlan_settings.vlan, vlan_settings.members,
		vlan_settings.tagged);
}
void vlan_delete(uint16_t vlan) { sprintf(vlan_ops + strlen(vlan_ops), "-%u ", vlan); }

/* the trunk and LACP (rtl837x_port.c, rtl837x_lacp.c), under om_lag.c: the calls, as text */
#include "rtl837x_regs.h"
volatile uint8_t SFR_DATA_24, SFR_DATA_16, SFR_DATA_8, SFR_DATA_0;
static char lag_ops[256];
void lacp_lag_set(uint8_t lag, uint16_t ports) { sprintf(lag_ops + strlen(lag_ops), "L%u %03x ", lag, ports); }
static uint16_t trunk[4];
static int two_trunks;		/* a port was in two trunks after a write */
void port_lag_members_set(uint8_t lag, uint16_t members)
{
	sprintf(lag_ops + strlen(lag_ops), "S%u %03x ", lag, members);
	trunk[lag] = members;
	for (int a = 0; a < 4; a++)
		for (int b = a + 1; b < 4; b++)
			if (trunk[a] & trunk[b])
				two_trunks = 1;
}
uint16_t port_lag_members_get(uint8_t lag) { return trunk[lag]; }
void reg_write(uint16_t reg)
{
	sprintf(lag_ops + strlen(lag_ops), "H%u %02x ", (reg - RTL837X_TRK_HASH_CTRL_BASE) >> 2, SFR_DATA_0);
}
/* the spanning tree (rtl837x_stp.c), under om_stp.c: its config, the calls as text */
#include "rtl837x_stp.h"
bool stp_enabled;
uint8_t stp_prio, stp_hello_s, stp_maxage_s, stp_fwddelay_s, stp_rstp, stp_txhold;
uint8_t stp_pcost_short, stp_bpdu_filter;
uint8_t stp_pflags[STP_ENTITIES], stp_pprio[STP_ENTITIES], stp_pp2p[STP_ENTITIES];
uint32_t stp_pcost[STP_ENTITIES];
static char stp_ops[256];
static uint16_t stp_gone;	/* entities that aren't (a LAG member, no such LAG) */
#define STP_OP(...)	sprintf(stp_ops + strlen(stp_ops), __VA_ARGS__)
void stp_setup(void) { STP_OP("on "); }
void stp_off(void) { STP_OP("off "); }
void stp_prio_apply(void) { STP_OP("prio "); }
void stp_lag_map(void) { }
void stp_counters_clear(void) { STP_OP("clear "); }
void stp_port_admin(uint8_t port, uint8_t on) { STP_OP("A%u %u ", port, on); }
void stp_port_mcheck(uint8_t port) { STP_OP("M%u ", port); }
void stp_ent_restart(uint8_t e) { STP_OP("R%u ", e); stp_pflags[e] &= ~STP_PF_TRIPPED; }
uint8_t stp_ent_id(uint8_t e) { return (stp_gone >> e & 1) ? 0 : e + 1; }
uint8_t om_do_mgmt_dhcp_renew(uint8_t idx)
{
	t_renew++;
	if (cfg_run.mgmt.mode != E_MGMTMODE_DHCP) {
		om_fail(OM_E_MGMT_STATIC);
		return OM_ERR;
	}
	return OM_OK;
}
uint8_t om_apply_web(uint8_t idx) { t_web++; return OM_OK; }
uint8_t om_apply_ntp(uint8_t idx) { t_ntp++; return OM_OK; }
uint8_t om_do_ntp_sync(uint8_t idx) { if (!cfg_run.ntp.enabled) { om_fail(OM_E_NTP_OFF); return OM_ERR; } return OM_OK; }
uint8_t om_apply_dns(uint8_t idx) { t_dns++; return OM_OK; }
/* igmp, lldp, loop, mirror (rtl837x_igmp.c, rtl837x_lldp.c, rtl837x_rldp.c, rtl837x_port.c) */
static int t_igmp;
uint8_t om_apply_igmp(uint8_t idx) { t_igmp++; return OM_OK; }
static int t_lldp;
uint8_t om_apply_lldp(uint8_t idx) { t_lldp++; return OM_OK; }
static int t_loop;
uint8_t om_apply_loop(uint8_t idx) { t_loop++; return OM_OK; }
static int t_mirror;
uint8_t om_apply_mirror(uint8_t idx) { t_mirror++; return OM_OK; }
uint8_t om_check_mirror(uint8_t idx)	/* as om_port.c */
{
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

/* state.health, state.con, do sched.clear (om_health.c, sched_health.c) */
static int sched_clears;
uint8_t om_get_health(uint8_t idx) { memset(&om_st.health, 0, sizeof(om_st.health)); om_st.health.hw_timeouts = 2; om_st.health.stack_headroom = 40; return OM_OK; }
uint8_t om_get_con(uint8_t idx) { memset(&om_st.con, 0, sizeof(om_st.con)); om_st.con.tx_free = 2047; return OM_OK; }
uint8_t om_do_sched_clear(uint8_t idx) { sched_clears++; return OM_OK; }
/* state.dbg, state.dbg.igmp.port (om_dbg.c): raw words */
uint8_t om_get_dbg(uint8_t idx) { om_st.dbg.sds_modes = 0x00001a2b; om_st.dbg.gpio_0 = 0; om_st.dbg.gpio_1 = 0xffffffff; return OM_OK; }
uint8_t om_get_dbg_igmp_port(uint8_t idx) { om_st.dbg_igmp_port.cfg = 0x40000000 | idx; return OM_OK; }
/* do l2.flush, do l2.delete (om_port.c): the argument they were given */
static int l2_arg = -1;
uint8_t om_do_l2_flush(uint8_t idx) { l2_arg = om_key; return OM_OK; }
uint8_t om_do_l2_delete(uint8_t idx)
{
	l2_arg = om_key;
	if (om_key != 0x20) { om_fail(OM_E_L2_NONE); return OM_ERR; }
	return OM_OK;
}
/* state.mgmt, state.ntp, state.dns, state.loop (dhcp.c, ntp.c, dns.c, rtl837x_rldp.c) */
uint8_t om_get_mgmt(uint8_t idx)
{
	static const uint8_t a[12] = { 192, 168, 2, 3, 255, 255, 255, 0, 192, 168, 2, 1 };
	memset(&om_st.mgmt, 0, sizeof(om_st.mgmt));
	memcpy(om_st.mgmt.ip, a, 4);
	memcpy(om_st.mgmt.netmask, a + 4, 4);
	memcpy(om_st.mgmt.gateway, a + 8, 4);
	om_st.mgmt.dhcp_state = 4;
	om_st.mgmt.dhcp_renew_s = 3600;
	return OM_OK;
}
uint8_t om_get_ntp(uint8_t idx)
{
	memset(&om_st.ntp, 0, sizeof(om_st.ntp));
	om_st.ntp.synced = 1;
	om_st.ntp.time = 1795160000;
	om_st.ntp.local_offset_min = -300;
	om_st.ntp.stratum = 2;
	return OM_OK;
}
uint8_t om_get_dns(uint8_t idx)
{
	memset(&om_st.dns, 0, sizeof(om_st.dns));
	om_st.dns.lookup_status = 3;
	return OM_OK;
}
uint8_t om_get_loop(uint8_t idx) { om_st.loop.looped = 0x0003; om_st.loop.blocked = 0x0002; return OM_OK; }
/* state.stp, state.lag, state.lacp.port (rtl837x_stp.c, rtl837x_lacp.c) */
uint8_t om_get_stp(uint8_t idx)
{
	static const uint8_t b[8] = { 0x80, 0x00, 0x06, 0xeb, 0x00, 0xb5, 0x32, 0x5d };
	memset(&om_st.stp, 0, sizeof(om_st.stp));
	memcpy(om_st.stp.bridge, b, 8);
	memcpy(om_st.stp.root, b, 8);
	om_st.stp.root[0] = 0x10;
	om_st.stp.root_port = 11;		/* lag.2 */
	om_st.stp.root_cost = 20000;
	om_st.stp.tc_count = 3;
	om_st.stp.tc_age_s = 90061;
	om_st.stp.root_maxage_s = 20;
	om_st.stp.root_fwd_s = 15;
	return OM_OK;
}
uint8_t om_get_stp_port(uint8_t idx)
{
	memset(&om_st.stp_port, 0, sizeof(om_st.stp_port));
	om_st.stp_port.state = idx == 1 ? 3 : 1;
	om_st.stp_port.role = idx == 1 ? 1 : 3;
	om_st.stp_port.rx_bpdu = 70000;
	om_st.stp_port.link = idx == 1;
	om_st.stp_port.guard_tripped = idx == 2;
	om_st.stp_port.dbridge[0] = 0x10;
	memcpy(om_st.stp_port.dbridge + 2, "\x06\xeb\x00\xb5\x32\x5d", 6);
	om_st.stp_port.dport = 0x8003;
	om_st.stp_port.dcost = 20000;
	return OM_OK;
}
uint8_t om_get_stp_lag(uint8_t idx)
{
	memset(&om_st.stp_lag, 0, sizeof(om_st.stp_lag));
	om_st.stp_lag.members = idx == 3 ? 0x0003 : 0;
	return OM_OK;
}
uint8_t om_get_lag(uint8_t idx)
{
	static const uint8_t m[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
	memset(&om_st.lag, 0, sizeof(om_st.lag));
	if (idx == 0) {
		om_st.lag.active = 0x0003;
		om_st.lag.agg_valid = 1;
		memcpy(om_st.lag.aggregator, m, 6);
	}
	return OM_OK;
}
uint8_t om_get_lacp_port(uint8_t idx)
{
	memset(&om_st.lacp_port, 0, sizeof(om_st.lacp_port));
	om_st.lacp_port.lag = idx < 2;
	om_st.lacp_port.actor = 0x3d;
	om_st.lacp_port.rx_state = 4;
	return OM_OK;
}
/* state.sfp (sfp.c) */
uint8_t om_get_sfp(uint8_t idx)
{
	memset(&om_st.sfp, 0, sizeof(om_st.sfp));
	if (idx)
		return OM_OK;
	om_st.sfp.present = om_st.sfp.ready = om_st.sfp.id_ok = om_st.sfp.ddm_ok = 1;
	om_st.sfp.los = 2;
	strcpy((char *)om_st.sfp.vendor, "RTLSIM");
	om_st.sfp.rate_mbps = 10300;
	om_st.sfp.ddm_temp = 0x1880;
	om_st.sfp.ddm_vcc_v = 33000;
	om_st.sfp.ddm_rx_power_mw = 5012;
	om_st.sfp.ddm_status = 0x82;
	return OM_OK;
}
/* state.l2 (om_port.c): a table of three, found from om_key as the chip's search */
static uint16_t l2_ix[40] = { 0x010, 0x020, 0x8f0 };
static int l2_n = 3, l2_calls, l2_change_at;
uint8_t om_get_l2(uint8_t idx)
{
	l2_calls++;
	if (l2_calls == l2_change_at) {	/* the chip learns 0x005 (below the cursor), ages out 0x8f0 (ahead) */
		memmove(l2_ix + 1, l2_ix, 2 * sizeof(l2_ix[0]));
		l2_ix[0] = 0x005;
		l2_n = 3;
	}
	for (int i = 0; i < l2_n; i++) {
		if (l2_ix[i] >= om_key) {
			om_key = l2_ix[i];
			memset(&om_st.l2, 0, sizeof(om_st.l2));
			om_st.l2.mac[0] = 2;
			om_st.l2.mac[5] = i;
			om_st.l2.vlan = 1;
			om_st.l2.port = i == 1 ? CPU_LOGICAL_PORT : 4;
			om_st.l2.static_ = i == 1;
			return OM_OK;
		}
	}
	return OM_ERR;			/* wrapped */
}
/* state.port.N.mib.K (om_port.c): the word says which port and index it was read for */
static int mib_calls;
uint8_t om_get_port_mib(uint8_t idx)
{
	uint64_t v = (uint64_t)idx << 32 | om_ix1;
	mib_calls++;
	memcpy(om_st.port_mib.v, &v, 8);
	return OM_OK;
}
/* state.sched (sched_health.c): the rows are test_sched.c's tasks' */
uint8_t om_get_sched(uint8_t idx) { memset(&om_st.sched, 0, sizeof(om_st.sched)); om_st.sched.slots = 8; return OM_OK; }
uint8_t om_get_sched_slot(uint8_t idx)
{
	memset(&om_st.sched_slot, 0, sizeof(om_st.sched_slot));
	if (idx == 0) {
		om_st.sched_slot.task = E_STASK_A;
		om_st.sched_slot.state = E_SSTATE_A_STEP;
		om_st.sched_slot.mask = 0x82;
	}
	return OM_OK;
}
uint8_t om_get_sched_state(uint8_t idx) { om_st.sched_state.runs = idx; om_st.sched_state.max_us = 402640; return OM_OK; }
uint8_t om_get_sched_task(uint8_t idx) { om_st.sched_task.max_us = 10 + idx; return OM_OK; }

/* the test schema's (om_schema/t.py) */
static int t_sys, t_rnd, t_lag[4], t_stpp[OM_PORTS], t_storm[OM_PORTS * 4], t_vlan;
static uint16_t t_vlan_old[4], t_vlan_new[4];

uint8_t om_apply_tsys(uint8_t idx) { t_sys++; return OM_OK; }

uint8_t om_check_tsys(uint8_t idx)
{
	if (cfg_cand.tsys.offset == 13) {
		om_fail(OM_E_TEST);
		return OM_ERR;
	}
	return OM_OK;
}

uint8_t om_derive_tsys(uint8_t idx)
{
	if (om_fid != 1)	/* hostname */
		return OM_ERR;
	const char *d = "RTLPlayground-a225fa";
	int n = 0;
	do om_val[n] = d[n]; while (d[n++]);
	return OM_OK;
}

uint8_t om_do_tsys_rnd(uint8_t idx) { t_rnd++; return OM_OK; }

uint8_t om_check_tvlan(uint8_t idx)
{
	if (cfg_run.tvlan[idx].key == 4000 && !cfg_cand.tvlan[idx].key) {
		om_fail(OM_E_TVLAN_KEEP);
		return OM_ERR;
	}
	return OM_OK;
}

uint8_t om_apply_tvlan(uint8_t idx)
{
	t_vlan++;
	t_vlan_old[idx] = cfg_roll.tvlan[idx].key;
	t_vlan_new[idx] = cfg_run.tvlan[idx].key;
	return OM_OK;
}

uint8_t om_apply_tlag(uint8_t idx) { t_lag[idx]++; return OM_OK; }
uint8_t om_apply_tstp_port(uint8_t idx) { t_stpp[idx]++; return OM_OK; }
uint8_t om_apply_port_storm(uint8_t idx) { t_storm[idx]++; return OM_OK; }
uint8_t om_check_port_storm(uint8_t idx) { (void)idx; return OM_OK; }

uint8_t om_get_tsys(uint8_t idx)
{
	static const uint8_t br[8] = {0x80, 0x00, 0x20, 0x7c, 0x14, 0xa2, 0x25, 0xfa};
	memcpy(om_st.tsys.bridge, br, 8);
	om_st.tsys.ip[0] = 10;
	om_st.tsys.ip[3] = 7;
	om_st.tsys.offset = -5;
	om_st.tsys.lp = 0x1c0;		/* 2g5 5g 10g */
	return OM_OK;
}

/* om_job.c's cfg job, as the scheduler would run it: apply, window, undo */
static int job_on, job_st, job_steps, job_wakes, job_due, job_arg;

uint8_t om_job_start(void)
{
	if (job_on)
		return 0;
	job_on = 1;
	job_st = 0;
	job_arg = 0;
	return 1;
}

void om_job_wake(void) { job_wakes++; }

static void job_step(void)
{
	uint8_t r;
	if (!job_on)
		return;
	job_steps++;
	switch (job_st) {
	case 0:			/* CFG_APPLY */
		r = om_apply_next();
		if (r == OM_A_MORE)
			return;
		if (r == OM_A_FAIL) {
			om_undo();
			job_st = 2;
			return;
		}
		if (!om_confirm_s) {
			om_settle(r);
			job_st = 3;
			return;
		}
		om_apply_st = E_CFGAPPLY_CONFIRM;
		job_st = 1;
		return;
	case 1:			/* CFG_WINDOW: due a second at a time (seconds()), then 100 ms */
		if (om_confirmed) {
			om_settle(OM_A_DONE);
			job_st = 3;
			return;
		}
		if (om_confirm_s) {
			if (!job_due)
				return;
			job_due = 0;
			if (--om_confirm_s)
				return;
		}
		if (cli_job == CLI_JOB_OM) {
			if (job_arg < 50) {
				job_arg++;
				return;
			}
			om_cut = 1;
		}
		om_undo();
		job_st = 2;
		return;
	case 2:			/* CFG_UNDO */
		r = om_apply_next();
		if (r == OM_A_MORE)
			return;
		om_settle(r);
		job_st = 3;
		return;
	default:		/* CFG_SETTLE */
		if (!om_copy())
			job_on = 0;
	}
}

static void job_run(void)	/* the job to its end, or into its window */
{
	while (job_on && !(job_st == 1 && !om_confirmed && om_confirm_s))
		job_step();
}

static void seconds(int n)	/* the window's clock */
{
	while (n-- && job_on && job_st == 1) {
		job_due = 1;
		job_step();
	}
	job_run();
}

/* ---- driving it ---------------------------------------------------------- */

static const char *run(const char *line)
{
	out_reset();
	snprintf((char *)cmd_buffer, CMD_BUF_SIZE, "%s", line);
	err_status = 0;
	tokenize();
	om_cmd();
	while (cli_job) {
		job_step();		/* the cfg job and the cli take turns */
		if (fl_kind)
			fl_run();	/* and the flash job */
		if (!om_line())
			cli_job = 0;
	}
	job_run();
	out_buf[out_len] = 0;
	return out_buf;
}

static const char *prompt(void)
{
	out_reset();
	om_prompt();
	out_buf[out_len] = 0;
	return out_buf;
}

static void expect(const char *line, const char *want)
{
	const char *got = run(line);
	char msg[256];
	snprintf(msg, sizeof msg, "%s", line);
	CHECK(!t_strcmp(got, want), msg);
	if (t_strcmp(got, want))
		printf("        got:  \"%s\"\n        want: \"%s\"\n", got, want);
}

/* the output ends in exactly one status line */
static int status_ok(const char *o)
{
	size_t n = t_strlen(o);
	if (n < 3 || o[n - 1] != '\n')
		return 0;
	const char *last = o + n - 1;
	while (last > o && last[-1] != '\n')
		last--;
	return !strncmp(last, "ok\n", 3) || !strncmp(last, "err ", 4) || !strncmp(last, "err:", 4);
}

/* port.N.storm.T, at its defaults */
#define STORM1(n, t) "port." n ".storm." t ".rate 0\nport." n ".storm." t ".unit pps\n"
#define STORM(n) STORM1(n, "bcast") STORM1(n, "mcast") STORM1(n, "ucast") STORM1(n, "umcast")

static void cases(void)
{
	om_init();

	{	/* om_gen's chains: each tree and first letter's objects, in table order */
		int bad = 0;
		for (int t = 0; t < 2; t++)
			for (int c = 0; c < 26; c++) {
				int o = om_fc[t * 26 + c];
				for (int i = 0; i < OM_N_OBJ; i++)
					if (!!(om_obj[i].flags & OM_STATE) == t && om_obj[i].name[0] == 'a' + c) {
						bad += o != i;
						o = o < OM_N_OBJ ? om_nx[o] : 0xff;
					}
				bad += o != 0xff;
			}
		CHECK(!bad, "the chains (om_fc, om_nx) are a linear scan's objects, in its order");
	}

	printf("get\n");
	expect("get port.3.mtu", "port.3.mtu 16383\nok\n");
	expect("get port.9.eee", "err port.9.eee: not on this port\n");
	expect("get port.3.isolate", "port.3.isolate 1-9,cpu\nok\n");
	expect("get port.2", "port.2.name \nport.2.enabled on\nport.2.speed auto\nport.2.duplex auto\n"
	       "port.2.mtu 16383\nport.2.pvid 1\nport.2.ingress all\nport.2.ingress_filter on\n"
	       "port.2.lldp permit\nport.2.eee on\nport.2.eee_speeds 100m,1g,2g5,5g,10g\nport.2.isolate 1-9,cpu\n"
	       "port.2.bw_in_kbps 0\nport.2.bw_in_mode drop\nport.2.bw_out_kbps 0\n" STORM("2") "ok\n");
	expect("get port.9", "port.9.name \nport.9.speed auto\nport.9.mtu 16383\nport.9.pvid 1\nport.9.ingress all\n"
	       "port.9.ingress_filter on\nport.9.lldp permit\nport.9.isolate 1-9,cpu\n"
	       "port.9.bw_in_kbps 0\nport.9.bw_in_mode drop\nport.9.bw_out_kbps 0\n" STORM("9") "ok\n");
	expect("get port.*.pvid", "port.1.pvid 1\nport.2.pvid 1\nport.3.pvid 1\nport.4.pvid 1\n"
	       "port.5.pvid 1\nport.6.pvid 1\nport.7.pvid 1\nport.8.pvid 1\nport.9.pvid 1\nok\n");
	{
		char all[512];
		snprintf(all, sizeof all, "%s", run("get port.*.pvid"));
		expect("get port.pvid", all);
	}
	expect("get lacp.1", "err lacp.1: sched l2 sys tsys tvlan.* tlag.* tstp tstp.port.* vlan.* mgmt dns ntp syslog web lag.* port.* port.*.storm.* stp stp.lag.* stp.port.* igmp lldp loop mirror\n");
	expect("get port.10", "err port.10: no such port\n");
	expect("get port.0.mtu", "err port.0.mtu: no such port\n");
	expect("get", "sched\nl2\nsys\ntsys\ntvlan.*\ntlag.*\ntstp\ntstp.port.*\nvlan.*\nmgmt\nstate.port.*\nstate.cfg\nstate.dbg\nstate.dbg.igmp.port.*\nstate.health\nstate.con\ndns\nstate.l2.*\nntp\nstate.lag.*\nstate.lacp.port.*\nstate.mgmt\nstate.port.*.mib.*\nstate.sfp.*\nstate.stp\nstate.stp.port.*\nstate.stp.lag.*\nstate.ntp\nstate.dns\nstate.loop\nstate.sys\nstate.sys.reset\nstate.loader\nsyslog\nweb\nlag.*\nport.*\nport.*.storm.*\nstp\nstp.lag.*\nstate.sched\nstate.sched.slot.*\nstate.sched.state.*\nstate.sched.task.*\nstp.port.*\nigmp\nlldp\nloop\nmirror\n"
	       "state.tsys\nok\n");

	printf("set, diff, apply\n");
	expect("set port.3.mtu 1500", "ok\n");
	expect("get port.3.mtu", "port.3.mtu 16383\nok\n");
	expect("get -c port.3.mtu", "port.3.mtu 1500\nok\n");
	expect("diff", "- port.3.mtu 16383\n+ port.3.mtu 1500\nok\n");
	checks = 0;
	expect("apply", "ok\n");
	CHECK(applied[2] == 1 && applied_old[2].mtu == 16383 && cfg_run.port[2].mtu == 1500,
	      "apply: port 3 applied once, the old value in roll");
	CHECK(checks == 9, "apply: every port checked");
	expect("diff", "ok\n");
	expect("apply", "ok\n");
	CHECK(applied[2] == 1, "apply with no change applies nothing");

	printf("values\n");
	expect("set port.3.mtu 63", "err port.3.mtu: 64-16383\n");
	expect("set port.3.mtu 16384", "err port.3.mtu: 64-16383\n");
	expect("set port.3.mtu 99999999999", "err port.3.mtu: 64-16383\n");
	expect("set port.3.mtu abc", "err port.3.mtu: 64-16383\n");
	expect("set port.3.mtu 1500 00", "err port.3.mtu: one value\n");
	expect("set port.3.mtu", "err port.3.mtu: 64-16383\n");
	expect("set port.3.pvid 0", "ok\n");
	expect("set port.3.pvid 4094", "ok\n");
	expect("set port.3.pvid 4095", "err port.3.pvid: 0-4094\n");
	expect("set port.1.speed fast", "err port.1.speed: auto 10m 100m 1g 2g5 5g 10g\n");
	expect("set port.1.speed 100m", "ok\n");
	expect("get -c port.1.speed", "port.1.speed 100m\nok\n");
	expect("set port.1.eee maybe", "err port.1.eee: on off\n");
	expect("set port.1.eee off", "ok\n");
	expect("set port.1.name my uplink", "ok\n");
	expect("get -c port.1.name", "port.1.name my uplink\nok\n");
	expect("set port.1.name say \"hi\"", "err port.1.name: printable text without \" or \\, at most its length\n");
	expect("set port.1.name 0123456789012345678901234567890", "ok\n");
	expect("set port.1.name 01234567890123456789012345678901",
	       "err port.1.name: printable text without \" or \\, at most its length\n");
	expect("set port.1.isolate 1-4,9,cpu", "ok\n");
	expect("get -c port.1.isolate", "port.1.isolate 1-4,9,cpu\nok\n");
	expect("set port.1.isolate none", "ok\n");
	expect("get -c port.1.isolate", "port.1.isolate none\nok\n");
	expect("set port.1.isolate all", "ok\n");
	expect("get -c port.1.isolate", "port.1.isolate 1-9,cpu\nok\n");
	expect("set port.1.isolate 2,4,6-8", "ok\n");
	expect("get -c port.1.isolate", "port.1.isolate 2,4,6-8\nok\n");
	expect("set port.1.isolate 0", "err port.1.isolate: ports like 1-4,9 (none, all, cpu)\n");
	expect("set port.1.isolate 3-1", "err port.1.isolate: ports like 1-4,9 (none, all, cpu)\n");
	expect("set port.1.isolate 1,", "err port.1.isolate: ports like 1-4,9 (none, all, cpu)\n");
	expect("set port.1.isolate 10", "err port.1.isolate: ports like 1-4,9 (none, all, cpu)\n");
	expect("set port.1.nonesuch 1", "err port.1.nonesuch: name enabled speed duplex mtu pvid ingress ingress_filter lldp eee eee_speeds isolate bw_in_kbps bw_in_mode bw_out_kbps cabletest\n");
	expect("set port.1", "err port.1: name enabled speed duplex mtu pvid ingress ingress_filter lldp eee eee_speeds isolate bw_in_kbps bw_in_mode bw_out_kbps cabletest\n");
	expect("set state.port.1.link up", "err state.port.1.link: read only\n");
	expect("set", "err: a path\n");

	printf("when, wildcards, del, discard\n");
	expect("set port.9.eee off", "err port.9.eee: not on this port\n");
	expect("set port.*.eee off", "ok\n");
	CHECK(cfg_cand.port[7].eee == 0 && cfg_cand.port[8].eee == 1, "port.*.eee off skips the SFP port");
	expect("set port.*.mtu 9000", "ok\n");
	expect("del port.3.mtu", "ok\n");
	expect("get -c port.3.mtu", "port.3.mtu 16383\nok\n");
	expect("get -c port.4.mtu", "port.4.mtu 9000\nok\n");
	expect("discard", "ok\n");
	expect("diff", "ok\n");

	printf("check\n");
	expect("set port.5.pvid 7", "ok\n");
	fail_check_on = 6;
	expect("apply", "err port.7: test check failed\n");
	CHECK(cfg_run.port[4].pvid == 1 && cfg_cand.port[4].pvid == 7, "a failed check applies nothing");
	fail_check_on = -1;
	expect("apply", "ok\n");
	CHECK(cfg_run.port[4].pvid == 7, "then it applies");

	printf("state\n");
	gets = 0;
	expect("get state.port.2.link", "state.port.2.link up\nok\n");
	CHECK(gets == 1, "a state get calls the getter once");
	expect("get state.port.3.cabletest.pair.a",
	       "state.port.3.cabletest.pair.a.result open\nstate.port.3.cabletest.pair.a.length_m 12.3\nok\n");
	st_len = 5;
	expect("get state.port.3.cabletest.pair.a.length_m", "state.port.3.cabletest.pair.a.length_m 0.5\nok\n");
	st_tx_good = 18446744073709551615ULL;
	expect("get state.port.3.tx_good", "state.port.3.tx_good 18446744073709551615\nok\n");
	st_tx_good = 0;
	expect("get state.port.3.tx_good", "state.port.3.tx_good 0\nok\n");
	gets = 0;
	run("get state.port");
	CHECK(gets == 9, "get state.port: the getter once per port");
	expect("get state.port.9.cabletest", "err state.port.9.cabletest: not on this port\n");
	expect("get state.port.9.media", "state.port.9.media sfp\nok\n");

	printf("streamed: state.l2\n");
	l2_calls = 0;
	expect("get state.l2.mac", "state.l2.16.mac 02:00:00:00:00:00\nstate.l2.32.mac 02:00:00:00:00:01\n"
	       "state.l2.2288.mac 02:00:00:00:00:02\nok\n");
	CHECK(l2_calls == 4, "a search an entry, and the one that wraps");
	expect("get state.l2.32", "state.l2.32.mac 02:00:00:00:00:01\nstate.l2.32.vlan 1\nstate.l2.32.port cpu\n"
	       "state.l2.32.lag 0\nstate.l2.32.static on\nok\n");
	expect("get state.l2.16.port", "state.l2.16.port 5\nok\n");
	expect("get state.l2.33", "err state.l2.33: no such entry\n");
	expect("get state.l2.4096", "err state.l2.4096: 0-4095\n");
	l2_n = 0;
	expect("get state.l2", "ok\n");
	l2_n = 3;
	/* the table changes between two steps of a walk: the new entry below
	 * the cursor isn't listed, the lost one ahead isn't either, the walk
	 * ends and lists no index twice */
	l2_calls = 0;
	l2_change_at = 2;
	expect("get state.l2.mac", "state.l2.16.mac 02:00:00:00:00:00\nstate.l2.32.mac 02:00:00:00:00:02\nok\n");
	CHECK(l2_calls == 3, "and searched once an entry, then the wrap");
	l2_change_at = 0;
	memcpy(l2_ix, (uint16_t[]){ 0x010, 0x020, 0x8f0 }, 3 * sizeof(l2_ix[0]));

	printf("streamed: a page, get state.l2.K-\n");
	expect("get state.l2.0-.mac", "state.l2.16.mac 02:00:00:00:00:00\nstate.l2.32.mac 02:00:00:00:00:01\n"
	       "state.l2.2288.mac 02:00:00:00:00:02\nok\n");
	expect("get state.l2.17-.vlan", "state.l2.32.vlan 1\nstate.l2.2288.vlan 1\nok\n");
	expect("get state.l2.16-.vlan", "state.l2.16.vlan 1\nstate.l2.32.vlan 1\nstate.l2.2288.vlan 1\nok\n");
	expect("get state.l2.2289-", "ok\n");
	expect("get state.l2.4096-", "err state.l2.4096-: 0-4095\n");
	expect("get port.3-.mtu", "err port.3-.mtu: no such port\n");
	{
		static char all[4096], pages[4096];
		int n = 0, from = 0, k;
		for (k = 0; k < 40; k++)
			l2_ix[k] = 0x100 + 3 * k;
		l2_n = 40;
		snprintf(all, sizeof all, "%s", run("get state.l2.vlan"));
		pages[0] = 0;
		for (k = 0; k < 4; k++) {
			char q[32];
			const char *o, *l;
			snprintf(q, sizeof q, "get state.l2.%d-.vlan", from);
			o = run(q);
			n = 0;
			for (l = o; (l = strstr(l, "state.l2.")); l++, n++)
				from = atoi(l + 9) + 1;
			strncat(pages, o, t_strlen(o) - 3);	/* its "ok\n" aside */
			if (n < OM_PAGE)
				break;
		}
		strcat(pages, "ok\n");
		CHECK(k == 1 && n == 15, "40 entries: a page of 25, then a short one of 15");
		CHECK(!strcmp(pages, all), "the pages, one after the other, are the whole stream");
		l2_n = 3;
		memcpy(l2_ix, (uint16_t[]){ 0x010, 0x020, 0x8f0 }, 3 * sizeof(l2_ix[0]));
	}

	printf("two levels from 0: state.port.N.mib.K\n");
	mib_calls = 0;
	expect("get state.port.3.mib.17", "state.port.3.mib.17 8589934609\nok\n");	/* logical 2, 17 */
	expect("get state.port.mib.54", "state.port.1.mib.54 54\nstate.port.2.mib.54 4294967350\n"
	       "state.port.3.mib.54 8589934646\nstate.port.4.mib.54 12884901942\nstate.port.5.mib.54 17179869238\n"
	       "state.port.6.mib.54 21474836534\nstate.port.7.mib.54 25769803830\nstate.port.8.mib.54 30064771126\n"
	       "state.port.9.mib.54 34359738422\nok\n");
	expect("get state.port.3.mib.55", "err state.port.3.mib.55: 0-54\n");
	mib_calls = 0;
	run("get state.port.1.mib");
	CHECK(mib_calls == 55, "get state.port.1.mib: the getter once an index");

	printf("do\n");
	expect("do port.2.cabletest", "ok\n");
	CHECK(ct_calls == 1, "do calls the action once");
	expect("do port.9.cabletest", "err port.9.cabletest: not on this port\n");
	expect("do port.2.mtu", "err port.2.mtu: not an action\n");
	expect("set port.2.cabletest 1", "err port.2.cabletest: an action: do it\n");
	expect("get port.2.cabletest", "err port.2.cabletest: name enabled speed duplex mtu pvid ingress ingress_filter lldp eee eee_speeds isolate bw_in_kbps bw_in_mode bw_out_kbps cabletest\n");
}

/* the kinds and types of the test schema */
static void kinds(void)
{
	om_init();

	printf("singleton, types\n");
	expect("get tsys", "tsys.hostname RTLPlayground-a225fa\ntsys.ip 192.168.2.2\ntsys.mask 255.255.255.0\n"
	       "tsys.mac auto\ntsys.password *\ntsys.offset 0\ntsys.tz +00:00\ntsys.mirror none\n"
	       "tsys.led off\ntsys.hash smac,dmac\ntsys.lp none\nok\n");
	CHECK(cfg_run.tsys.led == E_TLED_OFF, "a per-chipset default: the RTL8373's");
	CHECK(!cfg_run.tsys.hostname[0], "a derived field is stored empty");
	expect("set tsys.hostname sw1", "ok\n");
	expect("get -c tsys.hostname", "tsys.hostname sw1\nok\n");
	expect("del tsys.hostname", "ok\n");
	expect("get -c tsys.hostname", "tsys.hostname RTLPlayground-a225fa\nok\n");

	expect("set tsys.ip 10.0.0.1", "ok\n");
	expect("get -c tsys.ip", "tsys.ip 10.0.0.1\nok\n");
	CHECK(cfg_cand.tsys.ip[0] == 10 && cfg_cand.tsys.ip[3] == 1, "IPv4 in memory order");
	expect("set tsys.ip 10.0.0", "err tsys.ip: a.b.c.d\n");
	expect("set tsys.ip 10.0.0.256", "err tsys.ip: a.b.c.d\n");
	expect("set tsys.ip 10.0.0.1.5", "err tsys.ip: one value\n");
	expect("set tsys.ip a.b.c.d", "err tsys.ip: a.b.c.d\n");
	expect("set tsys.mask 255.255.0.0", "ok\n");
	expect("set tsys.mask 255.255.255.255", "ok\n");
	expect("set tsys.mask 0.0.0.0", "ok\n");
	expect("set tsys.mask 255.255.255.248", "ok\n");
	expect("get -c tsys.mask", "tsys.mask 255.255.255.248\nok\n");
	expect("set tsys.mask 255.0.255.0", "err tsys.mask: a.b.c.d, contiguous\n");
	expect("set tsys.mask 255.255.255.250", "err tsys.mask: a.b.c.d, contiguous\n");

	expect("set tsys.mac 20:7C:14:a2:25:FA", "ok\n");
	expect("get -c tsys.mac", "tsys.mac 20:7c:14:a2:25:fa\nok\n");
	expect("set tsys.mac 20-7c-14-a2-25-0f", "ok\n");
	expect("get -c tsys.mac", "tsys.mac 20:7c:14:a2:25:0f\nok\n");
	expect("set tsys.mac 20:7c:14:a2:25", "err tsys.mac: aa:bb:cc:dd:ee:ff or auto\n");
	expect("set tsys.mac 20:7c:14:a2:25:fg", "err tsys.mac: aa:bb:cc:dd:ee:ff or auto\n");
	expect("set tsys.mac auto", "ok\n");
	expect("get -c tsys.mac", "tsys.mac auto\nok\n");

	expect("set tsys.password s3cret", "ok\n");
	expect("get -c tsys.password", "tsys.password *\nok\n");
	CHECK(!t_strcmp(cfg_cand.tsys.password, "s3cret"), "a secret is stored as typed");
	expect("set tsys.password two words", "err tsys.password: printable text without \" \\ or a space, at most its length\n");
	expect("set tsys.password 012345678901234567890", "err tsys.password: printable text without \" \\ or a space, at most its length\n");

	expect("set tsys.offset -600", "ok\n");
	expect("get -c tsys.offset", "tsys.offset -600\nok\n");
	CHECK(cfg_cand.tsys.offset == -600, "I16 stored as int16_t");
	expect("set tsys.offset +5", "ok\n");
	expect("get -c tsys.offset", "tsys.offset 5\nok\n");
	expect("set tsys.offset 601", "err tsys.offset: -600..600\n");
	expect("set tsys.offset -601", "err tsys.offset: -600..600\n");
	expect("set tsys.offset -40000", "err tsys.offset: -600..600\n");
	expect("set tsys.offset --1", "err tsys.offset: -600..600\n");
	expect("set tsys.offset -", "err tsys.offset: -600..600\n");

	expect("set tsys.tz +05:30", "ok\n");
	expect("get -c tsys.tz", "tsys.tz +05:30\nok\n");
	CHECK(cfg_cand.tsys.tz == 330, "TZ in minutes");
	expect("set tsys.tz -03:30", "ok\n");
	expect("get -c tsys.tz", "tsys.tz -03:30\nok\n");
	expect("set tsys.tz 05:45", "ok\n");
	expect("get -c tsys.tz", "tsys.tz +05:45\nok\n");
	expect("set tsys.tz -00:00", "ok\n");
	expect("get -c tsys.tz", "tsys.tz +00:00\nok\n");
	expect("set tsys.tz +14:00", "ok\n");
	expect("set tsys.tz -12:00", "ok\n");
	expect("set tsys.tz +14:30", "err tsys.tz: -12:00..+14:00\n");
	expect("set tsys.tz -12:30", "err tsys.tz: -12:00..+14:00\n");
	expect("set tsys.tz +05:20", "err tsys.tz: -12:00..+14:00\n");
	expect("set tsys.tz +0530", "err tsys.tz: -12:00..+14:00\n");

	expect("set tsys.mirror 3", "ok\n");
	expect("get -c tsys.mirror", "tsys.mirror 3\nok\n");
	CHECK(cfg_cand.tsys.mirror == 2, "a Port is stored as the logical port");
	expect("set tsys.mirror none", "ok\n");
	expect("get -c tsys.mirror", "tsys.mirror none\nok\n");
	expect("set tsys.mirror 10", "err tsys.mirror: a port, or none\n");
	expect("set tsys.mirror 0", "err tsys.mirror: a port, or none\n");

	expect("set tsys.hash dport,spa", "ok\n");
	expect("get -c tsys.hash", "tsys.hash spa,dport\nok\n");
	expect("set tsys.hash none", "ok\n");
	expect("get -c tsys.hash", "tsys.hash none\nok\n");
	expect("set tsys.hash spa,bogus", "err tsys.hash: spa smac dmac sip dip sport dport\n");
	expect("set tsys.hash spa,", "err tsys.hash: spa smac dmac sip dip sport dport\n");
	expect("set tsys.lp 10g,10h", "ok\n");
	expect("get -c tsys.lp", "tsys.lp 10h,10g\nok\n");
	CHECK(cfg_cand.tsys.lp == 0x101, "Flags over 8 words: 16 bits");
	expect("del tsys.led", "ok\n");
	expect("get -c tsys.led", "tsys.led off\nok\n");

	expect("do tsys.rnd", "ok\n");

	printf("sys, state.sys\n");
	expect("do sys.reset", "ok\n");
	CHECK(sys_resets == 1, "do sys.reset: its handler, once");
	expect("do sys.rnd", "ok\n");
	expect("do sys.factory_reset", "err sys.factory_reset: the flash is busy\n");
	CHECK(sys_rnd == 1 && sys_factory == 1, "do sys.rnd, sys.factory_reset: their handlers");
	expect("set sys.reset on", "err sys.reset: an action: do it\n");
	expect("do sys", "err sys: hostname led reset factory_reset rnd\n");
	expect("get state.sys.version state.sys.chip state.sys.ports state.sys.sfp_ports state.sys.ports_10g",
	       "state.sys.version v0.1.0-test\nstate.sys.chip rtl8373\nstate.sys.ports 1-9\n"
	       "state.sys.sfp_ports 9\nstate.sys.ports_10g none\nok\n");
	expect("get state.sys.uptime_s state.sys.temp_c state.sys.flash.size state.sys.poe",
	       "state.sys.uptime_s 70000\nstate.sys.temp_c 41.5\nstate.sys.flash.size 2097152\n"
	       "state.sys.poe none\nok\n");
	expect("get state.sys.reset", "state.sys.reset.cause web\nstate.sys.reset.slot b\n"
	       "state.sys.reset.state none\nstate.sys.reset.step_ms 0\nstate.sys.reset.pc 0\n"
	       "state.sys.reset.bank 0\nstate.sys.reset.states.1 none\nstate.sys.reset.states.2 none\n"
	       "state.sys.reset.states.3 none\nstate.sys.reset.states.4 none\nstate.sys.reset.states.5 none\n"
	       "state.sys.reset.states.6 none\nstate.sys.reset.states.7 A_STEP\nstate.sys.reset.states.8 B_STEP\nok\n");
	expect("get state.sched.slot.a", "state.sched.slot.a.task a\nstate.sched.slot.a.state A_STEP\n"
	       "state.sched.slot.a.mask link,forever\nstate.sched.slot.a.wake 0\nstate.sched.slot.a.arg 0\nok\n");
	expect("get state.sched.slot.job0.state", "state.sched.slot.job0.state none\nok\n");
	expect("get state.sched.state.B_STEP", "state.sched.state.B_STEP.runs 1\nstate.sched.state.B_STEP.max_us 402640\nok\n");
	expect("get state.sched.task.w", "state.sched.task.w.max_us 12\nok\n");
	expect("get state.sched.slot.nope", "err state.sched.slot.nope: a b w job0 job1 job2 job3 job4\n");
	expect("set state.sched.slots 3", "err state.sched.slots: read only\n");
	expect("get state.loader", "state.loader.present on\nok\n");
	expect("get state.health", "state.health.hw_timeouts 2\nstate.health.hw_timeout_reg 0\n"
	       "state.health.stack_headroom 40\nok\n");
	expect("get state.con.tx_free", "state.con.tx_free 2047\nok\n");
	expect("get state.mgmt", "state.mgmt.ip 192.168.2.3\nstate.mgmt.netmask 255.255.255.0\n"
	       "state.mgmt.gateway 192.168.2.1\nstate.mgmt.dhcp.state leasing\nstate.mgmt.dhcp.renew_s 3600\n"
	       "state.mgmt.dhcp.server 0.0.0.0\nok\n");
	expect("get state.ntp", "state.ntp.synced on\nstate.ntp.time 1795160000\nstate.ntp.local_offset_min -300\n"
	       "state.ntp.dst_active off\nstate.ntp.last_sync_s 0\nstate.ntp.stratum 2\nstate.ntp.server_ip 0.0.0.0\nok\n");
	expect("get state.dns.lookup", "state.dns.lookup.status failed\n"
	       "state.dns.lookup.ip 0.0.0.0\nok\n");
	expect("get state.loop", "state.loop.looped 1-2\nstate.loop.blocked 2\nok\n");
	expect("set state.mgmt.ip 10.0.0.1", "err state.mgmt.ip: read only\n");
	{
		const char *o = run("get state.stp"), *w = "state.stp.bridge 8.0.06:eb:00:b5:32:5d\n"
			"state.stp.root 1.0.06:eb:00:b5:32:5d\nstate.stp.is_root off\nstate.stp.root_port lag.2\n"
			"state.stp.root_cost 20000\nstate.stp.tc_count 3\nstate.stp.tc_age_s 90061\nstate.stp.root_maxage_s 20\n"
			"state.stp.root_fwd_s 15\nstate.stp.port.1.state block\n";
		CHECK(!strncmp(o, w, t_strlen(w)) && strstr(o, "state.stp.lag.4.members 1-2\nok\n"),
		      "get state.stp: the bridge, then its ports and LAGs");
	}
	{
		const char *o = run("get state.sfp.1");
		CHECK(strstr(o, "state.sfp.1.present on\nstate.sfp.1.los none\n") && strstr(o, "state.sfp.1.vendor RTLSIM\n") &&
		      strstr(o, "state.sfp.1.rate_mbps 10300\n") && strstr(o, "state.sfp.1.ddm.temp 6272\nstate.sfp.1.ddm.vcc_v 3.3000\n") &&
		      strstr(o, "state.sfp.1.ddm.rx_power_mw 0.5012\n") && strstr(o, "state.sfp.1.ddm.status rx_los,tx_disable\n"),
		      "get state.sfp.1: the module, the raw words, Scaled in their units");
	}
	expect("get state.sfp.2.present", "state.sfp.2.present off\nok\n");
	expect("get state.stp.port.2", "state.stp.port.2.state fwd\nstate.stp.port.2.role root\nstate.stp.port.2.edge off\n"
	       "state.stp.port.2.bpdu_age_ms 0\nstate.stp.port.2.rx_bpdu 70000\nstate.stp.port.2.tx_bpdu 0\n"
	       "state.stp.port.2.rx_tc 0\nstate.stp.port.2.tx_tc 0\nstate.stp.port.2.guard_tripped off\n"
	       "state.stp.port.2.link on\nstate.stp.port.2.legacy off\nstate.stp.port.2.dbridge 1.0.06:eb:00:b5:32:5d\n"
	       "state.stp.port.2.dport 32771\nstate.stp.port.2.dcost 20000\nok\n");
	expect("get state.stp.lag.4.role state.stp.lag.4.members", "state.stp.lag.4.role disabled\nstate.stp.lag.4.members 1-2\nok\n");
	expect("get state.lag.1", "state.lag.1.active 1-2\nstate.lag.1.agg_valid on\nstate.lag.1.aggregator 02:11:22:33:44:55\nok\n");
	expect("get state.lacp.port.1", "state.lacp.port.1.lag 1\n"
	       "state.lacp.port.1.actor activity,aggregation,sync,collecting,distributing\nstate.lacp.port.1.partner none\n"
	       "state.lacp.port.1.rx_state current\nstate.lacp.port.1.rx_pdus 0\nstate.lacp.port.1.partner_sys 00:00:00:00:00:00\nok\n");
	expect("do sched.clear", "ok\n");
	CHECK(sched_clears == 1, "do sched.clear: its handler, once");
	expect("get state.dbg.sds_modes state.dbg.gpio_0 state.dbg.gpio_1 state.dbg.igmp.port.3",
	       "state.dbg.sds_modes 0x00001a2b\nstate.dbg.gpio_0 0x00000000\nstate.dbg.gpio_1 0xffffffff\n"
	       "state.dbg.igmp.port.3.cfg 0x40000002\nok\n");
	expect("set state.dbg.gpio_0 1", "err state.dbg.gpio_0: read only\n");
	expect("do l2.flush", "ok\n");
	CHECK(l2_arg == 0xffff, "do l2.flush: no argument, 0xffff");
	expect("do l2.flush 3", "ok\n");
	CHECK(l2_arg == 3, "do l2.flush 3: om_key 3");
	expect("do l2.delete 32", "ok\n");
	expect("do l2.delete 7", "err l2.delete: no such entry\n");
	CHECK(l2_arg == 7, "do l2.delete 7: om_key 7");
	l2_arg = -1;
	expect("do l2.delete x", "err l2.delete: a number\n");
	expect("do l2.delete 70000", "err l2.delete: a number\n");
	CHECK(l2_arg == -1, "a bad argument: the handler isn't called");
	{
		const char *o = run("get state.sys");
		CHECK(!strncmp(o, "state.sys.version ", 18) && strstr(o, "state.sys.button off\nstate.sys.reset.cause web\n") &&
		      !strcmp(o + t_strlen(o) - 3, "ok\n"), "get state.sys: state.sys.reset as well, after it");
	}
	CHECK(t_rnd == 1, "a singleton's action");
	expect("set tsys.offset 13", "ok\n");
	expect("apply", "err tsys: test check failed\n");
	expect("set tsys.offset 12", "ok\n");
	t_sys = 0;
	expect("apply", "ok\n");
	CHECK(t_sys == 1, "a singleton applied once");
	expect("diff", "ok\n");
	expect("set tsys.password other", "ok\n");
	expect("diff", "- tsys.password *\n+ tsys.password *\nok\n");
	expect("del tsys", "ok\n");
	expect("get -c tsys.ip tsys.password", "tsys.ip 192.168.2.2\ntsys.password *\nok\n");
	CHECK(!t_strcmp(cfg_cand.tsys.password, "1234"), "del of a singleton: every field back to its default");

	printf("state types\n");
	expect("get state.tsys", "state.tsys.bridge 8.0.20:7c:14:a2:25:fa\nstate.tsys.ip 10.0.0.7\n"
	       "state.tsys.offset -5\nstate.tsys.lp 2g5,5g,10g\nok\n");
	expect("set state.tsys.ip 1.2.3.4", "err state.tsys.ip: read only\n");

	printf("keyed\n");
	expect("discard", "ok\n");
	expect("get tvlan", "tvlan.1.name \ntvlan.1.members 1-9\ntvlan.1.tagged none\nok\n");
	expect("get tvlan.2", "err tvlan.2: no such entry\n");
	expect("get tvlan.0", "err tvlan.0: 1-4094\n");
	expect("get tvlan.5000", "err tvlan.5000: 1-4094\n");
	expect("set tvlan.20.members 3-4", "ok\n");
	expect("set tvlan.10.name ten", "ok\n");
	expect("get tvlan.20", "err tvlan.20: no such entry\n");
	expect("get -c tvlan.*.name", "tvlan.1.name \ntvlan.10.name ten\ntvlan.20.name \nok\n");
	expect("get -c tvlan.20", "tvlan.20.name \ntvlan.20.members 3-4\ntvlan.20.tagged none\nok\n");
	expect("diff", "+ tvlan.10\n+ tvlan.10.name ten\n+ tvlan.20\n+ tvlan.20.members 3-4\nok\n");
	t_vlan = 0;
	expect("apply", "ok\n");
	CHECK(t_vlan == 2 && t_vlan_new[1] == 20 && t_vlan_new[2] == 10 && !t_vlan_old[1],
	      "apply: each new entry, in its slot");
	expect("get tvlan.name", "tvlan.1.name \ntvlan.10.name ten\ntvlan.20.name \nok\n");
	expect("del tvlan.10", "ok\n");
	expect("diff", "- tvlan.10\n- tvlan.10.name ten\nok\n");
	expect("set tvlan.30.name x", "ok\n");
	expect("set tvlan.40.name y", "err tvlan.40.name: table full\n");
	expect("set tvlan.10.tagged 3", "ok\n");
	CHECK(cfg_cand.tvlan[2].key == 10 && cfg_cand.tvlan[3].key == 30,
	      "a key comes back to its slot in running; a new one takes a slot free in both");
	expect("diff", "- tvlan.10.name ten\n+ tvlan.10.name \n- tvlan.10.tagged none\n+ tvlan.10.tagged 3\n"
	       "+ tvlan.30\n+ tvlan.30.name x\nok\n");
	expect("set tvlan.*.tagged 4", "ok\n");
	expect("get -c tvlan.tagged", "tvlan.1.tagged 4\ntvlan.10.tagged 4\ntvlan.20.tagged 4\ntvlan.30.tagged 4\nok\n");
	t_vlan = 0;
	expect("apply", "ok\n");
	CHECK(t_vlan == 4, "apply: the four changed entries");
	expect("del tvlan.10", "ok\n");
	expect("del tvlan.20", "ok\n");
	expect("apply", "ok\n");
	CHECK(t_vlan_old[2] == 10 && !t_vlan_new[2], "apply of a deletion: the slot's old key in roll");
	expect("del tvlan.99", "err tvlan.99: no such entry\n");
	expect("del tvlan", "ok\n");
	expect("get -c tvlan", "tvlan.1.name \ntvlan.1.members 1-9\ntvlan.1.tagged none\nok\n");
	CHECK(cfg_cand.tvlan[0].key == 1, "del tvlan: the default entry back, in running's slot for it");
	expect("del tvlan.1", "ok\n");
	expect("get -c tvlan", "ok\n");
	expect("set tvlan.*.name z", "err tvlan.*.name: no such entry\n");
	expect("discard", "ok\n");
	expect("set tvlan.4000.name keep", "ok\n");
	expect("apply", "ok\n");
	expect("del tvlan.4000", "ok\n");
	expect("apply", "err tvlan.4000: test: tvlan.4000 stays\n");
	CHECK(cfg_run.tvlan[1].key == 4000, "a check sees an entry deleted in the candidate, and can refuse it");
	expect("discard", "ok\n");

	printf("numbered\n");
	expect("get tlag.2", "tlag.2.mode off\ntlag.2.members none\nok\n");
	expect("get tlag.5", "err tlag.5: 1-4\n");
	expect("get tlag.0.mode", "err tlag.0.mode: 1-4\n");
	expect("set tlag.*.mode static", "ok\n");
	expect("set tlag.4.members 5-8", "ok\n");
	expect("get -c tlag.mode", "tlag.1.mode static\ntlag.2.mode static\ntlag.3.mode static\ntlag.4.mode static\nok\n");
	memset(t_lag, 0, sizeof t_lag);
	expect("apply", "ok\n");
	CHECK(t_lag[0] == 1 && t_lag[3] == 1 && cfg_run.tlag[3].members == 0xf0,
	      "a numbered object's handler gets number - 1");

	printf("nested\n");
	{
		char all[2048];
		snprintf(all, sizeof all, "%s", run("get tstp"));
		CHECK(!strncmp(all, "tstp.enabled off\ntstp.port.1.cost 0\ntstp.port.1.edge auto\n", 56) &&
		      strstr(all, "tstp.port.9.edge auto\nok\n"), "get tstp: the singleton, then its sub-object's instances");
	}
	expect("get tstp.port.3", "tstp.port.3.cost 0\ntstp.port.3.edge auto\nok\n");
	expect("set tstp.port.3.cost 200000000", "ok\n");
	expect("set tstp.port.3.cost 200000001", "err tstp.port.3.cost: 0-200000000\n");
	expect("set tstp.port.3.edge sometimes", "err tstp.port.3.edge: auto on off\n");
	expect("set tstp.port.10.edge on", "err tstp.port.10.edge: no such port\n");
	expect("get port.3.storm.mcast", "port.3.storm.mcast.rate 0\nport.3.storm.mcast.unit pps\nok\n");
	expect("set port.3.storm.mcast.rate 1000", "ok\n");
	CHECK(cfg_cand.port_storm[2][1].rate == 1000, "port.3.storm.mcast is [logical 2][1]");
	expect("set port.3.storm.mcast.unit kbps", "ok\n");
	expect("get -c port.3.storm.mcast", "port.3.storm.mcast.rate 1000\nport.3.storm.mcast.unit kbps\nok\n");
	expect("get port.3.storm.xcast", "err port.3.storm.xcast: bcast mcast ucast umcast\n");
	expect("get port.3.storm.*.unit", "port.3.storm.bcast.unit pps\nport.3.storm.mcast.unit pps\n"
	       "port.3.storm.ucast.unit pps\nport.3.storm.umcast.unit pps\nok\n");
	expect("get port.*.storm.umcast.rate", "port.1.storm.umcast.rate 0\nport.2.storm.umcast.rate 0\n"
	       "port.3.storm.umcast.rate 0\nport.4.storm.umcast.rate 0\nport.5.storm.umcast.rate 0\n"
	       "port.6.storm.umcast.rate 0\nport.7.storm.umcast.rate 0\nport.8.storm.umcast.rate 0\n"
	       "port.9.storm.umcast.rate 0\nok\n");
	{
		char a[1024];
		snprintf(a, sizeof a, "%s", run("get port.*.storm.umcast.rate"));
		expect("get port.storm.umcast.rate", a);
	}
	{
		const char *o = run("get port.2");
		CHECK(strstr(o, "port.2.bw_out_kbps 0\nport.2.storm.bcast.rate 0\n") &&
		      strstr(o, "port.2.storm.umcast.unit pps\nok\n"), "get port.2: the port, then its sub-objects");
	}
	memset(t_storm, 0, sizeof t_storm);
	memset(t_stpp, 0, sizeof t_stpp);
	expect("apply", "ok\n");
	CHECK(t_storm[2 * 4 + 1] == 1 && t_stpp[2] == 1, "a nested handler gets logical * 4 + type");
	expect("diff", "ok\n");
	expect("del port.3.storm.mcast", "ok\n");
	expect("diff", "- port.3.storm.mcast.rate 1000\n+ port.3.storm.mcast.rate 0\n"
	       "- port.3.storm.mcast.unit kbps\n+ port.3.storm.mcast.unit pps\nok\n");
	expect("discard", "ok\n");

	printf("several paths\n");
	expect("get port.1.mtu tlag.1.mode state.tsys.ip", "port.1.mtu 16383\ntlag.1.mode static\nstate.tsys.ip 10.0.0.7\nok\n");
	expect("get -c port.1.mtu tvlan.1.members", "port.1.mtu 16383\ntvlan.1.members 1-9\nok\n");
	expect("get port.1.mtu nope", "port.1.mtu 16383\nerr nope: sched l2 sys tsys tvlan.* tlag.* tstp tstp.port.* vlan.* mgmt dns ntp syslog web lag.* port.* port.*.storm.* stp stp.lag.* stp.port.* igmp lldp loop mirror\n");
	expect("get port.1.mtu port.9.eee", "port.1.mtu 16383\nerr port.9.eee: not on this port\n");
	expect("get port.1.mtu tlag.9", "port.1.mtu 16383\nerr tlag.9: 1-4\n");
	{
		const char *o = run("get state");
		CHECK(!strncmp(o, "state.port.1.link ", 18) && strstr(o, "state.port.9.lag 0\nstate.cfg.pending ") &&
		      strstr(o, "state.cfg.load.skip_id 0\nstate.cfg.boot_defaults \nstate.dbg.sds_modes 0x00001a2b\n") &&
		      strstr(o, "state.dbg.igmp.port.9.cfg 0x40000008\nstate.health.hw_timeouts 2\n") &&
		      strstr(o, "state.con.tx_free 2047\nstate.l2.16.mac 02:00:00:00:00:00\n") &&
		      strstr(o, "state.l2.2288.static off\nstate.lag.1.active 1-2\n") &&
		      strstr(o, "state.port.9.mib.54 34359738422\nstate.sfp.1.present on\n") &&
		      strstr(o, "state.lacp.port.9.partner_sys 00:00:00:00:00:00\nstate.mgmt.ip 192.168.2.3\n") &&
		      strstr(o, "state.stp.lag.4.members 1-2\nstate.ntp.synced on\n") &&
		      strstr(o, "state.loop.blocked 2\nstate.sys.version ") &&
		      strstr(o, "state.sys.reset.states.8 B_STEP\nstate.loader.present on\nstate.sched.passes ") &&
		      strstr(o, "state.sched.task.j.max_us 13\nstate.tsys.bridge ") &&
		      status_ok(o) && !strcmp(o + t_strlen(o) - 3, "ok\n"), "get state: the whole state tree");
	}
}

/* staging: the apply job, the confirm window, the rollback, state.cfg */
static void staging(void)
{
	static struct cfg before, mid;
	om_init();
	memset(applied, 0, sizeof applied);

	printf("staging\n");
	expect("get state.cfg", "state.cfg.pending 0\nstate.cfg.dirty off\nstate.cfg.apply idle\nstate.cfg.confirm_s 0\nstate.cfg.format ok\nstate.cfg.save idle\nstate.cfg.saved_bytes 0\nstate.cfg.load.skipped 0\nstate.cfg.load.skip_ix 0\nstate.cfg.load.skip_id 0\nstate.cfg.boot_defaults \nok\n");
	CHECK(!t_strcmp(prompt(), ""), "the prompt: nothing to add");
	expect("set port.3.mtu 1500", "ok\n");
	expect("set port.3.pvid 7", "ok\n");
	expect("set tlag.2.mode lacp", "ok\n");
	CHECK(!t_strcmp(prompt(), "*"), "the prompt: * with unapplied changes");
	expect("get state.cfg.pending", "state.cfg.pending 2\nok\n");	/* port.3 and tlag.2 */
	job_steps = 0;
	expect("apply", "ok\n");
	CHECK(applied[2] == 1 && cfg_run.port[2].mtu == 1500 && applied_old[2].mtu == 16383,
	      "apply: the job applies port 3, the old value in the rollback copy");
	/* P3I: the copies a chunk a step (2 before the walk, 1 after), the walk WALK_N
	 * instances a step; a step for each changed instance (port.3, tlag.2) */
	int chunks = (sizeof(struct cfg) + 511) / 512;
	CHECK(job_steps >= 3 * chunks + 2 && job_steps <= 3 * chunks + 2 + 1 + OM_N_OBJ,
	      "apply: the copies, the walk and the settle, each in bounded steps");
	CHECK(!memcmp(&cfg_roll, &cfg_run, sizeof cfg_run), "apply without a window: the rollback copy is running again");
	expect("get state.cfg", "state.cfg.pending 0\nstate.cfg.dirty on\nstate.cfg.apply done\nstate.cfg.confirm_s 0\nstate.cfg.format ok\nstate.cfg.save idle\nstate.cfg.saved_bytes 0\nstate.cfg.load.skipped 0\nstate.cfg.load.skip_ix 0\nstate.cfg.load.skip_id 0\nstate.cfg.boot_defaults \nok\n");
	memset(applied, 0, sizeof applied);
	job_steps = 0;
	expect("apply", "ok\n");
	CHECK(!applied[2] && job_steps >= 3 * chunks, "an empty diff: apply calls no handler");

	/* P3I: the pending count is kept, not compared: back to running's value is 0 again */
	expect("set port.3.mtu 9000", "ok\n");
	expect("set port.4.mtu 9000", "ok\n");
	expect("get state.cfg.pending", "state.cfg.pending 2\nok\n");
	expect("set port.3.mtu 1500", "ok\n");
	expect("get state.cfg.pending", "state.cfg.pending 1\nok\n");
	expect("del port.4", "ok\n");
	CHECK(!t_strcmp(prompt(), ""), "set back to running's values: nothing pending, no *");
	expect("set port.*.mtu 2000", "ok\n");
	expect("get state.cfg.pending", "state.cfg.pending 9\nok\n");
	expect("discard", "ok\n");
	CHECK(!t_strcmp(prompt(), "") && !memcmp(&cfg_cand, &cfg_run, sizeof cfg_run), "discard: the copy, nothing pending");
	expect("set tvlan.30.name x", "ok\n");
	expect("del tvlan.1", "ok\n");
	expect("get state.cfg.pending", "state.cfg.pending 2\nok\n");
	expect("del tvlan", "ok\n");
	expect("get state.cfg.pending", "state.cfg.pending 0\nok\n");

	printf("confirm window\n");
	memcpy(&before, &cfg_run, sizeof before);
	expect("set port.3.mtu 9000", "ok\n");
	expect("set port.5.name lab", "ok\n");
	expect("apply 5", "ok\n");
	memcpy(&mid, &cfg_run, sizeof mid);
	CHECK(cfg_run.port[2].mtu == 9000 && om_apply_st == E_CFGAPPLY_CONFIRM, "apply 5: applied, the window open");
	CHECK(!t_strcmp(prompt(), "[confirm 5] "), "the prompt: [confirm 5]");
	expect("get state.cfg.apply state.cfg.confirm_s", "state.cfg.apply confirm\nstate.cfg.confirm_s 5\nok\n");
	expect("apply", "err apply: confirm first\n");
	expect("set port.6.mtu 100", "ok\n");
	CHECK(!t_strcmp(prompt(), "[confirm 5] *"), "set in a window: the candidate moves on");
	seconds(3);
	CHECK(!t_strcmp(prompt(), "[confirm 2] *"), "the window counts down");
	memset(applied, 0, sizeof applied);
	out_reset();
	seconds(2);
	out_buf[out_len] = 0;
	CHECK(!memcmp(&cfg_run, &before, sizeof before) && !memcmp(&cfg_cand, &before, sizeof before) &&
	      !memcmp(&cfg_roll, &before, sizeof before), "unconfirmed: running, candidate and rollback exactly as before");
	CHECK(applied[2] == 1 && applied[4] == 1 && applied_old[2].mtu == 9000 && applied_new[2].mtu == 1500,
	      "the rollback re-applies what changed, with the window's values as the old ones");
	CHECK(!t_strcmp(out_buf, "event state.cfg.apply rolled_back\n"), "the rollback says so on the console");
	expect("get state.cfg.apply", "state.cfg.apply rolled_back\nok\n");
	CHECK(!t_strcmp(prompt(), ""), "after the rollback: nothing pending");

	expect("set port.3.mtu 9000", "ok\n");
	expect("apply 60", "ok\n");
	job_wakes = 0;
	expect("confirm", "ok\n");
	CHECK(job_wakes == 1, "confirm wakes the job");
	job_run();
	CHECK(cfg_run.port[2].mtu == 9000 && om_apply_st == E_CFGAPPLY_DONE &&
	      !memcmp(&cfg_roll, &cfg_run, sizeof cfg_run), "confirmed: kept");
	expect("confirm", "err confirm: no window\n");
	expect("apply 0", "err apply: 1-3600 seconds\n");
	expect("apply 3601", "err apply: 1-3600 seconds\n");
	expect("apply x", "err apply: 1-3600 seconds\n");

	printf("a handler fails\n");
	memcpy(&before, &cfg_run, sizeof before);
	expect("set port.2.mtu 2000", "ok\n");
	expect("set port.5.mtu 5000", "ok\n");
	expect("set port.7.mtu 7000", "ok\n");
	memcpy(&mid, &cfg_cand, sizeof mid);
	memset(applied, 0, sizeof applied);
	fail_apply_on = 4;
	expect("apply", "err port.5: failed, rolled back\n");
	CHECK(!memcmp(&cfg_run, &before, sizeof before) && !memcmp(&cfg_roll, &before, sizeof before),
	      "a handler failed: running back as it was");
	CHECK(!memcmp(&cfg_cand, &mid, sizeof mid), "the candidate keeps the edits");
	CHECK(applied[1] == 2 && applied[4] == 2 && applied[6] == 1,
	      "the walk stops at the failure; the rollback puts back every changed port");
	expect("get state.cfg.apply", "state.cfg.apply rolled_back\nok\n");
	expect("discard", "ok\n");

	printf("busy\n");
	expect("set port.2.mtu 2000", "ok\n");
	snprintf((char *)cmd_buffer, CMD_BUF_SIZE, "apply");
	tokenize();
	out_reset();
	om_cmd();		/* the checks, a cli step at a time, then the walk starts; nobody steps it yet */
	while (cli_job && !job_on)
		om_line();
	snprintf((char *)cmd_buffer, CMD_BUF_SIZE, "get port.2.mtu");
	tokenize();
	om_cmd();
	out_buf[out_len] = 0;
	CHECK(!t_strcmp(out_buf, "err: applying\n"), "while the walk runs, the verbs wait");
	cli_job = 0;
	job_run();
	expect("get port.2.mtu", "port.2.mtu 2000\nok\n");

	printf("a get streaming at the window's end\n");
	memcpy(&before, &cfg_run, sizeof before);
	expect("set port.3.mtu 3000", "ok\n");
	expect("apply 2", "ok\n");
	snprintf((char *)cmd_buffer, CMD_BUF_SIZE, "get port.*");
	tokenize();
	out_reset();
	om_cmd();
	om_line();
	seconds(1);
	CHECK(om_confirm_s == 1, "a second gone");
	job_due = 1;		/* seconds(1) without its job_run(): the cli is streaming */
	job_step();
	CHECK(om_apply_st == E_CFGAPPLY_CONFIRM && !om_confirm_s && cli_job == CLI_JOB_OM, "the window ends at N s");
	job_step();
	job_step();
	CHECK(om_apply_st == E_CFGAPPLY_CONFIRM, "the rollback waits for a get");
	while (cli_job && om_line())
		;
	cli_job = 0;
	job_run();
	out_buf[out_len] = 0;
	CHECK(!strstr(out_buf, "err") && strstr(out_buf, "port.9.mtu") && om_apply_st == E_CFGAPPLY_ROLLED_BACK &&
	      !memcmp(&cfg_run, &before, sizeof before), "a get that ends: whole, then the rollback");
	expect("set port.3.mtu 3000", "ok\n");
	expect("apply 1", "ok\n");
	snprintf((char *)cmd_buffer, CMD_BUF_SIZE, "get port.*");
	tokenize();
	out_reset();
	om_cmd();
	om_line();
	seconds(1);		/* the get stalls (a slow web client): job_run steps only the job */
	CHECK(om_apply_st == E_CFGAPPLY_ROLLED_BACK && !memcmp(&cfg_run, &before, sizeof before),
	      "a stalled get holds the rollback 5 s at most");
	out_reset();
	CHECK(!om_line(), "the get's next line ends it");
	out_buf[out_len] = 0;
	CHECK(!t_strcmp(out_buf, "err: cut by a rollback\n"), "and says why");
	cli_job = 0;
	expect("get port.3.mtu", "port.3.mtu 9000\nok\n");

	printf("a stepped set (J_SET) at the window's end\n");
	for (int v = 2; v <= 21; v++) {		/* more than SET_N entries: the set takes steps */
		char l[40];
		snprintf(l, sizeof l, "set vlan.%d.members 1", v);
		expect(l, "ok\n");
	}
	expect("apply", "ok\n");
	memcpy(&jset_before, &cfg_run, sizeof jset_before);
	for (int stall = 0; stall < 2; stall++) {
		expect("set port.3.mtu 3000", "ok\n");
		expect("apply 1", "ok\n");
		snprintf((char *)cmd_buffer, CMD_BUF_SIZE, "set vlan.*.tagged 1");
		tokenize();
		out_reset();
		om_cmd();
		CHECK(cli_job == CLI_JOB_OM, "the set is a cli job, a get's kind");
		if (!stall) {
			job_due = 1;		/* the window ends while the set has steps to go */
			job_step();
			job_step();
			CHECK(om_apply_st == E_CFGAPPLY_CONFIRM, "the rollback waits for the set");
			while (cli_job && om_line())
				;
			cli_job = 0;
			out_buf[out_len] = 0;
			CHECK(!t_strcmp(out_buf, "ok\n"), "the set ends: ok");
			job_run();
		} else {
			seconds(1);		/* the set stalls: the rollback waits 5 s, then cuts it */
			out_reset();
			CHECK(!om_line(), "the set's next step ends it");
			out_buf[out_len] = 0;
			CHECK(!t_strcmp(out_buf, "err: cut by a rollback\n"), "and says why");
			cli_job = 0;
		}
		CHECK(om_apply_st == E_CFGAPPLY_ROLLED_BACK && !memcmp(&cfg_run, &jset_before, sizeof jset_before) &&
		      !memcmp(&cfg_cand, &cfg_run, sizeof cfg_run),
		      stall ? "cut: rolled back, the set's part gone with the candidate"
			    : "then the rollback: running as before, the set gone with the candidate");
	}
	expect("del vlan", "ok\n");
	expect("apply", "ok\n");

	printf("boot\n");
	expect("set port.5.mtu 5000", "ok\n");
	memcpy(&mid, &cfg_cand, sizeof mid);
	fail_apply_on = 4;
	memset(applied, 0, sizeof applied);
	out_reset();
	om_boot_apply();
	out_buf[out_len] = 0;
	CHECK(strstr(out_buf, "Boot config not applied\n") && !memcmp(&cfg_run, &before, sizeof before) &&
	      !memcmp(&cfg_cand, &mid, sizeof mid), "a boot handler fails: rolled back, said once");
	CHECK(applied[5] && applied[6] && applied[7] && applied[8] && applied[0],
	      "and the ports after it were applied too (the boot's whole apply)");
	expect("get state.cfg.apply", "state.cfg.apply rolled_back\nok\n");
	fail_check_on = 4;		/* the check refuses it, at its defaults too: nothing applied */
	memset(applied, 0, sizeof applied);
	out_reset();
	om_boot_apply();
	out_buf[out_len] = 0;
	fail_check_on = -1;
	CHECK(strstr(out_buf, "Boot config not applied\n") && !memcmp(&cfg_run, &before, sizeof before) &&
	      cfg_cand.port[4].mtu == 16383 && !applied[4],
	      "a boot check refuses an object at its defaults: none applied, the object reset in the candidate");
	expect("get state.cfg.apply state.cfg.boot_defaults", "state.cfg.apply rolled_back\nstate.cfg.boot_defaults port.#\nok\n");
	expect("discard", "ok\n");
	om_boot_apply();
	expect("get state.cfg.apply state.cfg.boot_defaults", "state.cfg.apply idle\nstate.cfg.boot_defaults \nok\n");
	/* P4-4b: an object a check refuses goes back to its defaults, the rest applies */
	expect("set port.5.mtu 5000", "ok\n");
	expect("set stp.maxage_s 40", "ok\n");	/* with hello 2, fwd 15: 6.20.5 refuses */
	expect("set stp.priority 3", "ok\n");
	out_reset();
	om_boot_apply();
	out_buf[out_len] = 0;
	CHECK(strstr(out_buf, "err stp: needs") && !strstr(out_buf, "Boot config not applied") &&
	      cfg_run.port[4].mtu == 5000 && cfg_run.stp.maxage_s == 20 && cfg_run.stp.priority == 8,
	      "boot: stp refused goes back to its defaults, port applies");
	expect("get state.cfg.apply state.cfg.boot_defaults", "state.cfg.apply idle\nstate.cfg.boot_defaults stp\nok\n");
	expect("diff", "ok\n");
	expect("set lag.1.hash none", "ok\n");	/* two objects: both named, in apply order */
	expect("set stp.hello_s 10", "ok\n");
	expect("set stp.port.2.priority 8", "ok\n");
	om_boot_apply();
	expect("get state.cfg.boot_defaults", "state.cfg.boot_defaults lag.# stp stp.port.#\nok\n");
	CHECK(cfg_run.lag[0].hash == cfg_cand.lag[1].hash && cfg_run.stp.hello_s == 2 && cfg_run.stp_port[1].priority == 128,
	      "each refused object at its defaults");
	om_init();
}

/* ---- the saved config: binary records, save, load, boot ----------------- */

uint16_t crc16_host_buf(const uint8_t *d, uint32_t n);

static void boot(void)		/* as rtlplayground.c: defaults, the saved config, apply */
{
	out_reset();
	om_init();
	int v = om_boot_load(0);
	if (v == E_CFGFORMAT_BLANK || v == E_CFGFORMAT_BAD)
		om_boot_load(1);	/* cmd_parser.c execute_config(): the default */
	om_boot_apply();		/* (old text is replayed there: not here) */
	out_buf[out_len] = 0;
}

static uint8_t sec[CONFIG_LEN];
static char out_dir[400] = ".";	/* the binary's: test_om_codec.py reads what binary() leaves there */

/* a blank flash with the help blob where the image has it (om_gen --help-blob) */
static void help_flash(void)
{
	if (fl.mem)
		fd_free(&fl);
	fd_init(&fl, 0x100000);
#if OM_HELP
	static uint8_t b[8192];
	char path[450];
	snprintf(path, sizeof path, "%s/om_t/help.bin", out_dir);
	FILE *f = fopen(path, "rb");
	CHECK(f != NULL, "the help blob");
	if (f) {
		fd_load(&fl, HELP_START, b, fread(b, 1, sizeof b, f));
		fclose(f);
	}
#endif
}

static void sec_read(void)
{
	fd_read(&fl, CONFIG_START, sec, CONFIG_LEN);
}

/* records r (n bytes) as the saved config, with their header */
static void sec_write(const uint8_t *r, int n)
{
	uint16_t c = crc16_host_buf(r, n);
	uint8_t h[8] = { 0, 'R', 'C', '1', n, n >> 8, c, c >> 8 };
	fd_erase_sector(&fl, CONFIG_START);
	fd_write(&fl, CONFIG_START, h, 8);
	fd_write(&fl, CONFIG_START + 8, r, n);
}

static int cfg_is(const struct cfg *c) { return !memcmp(&cfg_run, c, sizeof(struct cfg)); }
static int cand_moved(void) { return memcmp(&cfg_cand, &cfg_run, sizeof(struct cfg)); }

static const char *big_lines[] = {
	"set port.1.name the first port of this test, 31", "set port.2.name the second port of the test, 31",
	"set port.3.name the third port of this test, 31", "set port.4.name the fourth port of the test, 31",
	"set port.5.name the fifth port of this test, 31", "set port.6.name the sixth port of this test, 31",
	"set port.7.name the seventh port of the test, 31", "set port.8.name the eighth port of the test, 31",
	"set port.9.name the ninth port of this test, 31", "set tsys.hostname twenty-three-characters",
	"set tsys.password twenty-character-pwd", "set tvlan.2.name fifteen-chars-a", "set tvlan.3.name fifteen-chars-b",
	"set tvlan.4.name fifteen-chars-c", "set tvlan.1.name fifteen-chars-d",
	"set tstp.port.cost 199999999", "set port.storm.bcast.rate 1000", "set port.storm.ucast.unit kbps", NULL,
};

static void dump(const char *name, int n)	/* the sector and get -d, for test_om_codec.py */
{
	char fn[512];
	static char d[8192];
	FILE *f;
	sec_read();
	snprintf(fn, sizeof fn, "%s/om_codec_%s.bin", out_dir, name);
	f = fopen(fn, "wb");
	fwrite(sec, 1, 8 + (sec[4] | sec[5] << 8), f);
	fclose(f);
	snprintf(d, sizeof d, "%s", run(n ? "get -s" : "get -d"));
	snprintf(fn, sizeof fn, "%s/om_codec_%s.txt", out_dir, name);
	f = fopen(fn, "w");
	fwrite(d, 1, strlen(d) - 3, f);		/* without "ok\n" */
	fclose(f);
}

/* random configs over every object and type, saved: test_om_codec.py encodes
 * each one's get -d and must make the same bytes */
static void codec_samples(void)
{
	static const char *v[][6] = {
		{ "port.%d.name", "a b", "x", "port %d", "~!@#$%%^&*()", "" },
		{ "port.%d.mtu", "64", "9000", "16383", "1500", "1518" },
		{ "port.%d.pvid", "0", "100", "4094", "1", "2" },
		{ "port.%d.isolate", "none", "1-3,cpu", "all", "9", "2,4,6,8" },
		{ "port.%d.lldp", "block", "permit", "block", "permit", "block" },
		{ "port.%d.eee", "off", "on", "off", "on", "off" },
		{ "port.%d.storm.ucast.rate", "1", "10000000", "0", "77", "500" },
		{ "tstp.port.%d.edge", "on", "off", "auto", "on", "off" },
		{ "tvlan.%d0.members", "1-3", "none", "9", "1-9", "4" },
		{ "tvlan.%d0.name", "v", "fifteen-chars-x", "a b", "", "q" },
		{ "tlag.%d.members", "1,2", "3-4", "none", "8,9", "5" },
		{ "tsys.offset", "-600", "600", "0", "-1", "1" },
		{ "tsys.tz", "+14:00", "-12:00", "+05:45", "-03:30", "+00:00" },
		{ "tsys.mac", "auto", "ff:ff:ff:ff:ff:ff", "00:11:22:33:44:55", "02:00:00:00:00:01", "auto" },
		{ "tsys.mirror", "none", "1", "9", "5", "none" },
		{ "tsys.hash", "none", "spa,dport", "sip", "smac,dmac", "dip,sport,spa" },
		{ "tsys.lp", "none", "10h,10g", "5g", "100f,1000f,2g5", "10f" },
		{ "tsys.password", "x", "twenty-character-pwd", "1234", "p", "a~b" },
		{ "tsys.ip", "0.0.0.0", "255.255.255.255", "192.168.2.2", "10.1.2.3", "1.1.1.1" },
		{ "tsys.mask", "0.0.0.0", "255.255.255.255", "255.255.255.0", "255.128.0.0", "255.255.254.0" },
	};
	char line[160], path[64], name[16];
	srand(7);
	for (int k = 0; k < 20; k++) {
		om_init();
		for (int j = 0; j < 30; j++) {
			int r = rand() % (int)(sizeof v / sizeof v[0]);
			snprintf(path, sizeof path, v[r][0], 1 + rand() % (r == 8 || r == 9 ? 3 : r == 10 ? 4 : 9));
			snprintf(line, sizeof line, "set %s %s", path, v[r][1 + rand() % 5]);
			run(line);
		}
		if (rand() % 3 == 0)
			run("del tvlan.1");
		run("apply");
		CHECK(!t_strcmp(run("save"), "ok\n"), "a random config saves");
		snprintf(name, sizeof name, "r%02d", k);
		dump(name, 0);
	}
}

static void binary(void)
{
	static struct cfg a, b, dflt;
	int n;

	printf("save, load, get -s, get -d\n");
	help_flash();
	fd_fail_after(&fl, -1);
	om_init();
	memcpy(&dflt, &cfg_run, sizeof dflt);
	expect("get -s", "err get: blank\n");
	expect("load saved", "err load: blank\n");
	expect("load", "err load: saved or default\n");
	expect("save", "ok\n");
	sec_read();
	/* all defaults: vlan's and tvlan's reset, and their entry 1 (members 1-9 against an entry's 0) */
	static const uint8_t empty[] = { 0, 'R', 'C', '1', 24, 0 };
	CHECK(!memcmp(sec, empty, 6) && sec[32] == 0xff, "save of the defaults: the header and 24 B of records");
	expect("get -s", "ok\n");		/* the default entries at their own defaults: nothing */
	/* get -s and load leave crc_value as they found it: a web upload's verify carries it
	 * across its steps (P3D-c review). save's encoding is the flash job's, which no
	 * upload shares (flash_job_start() refuses while one is active). */
	{
		extern uint16_t crc_value;
		int kept = 1;
		const char *const l[] = { "get -s", "load saved", "discard" };
		for (int j = 0; j < 3; j++) {
			crc_value = 0xbeef;
			run(l[j]);
			kept &= crc_value == 0xbeef;
		}
		CHECK(kept, "get -s, load: crc_value as it was (an upload's verify)");
	}
	expect("get state.cfg.save state.cfg.saved_bytes", "state.cfg.save done\nstate.cfg.saved_bytes 32\nok\n");

	expect("set port.3.mtu 9000", "ok\n");
	expect("set port.5.name lab rack 2", "ok\n");
	expect("set tsys.password s3cret", "ok\n");
	expect("set tsys.ip 10.0.0.2", "ok\n");
	expect("set tsys.mask 255.255.0.0", "ok\n");
	expect("set tsys.mac 02:00:00:00:00:01", "ok\n");
	expect("set tsys.offset -30", "ok\n");
	expect("set tsys.tz -03:30", "ok\n");
	expect("set tsys.mirror 4", "ok\n");
	expect("set tsys.hash sip,dip", "ok\n");
	expect("set tsys.lp 2g5,10g", "ok\n");
	expect("set tvlan.10.name ten", "ok\n");
	expect("set tvlan.10.members 1-3,9", "ok\n");
	expect("set tlag.2.mode lacp", "ok\n");
	expect("set tstp.port.4.cost 20000", "ok\n");
	expect("set port.3.storm.mcast.rate 500", "ok\n");
	expect("save", "err save: apply or discard first\n");
	expect("apply", "ok\n");
	expect("get state.cfg.dirty", "state.cfg.dirty on\nok\n");
	expect("save", "ok\n");
	expect("get state.cfg.dirty", "state.cfg.dirty off\nok\n");
	memcpy(&a, &cfg_run, sizeof a);
	{
		static char d[4096], sv[4096];
		snprintf(d, sizeof d, "%s", run("get -d"));
		snprintf(sv, sizeof sv, "%s", run("get -s"));
		CHECK(strstr(d, "tsys.password s3cret\n") && !strstr(sv, "password"), "get -d carries the Secret, get -s omits it");
		CHECK(strstr(sv, "port.5.name lab rack 2\n") && strstr(sv, "tvlan.10.members 1-3,9\n") &&
		      strstr(sv, "port.3.storm.mcast.rate 500\n") && strstr(sv, "tsys.tz -03:30\n"), "get -s: the saved fields");
		char *pw = strstr(d, "tsys.password s3cret\n");
		memmove(pw, pw + 21, strlen(pw + 21) + 1);
		dump("save", 0);
		CHECK(!strcmp(d, sv), "get -s is get -d's lines, the Secret left out");
		if (strcmp(d, sv))
			printf("        -d: \"%s\"\n        -s: \"%s\"\n", d, sv);
	}
	expect("get -d port.3", "port.3.mtu 9000\nport.3.storm.mcast.rate 500\nok\n");
	expect("get -d tvlan.10", "tvlan.10.name ten\ntvlan.10.members 1-3,9\nok\n");

	printf("boot loads it\n");
	boot();
	CHECK(cfg_is(&a) && !strstr(out_buf, "not applied"), "boot: running is the saved config");
	expect("get state.cfg.format state.cfg.dirty state.cfg.load.skipped", "state.cfg.format ok\nstate.cfg.dirty off\nstate.cfg.load.skipped 0\nok\n");
	expect("set port.3.mtu 1500", "ok\n");
	expect("load saved", "ok\n");
	expect("diff", "ok\n");
	expect("set port.3.mtu 1500", "ok\n");
	expect("apply", "ok\n");
	expect("load saved", "ok\n");
	expect("get state.cfg.pending", "state.cfg.pending 1\nok\n");	/* port.3: counted again after the load */
	expect("apply", "ok\n");
	expect("get state.cfg.dirty", "state.cfg.dirty off\nok\n");
	expect("load default", "err load: blank\n");
	expect("apply 5", "ok\n");
	expect("save", "err save: confirm first\n");
	expect("load saved", "err load: confirm first\n");
	expect("get -d", "err get: confirm first\n");	/* the rollback copy is in use */
	expect("confirm", "ok\n");
	fl_busy = 1;
	expect("save", "err save: busy\n");
	fl_busy = 0;

	printf("a save of several chunks\n");
	{
		static struct cfg big;
		char c[80];
		for (n = 1; n <= OM_PORTS; n++) {
			snprintf(c, sizeof c, "set port.%d.name port %02d is a name of 31 chars!!", n, n);
			expect(c, "ok\n");
			snprintf(c, sizeof c, "set port.%d.storm.bcast.rate %d", n, 100 + n);
			expect(c, "ok\n");
			snprintf(c, sizeof c, "set port.%d.storm.ucast.rate %d", n, 200 + n);
			expect(c, "ok\n");
			snprintf(c, sizeof c, "set tstp.port.%d.cost %d", n, 1000 + n);
			expect(c, "ok\n");
		}
		expect("apply", "ok\n");
		memcpy(&big, &cfg_run, sizeof big);
		old_ops = 1;
		fl.page_wraps = 0;
		expect("save", "err save: failed\n");
		CHECK(fl.page_wraps > 0, "4 B ops from an unaligned chunk cross a page: the model wraps them and the CRC fails");
		old_ops = 0;
		fl.page_wraps = 0;
		expect("save", "ok\n");
		expect("get state.cfg.save", "state.cfg.save done\nok\n");
		CHECK(!fl.page_wraps, "ops cut at the page: none wraps");
		sec_read();
		CHECK((sec[4] | sec[5] << 8) > 2 * 256, "the save took 3 chunks or more");
		boot();
		CHECK(cfg_is(&big) && !strstr(out_buf, "not applied"), "boot loads the several-chunk save");
		expect("get state.cfg.format state.cfg.load.skipped", "state.cfg.format ok\nstate.cfg.load.skipped 0\nok\n");
	}

	printf("unknown records, values out of range\n");
	{
		static const uint8_t r[] = {
			250, 1, 0, 1,  1, 1, 0,			/* an unknown object */
			1, 3, 0, 3,  5, 2, 0x28, 0x23,		/* port.3.mtu 9000 */
			            99, 1, 7,			/* an unknown field */
			            3, 1, 9,			/* speed: past its words */
			1, 10, 0, 1,  5, 2, 0x28, 0x23,		/* port.10: not on this board */
			1, 4, 0, 1,  5, 2, 10, 0,		/* port.4.mtu 10: under 64 */
			1, 9, 0, 1,  2, 1, 0,			/* port.9.enabled: copper only */
			1, 2, 0, 1,  5, 1, 100,			/* a U16 in one byte */
			200, 0, 0, 2,  1, 3, 'a', '"', 'b',	/* tsys.hostname with a quote */
			               2, 4, 10, 0, 0, 1,	/* tsys.ip 10.0.0.1 */
			201, 0, 0, 0,				/* tvlan: no entries */
			201, 7, 0, 1,  1, 3, 's', 'e', 'v',	/* tvlan.7.name sev */
			204, 4, 0, 1,  1, 4, 0, 0, 0, 0x7f,	/* tstp.port.4.cost: past 200000000 */
			18, 1, 3, 1,  1, 4, 0xf4, 1, 0, 0,	/* port.3.storm.mcast.rate 500 */
			18, 4, 9, 1,  1, 4, 1, 0, 0, 0,		/* port.9.storm.<a 5th type>: index 0x0904 */
			18, 1, 10, 1,  1, 4, 1, 0, 0, 0,	/* port.10.storm.mcast: past the ports */
			201, 8, 0, 0,  201, 9, 0, 0,  201, 10, 0, 0,	/* tvlan.8-10: the 4 slots full */
			201, 11, 0, 1,  1, 1, 'x',		/* tvlan.11: past the slots */
		};
		sec_write(r, sizeof r);
		boot();
		/* the boot's load takes tvlan.1's slot too (running's, the candidate deleted it) */
		expect("get state.cfg.format state.cfg.load.skipped state.cfg.load.skip_ix state.cfg.load.skip_id",
		       "state.cfg.format ok\nstate.cfg.load.skipped 12\nstate.cfg.load.skip_ix 1\nstate.cfg.load.skip_id 250\nok\n");
		expect("get -d", "tsys.ip 10.0.0.1\ndel tvlan.1\ntvlan.7.name sev\ntvlan.8.name \ntvlan.9.name \n"
		       "tvlan.10.name \nport.3.mtu 9000\nport.3.storm.mcast.rate 500\nok\n");
		expect("get -s", "tsys.ip 10.0.0.1\ndel tvlan.1\ntvlan.7.name sev\ntvlan.8.name \ntvlan.9.name \n"
		       "tvlan.10.name \nport.3.mtu 9000\nport.3.storm.mcast.rate 500\nok\n");
		/* index 0x0904 is skipped: before first_inst took step1()'s answer it was
		 * copied to cfg_cand.port_storm[8][4], past struct cfg (ASan: global-buffer-overflow) */
		dump("skip", 1);
	}
	{	/* the first skip named: a record past this build's storm types */
		static const uint8_t r[] = { 1, 3, 0, 1,  5, 2, 0x28, 0x23,  18, 4, 9, 1,  1, 4, 1, 0, 0, 0 };
		sec_write(r, sizeof r);
		boot();
		expect("get state.cfg.load", "state.cfg.load.skipped 1\nstate.cfg.load.skip_ix 2308\nstate.cfg.load.skip_id 18\nok\n");
		expect("get -s", "port.3.mtu 9000\nok\n");
		expect("get state.cfg.load.skip_id", "state.cfg.load.skip_id 18\nok\n");
	}

	printf("a saved config without vlan.1 and every slot taken: all of it at boot\n");
	{
		char c[64];
		boot();
		expect("set port.*.pvid 0", "ok\n");
		expect("del vlan.1", "ok\n");
		expect("apply", "ok\n");	/* one apply holds 64 keys over running and the candidate */
		for (int v = 2; v < 2 + MAX_VLANS; v++) {
			snprintf(c, sizeof c, "set vlan.%d.members 1", v);
			expect(c, "ok\n");
		}
		expect("set port.*.pvid 2", "ok\n");
		vlan_ops[0] = 0;
		expect("apply", "ok\n");
		expect("save", "ok\n");
		vlan_ops[0] = 0;
		boot();
		const char *o = run("get vlan.*.members");
		int n = 0;
		for (const char *q = o; (q = strstr(q, ".members 1\n")); q++)
			n++;
		CHECK(n == MAX_VLANS && !strstr(o, "vlan.1.members") && strstr(o, "vlan.65.members 1\n"),
		      "boot: every VLAN of the save, vlan.1 deleted (its slot taken)");
		expect("get state.cfg.load.skipped state.cfg.pending", "state.cfg.load.skipped 0\nstate.cfg.pending 0\nok\n");
		CHECK(!strstr(out_buf, "not applied") && strstr(vlan_ops, "-1 ") && strstr(vlan_ops, "+65 "),
		      "and applied: VLAN 1 out of the table, 65 in");
		vlan_ops[0] = 0;
		/* the same sector, a handler failing: the boot undoes, running is the defaults (vlan.1 in
		 * the slot the candidate's vlan.65 took) */
		fail_apply_on = 3;
		boot();
		CHECK(strstr(out_buf, "not applied") != NULL, "a handler fails: the boot undoes");
		o = run("diff");
		int ok_ = strstr(o, "- vlan.1\n") && strstr(o, "+ vlan.65\n") && strstr(o, "+ vlan.65.members 1\n") &&
		      strstr(o, "+ vlan.2\n") && status_ok(o);
		CHECK(ok_, "diff: vlan.1 deleted, vlan.65 made, each of its own");
		if (!ok_)
			printf("%.600s\n", o);
		expect("get -c vlan.65.members vlan.1", "vlan.65.members 1\nerr vlan.1: no such entry\n");
		expect("get vlan.1.members vlan.65", "vlan.1.members 1-9\nerr vlan.65: no such entry\n");
		expect("apply", "ok\n");		/* and the apply takes the candidate */
		expect("get vlan.65.members state.cfg.pending", "vlan.65.members 1\nstate.cfg.pending 0\nok\n");
		CHECK(strstr(vlan_ops, "-1 ") && strstr(vlan_ops, "+65 "), "apply: VLAN 1 out, 65 in");
		vlan_ops[0] = 0;
		for (int v = 2; v < 2 + MAX_VLANS; v++) {
			snprintf(c, sizeof c, "del vlan.%d", v);
			expect(c, "ok\n");
		}
		expect("apply", "ok\n");
		expect("del vlan", "ok\n");
		expect("del port", "ok\n");
		expect("apply", "ok\n");
		expect("save", "ok\n");
	}

	printf("a sector boot doesn't take\n");
	{
		static const uint8_t r[] = { 1, 3, 0, 1,  5, 2, 0x28, 0x23 };
		uint8_t before[CONFIG_LEN];
		sec_write(r, sizeof r);
		fd_read(&fl, CONFIG_START + 10, before, 1);
		before[0] ^= 1;			/* a bit flipped */
		fl.mem[CONFIG_START + 10] = before[0];
		sec_read();
		memcpy(before, sec, CONFIG_LEN);
		boot();
		expect("get state.cfg.format", "state.cfg.format bad\nok\n");
		CHECK(cfg_is(&dflt), "a bad CRC loads nothing: the defaults");
		sec_read();
		CHECK(!memcmp(before, sec, CONFIG_LEN), "and the sector is left as it was");
		expect("get -s", "err get: bad\n");		/* its job's CRC check */
		expect("load saved", "err load: bad\n");
		CHECK(cfg_is(&dflt) && !cand_moved(), "and the candidate as it was");

		fd_erase_sector(&fl, CONFIG_START);
		fd_write(&fl, CONFIG_START, (const uint8_t *)"ip 192.168.1.1\nvlan 100\n", 24);
		sec_read();
		memcpy(before, sec, CONFIG_LEN);
		boot();
		expect("get state.cfg.format", "state.cfg.format bad\nok\n");
		sec_read();
		CHECK(cfg_is(&dflt) && !memcmp(before, sec, CONFIG_LEN), "old command text: the defaults, the sector untouched");
		expect("load saved", "err load: bad\n");

		fd_erase_sector(&fl, CONFIG_START);
		boot();
		expect("get state.cfg.format", "state.cfg.format blank\nok\n");
		CHECK(cfg_is(&dflt), "blank: the defaults");

		sec_write(r, sizeof r);		/* a recovery boot: the default sector */
		fd_erase_sector(&fl, DEFAULT_CONFIG_START);
		fd_load(&fl, DEFAULT_CONFIG_START, fl.mem + CONFIG_START, 16);
		fl.mem[DEFAULT_CONFIG_START + 14] = 0xdc;	/* port.3.mtu 1500 */
		fl.mem[DEFAULT_CONFIG_START + 15] = 0x05;
		{
			uint16_t c = crc16_host_buf(fl.mem + DEFAULT_CONFIG_START + 8, 8);
			fl.mem[DEFAULT_CONFIG_START + 6] = c;
			fl.mem[DEFAULT_CONFIG_START + 7] = c >> 8;
		}
		om_init();
		om_boot_load(1);
		om_boot_apply();
		expect("get port.3.mtu", "port.3.mtu 1500\nok\n");
		expect("get state.cfg.format", "state.cfg.format ok\nok\n");	/* what the saved sector holds */
		expect("load default", "ok\n");
		expect("load saved", "ok\n");
		expect("get -c port.3.mtu", "port.3.mtu 9000\nok\n");
		expect("discard", "ok\n");
		fd_erase_sector(&fl, CONFIG_START);	/* blank: boot takes the default */
		boot();
		expect("get port.3.mtu state.cfg.format", "port.3.mtu 1500\nstate.cfg.format blank\nok\n");
		sec_read();
		CHECK(sec[0] == 0xff && sec[CONFIG_LEN - 1] == 0xff, "and leaves the saved sector blank");

		fd_write(&fl, CONFIG_START, (const uint8_t *)"ip 192.168.1.1\n", 15);
		boot();
		expect("save", "ok\n");	/* over old text: no word for it */
		expect("get state.cfg.format", "state.cfg.format ok\nok\n");
		/* a default sector whose length runs into the help blob (its CRC right): bad */
		fd_erase_sector(&fl, DEFAULT_CONFIG_START);
		{
			uint8_t h[8] = { 0, 'R', 'C', '1', (DEFAULT_CONFIG_LEN - 7) & 0xff, (DEFAULT_CONFIG_LEN - 7) >> 8 };
			uint16_t c = crc16_host_buf(fl.mem + DEFAULT_CONFIG_START + 8, DEFAULT_CONFIG_LEN - 7);
			h[6] = c;
			h[7] = c >> 8;
			fd_load(&fl, DEFAULT_CONFIG_START, h, 8);
		}
		expect("load default", "err load: bad\n");
		fd_erase_sector(&fl, DEFAULT_CONFIG_START);
	}

	printf("a power cut at every flash operation of a save\n");
	{
		int ops, bad = 0, seen_a = 0, seen_b = 0, seen_dflt = 0;
		om_init();
		expect("set port.3.mtu 2000", "ok\n");
		expect("apply", "ok\n");
		expect("save", "ok\n");
		memcpy(&a, &cfg_run, sizeof a);
		for (n = 0; big_lines[n]; n++)
			run(big_lines[n]);
		expect("apply", "ok\n");
		memcpy(&b, &cfg_run, sizeof b);
		expect("save", "ok\n");
		sec_read();
		CHECK((sec[4] | sec[5] << 8) > 512, "the big config takes two FL_PROG steps");
		for (ops = 0; ; ops++) {
			/* A saved; B running; cut after ops operations */
			om_init();
			memcpy(&cfg_run, &a, sizeof a);
			memcpy(&cfg_cand, &a, sizeof a);
			fd_fail_after(&fl, -1);
			run("save");
			memcpy(&cfg_run, &b, sizeof b);
			memcpy(&cfg_cand, &b, sizeof b);
			fd_fail_after(&fl, ops);
			if (!setjmp(fl_cut))
				run("save");
			cli_job = 0;
			fl_kind = 0;
			int cut = fl.powercut;
			fd_fail_after(&fl, -1);
			boot();
			if (cfg_is(&a))
				seen_a++;
			else if (cfg_is(&b))
				seen_b++;
			else if (cfg_is(&dflt))
				seen_dflt++;
			else
				bad++;
			if (!cut)
				break;
		}
		CHECK(!bad && seen_b == 1 && seen_a >= 1 && seen_dflt >= 1,
		      "each cut boots on A (untouched), B (written), or the defaults (blank or bad): never a mix");
		printf("        %d operations: A %d, B %d, defaults %d\n", ops, seen_a, seen_b, seen_dflt);
	}
	codec_samples();
	fd_free(&fl);
}


/* random valid values, set as text and read back: text and bytes agree */
static void roundtrip(void)
{
	static const char *speeds[] = {"auto", "10m", "100m", "1g", "2g5", "5g", "10g"};
	char line[128], want[128];
	int bad = 0;
	srand(1);
	for (int n = 0; n < 2000; n++) {
		int port = 1 + rand() % 9;
		int kind = rand() % 5;
		const char *field;
		char val[64];
		switch (kind) {
		case 0: field = "mtu"; snprintf(val, sizeof val, "%d", 64 + rand() % (16383 - 63)); break;
		case 1: field = "pvid"; snprintf(val, sizeof val, "%d", rand() % 4095); break;
		case 2: field = "speed"; snprintf(val, sizeof val, "%s", speeds[rand() % 7]); break;
		case 3: {
			field = "name";
			int len = rand() % 32;
			for (int i = 0; i < len; i++) {
				char c;
				do c = 0x20 + rand() % 95; while (c == '"' || c == '\\');
				val[i] = c;
			}
			val[len] = 0;
			/* a value is the rest of the line, trimmed at the end by nobody: avoid edge spaces */
			while (len && val[len - 1] == ' ') val[--len] = 0;
			if (!len || val[0] == ' ') snprintf(val, sizeof val, "x%d", n);
			break;
		}
		default: {
			field = "isolate";
			int m = rand() & 0x3ff;
			/* canonical text, as the engine prints it */
			char *p = val; *p = 0;
			int any = 0;
			for (int i = 1; i <= 9; i++) {
				if (!(m & (1 << (i - 1)))) continue;
				int j = i;
				while (j < 9 && (m & (1 << j))) j++;
				p += sprintf(p, "%s%d", any ? "," : "", i);
				if (j != i) p += sprintf(p, "-%d", j);
				any = 1;
				i = j;
			}
			if (m & 0x200) p += sprintf(p, "%scpu", any ? "," : "");
			if (!*val) strcpy(val, "none");
			break;
		}
		}
		if (kind == 2 && (val[0] == '5' || val[0] == '1') && port == 9)
			continue;	/* speed is copper only: port 9 refuses */
		snprintf(line, sizeof line, "set port.%d.%s %s", port, field, val);
		if (port == 9 && kind == 2)
			continue;
		const char *o = run(line);
		if (t_strcmp(o, "ok\n")) {
			if (!bad++) printf("  %s -> %s", line, o);
			continue;
		}
		snprintf(line, sizeof line, "get -c port.%d.%s", port, field);
		snprintf(want, sizeof want, "port.%d.%s %s\nok\n", port, field, val);
		o = run(line);
		if (t_strcmp(o, want)) {
			if (!bad++) printf("  %s -> %s (want %s)", line, o, want);
		}
	}
	CHECK(bad == 0, "round trip: 2000 random sets read back as the same text");
	/* the bytes: apply, then every field of running equals the candidate */
	run("apply");
	CHECK(!memcmp(&cfg_run, &cfg_cand, sizeof cfg_run), "round trip: apply makes running equal the candidate");
}

/* canonical text for a random value of each new type */
static void rnd_mask(char *v, int bits, int n)	/* front-panel ports 1..n as the engine prints them */
{
	char *p = v;
	*p = 0;
	for (int i = 1; i <= n; i++) {
		if (!(bits & (1 << (i - 1))))
			continue;
		int j = i;
		while (j < n && (bits & (1 << j)))
			j++;
		p += sprintf(p, "%s%d", p == v ? "" : ",", i);
		if (j != i)
			p += sprintf(p, "-%d", j);
		i = j;
	}
	if (!*v)
		strcpy(v, "none");
}

static void rnd_flags(char *v, const char *const *w, int n)
{
	char *p = v;
	*p = 0;
	for (int i = 0; i < n; i++)
		if (rand() % 3 == 0)
			p += sprintf(p, "%s%s", p == v ? "" : ",", w[i]);
	if (!*v)
		strcpy(v, "none");
}

/* the test schema's objects: random sets read back as the same text; then
 * the whole tree, printed and replayed as set lines, prints the same again */
static void roundtrip_kinds(void)
{
	static const char *hash[] = {"spa", "smac", "dmac", "sip", "dip", "sport", "dport"};
	static const char *lp[] = {"10h", "10f", "100h", "100f", "1000h", "1000f", "2g5", "5g", "10g"};
	static const char *storm[] = {"bcast", "mcast", "ucast", "umcast"};
	static const int keys[] = {1, 7, 300, 4094};
	static char t1[65536], t2[65536];
	char line[160], want[160], path[64], val[64];
	int bad = 0;
	om_init();
	srand(3);
	for (int n = 0; n < 4000; n++) {
		int kind = rand() % 17;
		switch (kind) {
		case 0:
			sprintf(path, "tsys.ip");
			sprintf(val, "%d.%d.%d.%d", rand() & 255, rand() & 255, rand() & 255, rand() & 255);
			break;
		case 1: {
			int b = rand() % 33;
			uint32_t m = b ? 0xffffffffu << (32 - b) : 0;
			sprintf(path, "tsys.mask");
			sprintf(val, "%u.%u.%u.%u", m >> 24, (m >> 16) & 255, (m >> 8) & 255, m & 255);
			break;
		}
		case 2:
			sprintf(path, "tsys.mac");
			if (rand() % 4 == 0)
				sprintf(val, "auto");
			else
				sprintf(val, "%02x:%02x:%02x:%02x:%02x:%02x", rand() & 255, rand() & 255,
					rand() & 255, rand() & 255, rand() & 255, (rand() & 255) | 1);
			break;
		case 3:
			sprintf(path, "tsys.offset");
			sprintf(val, "%d", rand() % 1201 - 600);
			break;
		case 4: {
			static const int mm[] = {0, 30, 45};
			int m = (rand() % 13) * 60 + mm[rand() % 3];
			if (m > 12 * 60)
				m = 12 * 60;
			int neg = rand() % 2 && m;
			sprintf(path, "tsys.tz");
			sprintf(val, "%c%02d:%02d", neg ? '-' : '+', m / 60, m % 60);
			break;
		}
		case 5:
			sprintf(path, "tsys.mirror");
			if (rand() % 5 == 0)
				sprintf(val, "none");
			else
				sprintf(val, "%d", 1 + rand() % 9);
			break;
		case 6:
			sprintf(path, "tsys.hash");
			rnd_flags(val, hash, 7);
			break;
		case 7:
			sprintf(path, "tsys.lp");
			rnd_flags(val, lp, 9);
			break;
		case 8:
			sprintf(path, "tsys.hostname");
			sprintf(val, "sw%d", rand() % 1000);
			break;
		case 9:
		case 10: {
			int k = keys[rand() % 4];
			if (rand() % 6 == 0) {	/* delete it, if it is there */
				snprintf(line, sizeof line, "del tvlan.%d", k);
				run(line);
				continue;
			}
			sprintf(path, "tvlan.%d.%s", k, kind == 9 ? "members" : "name");
			if (kind == 9)
				rnd_mask(val, rand() & 0x1ff, 9);
			else
				sprintf(val, "v%d", rand() % 100);
			break;
		}
		case 11:
			sprintf(path, "tlag.%d.mode", 1 + rand() % 4);
			sprintf(val, "%s", (const char *[]){"off", "static", "lacp"}[rand() % 3]);
			break;
		case 12:
			sprintf(path, "tlag.%d.members", 1 + rand() % 4);
			rnd_mask(val, rand() & 0x1ff, 9);
			break;
		case 13:
			sprintf(path, "tstp.port.%d.cost", 1 + rand() % 9);
			sprintf(val, "%d", rand() % 200000001);
			break;
		case 14:
			sprintf(path, "tstp.port.%d.edge", 1 + rand() % 9);
			sprintf(val, "%s", (const char *[]){"auto", "on", "off"}[rand() % 3]);
			break;
		case 15:
			sprintf(path, "port.%d.storm.%s.rate", 1 + rand() % 9, storm[rand() % 4]);
			sprintf(val, "%d", rand() % 1048576);
			break;
		default:
			if (rand() % 3 == 0) {	/* apply now and then: running moves too */
				run("apply");
				continue;
			}
			sprintf(path, "port.%d.storm.%s.unit", 1 + rand() % 9, storm[rand() % 4]);
			sprintf(val, "%s", rand() % 2 ? "kbps" : "pps");
			break;
		}
		snprintf(line, sizeof line, "set %s %s", path, val);
		const char *o = run(line);
		if (t_strcmp(o, "ok\n")) {
			if (!bad++)
				printf("  %s -> %s", line, o);
			continue;
		}
		snprintf(line, sizeof line, "get -c %s", path);
		snprintf(want, sizeof want, "%s %s\nok\n", path, val);
		o = run(line);
		if (t_strcmp(o, want) && !bad++)
			printf("  %s -> %s (want %s)", line, o, want);
	}
	CHECK(bad == 0, "round trip: 4000 random sets of the new types and kinds read back as the same text");

	/* the whole config tree as text, replayed into a default config: Secret
	 * prints as *, and an empty derived field as what it derives to, so those
	 * two aren't replayed (the random sets leave the password alone) */
	static struct cfg orig;
	memcpy(&orig, &cfg_cand, sizeof orig);
	snprintf(t1, sizeof t1, "%s", run("get -c port tsys tvlan tlag tstp"));
	om_init();
	run("del tvlan.1");
	bad = 0;
	for (char *l = t1, *e; (e = strchr(l, '\n')); l = e + 1) {
		*e = 0;
		if (strcmp(l, "ok") && !strstr(l, ".password ") &&
		    !(orig.tsys.hostname[0] == 0 && !strncmp(l, "tsys.hostname ", 14))) {
			snprintf(line, sizeof line, "set %.150s", l);
			const char *o = run(line);
			if (t_strcmp(o, "ok\n") && !bad++)
				printf("  %s -> %s", line, o);
		}
		*e = '\n';
	}
	snprintf(t2, sizeof t2, "%s", run("get -c port tsys tvlan tlag tstp"));
	CHECK(bad == 0 && !strcmp(t1, t2) && t_strlen(t1) > 3000,
	      "round trip: the whole tree printed, replayed as set lines, prints the same");
	{
		/* and is the same bytes: keyed entries may sit in other slots */
		struct cfg a = cfg_cand, b = orig;
		int same = 1;
		for (int x = 0; x < 4; x++) {
			if (!b.tvlan[x].key)
				continue;
			int y = 0;
			while (y < 4 && a.tvlan[y].key != b.tvlan[x].key)
				y++;
			same &= y < 4 && !memcmp(&a.tvlan[y], &b.tvlan[x], sizeof a.tvlan[0]);
		}
		for (int x = 0; x < 4; x++)
			same &= !a.tvlan[x].key || b.tvlan[0].key == a.tvlan[x].key || b.tvlan[1].key == a.tvlan[x].key ||
				b.tvlan[2].key == a.tvlan[x].key || b.tvlan[3].key == a.tvlan[x].key;
		memset(a.tvlan, 0, sizeof a.tvlan);
		memset(b.tvlan, 0, sizeof b.tvlan);
		CHECK(same && !memcmp(&a, &b, sizeof a), "round trip: the replayed config is the original's bytes");
	}
	run("apply");
	CHECK(!memcmp(&cfg_run, &cfg_cand, sizeof cfg_run), "round trip: apply makes running equal the candidate");
}

/* random lines: never a crash (ASan), always one status line */
static void fuzz(void)
{
	static const char *words[] = {"get", "set", "del", "diff", "apply", "discard", "do", "-c",
		"port", "port.", "port.1", "port.*", "port.9.", "state.port.3", "state.", ".", "..",
		"port.3.mtu", "port.3.name", "port.1.isolate", "port.2.cabletest", "port.4.speed",
		"1", "0", "-", ",", "1-", "1-9", "cpu", "all", "none", "on", "off", "auto", "10m",
		"4294967296", "18446744073709551616", "\"", "\\", "*", "port.1.cabletest.a",
		"tsys", "tsys.ip", "tsys.mask", "tsys.mac", "tsys.tz", "tsys.offset", "tsys.mirror", "tsys.hash",
		"tsys.lp", "tsys.password", "tsys.hostname", "tsys.rnd", "tvlan", "tvlan.", "tvlan.1", "tvlan.4094",
		"tvlan.*", "tvlan.7.name", "tvlan.9.members", "tlag", "tlag.4", "tlag.5.mode", "tstp", "tstp.port",
		"tstp.port.3", "port.3.storm", "port.3.storm.mcast", "port.*.storm.*.rate", "port.storm.x",
		"state.tsys", "state", "1.2.3.4", "255.255.0.0", "255.0.255.0", "aa:bb:cc:dd:ee:ff", "aa-bb",
		"+05:30", "-12:00", "+14:45", "-600", "+600", "--", "spa,dport", "10h,10g", ",", ":", "kbps"};
	int bad = 0;
	srand(2);
	for (int n = 0; n < 50000; n++) {
		char line[CMD_BUF_SIZE];
		int len = 0, w = 1 + rand() % 5;
		for (int i = 0; i < w && len < CMD_BUF_SIZE - 40; i++) {
			if (rand() % 4 == 0) {	/* raw bytes */
				int k = rand() % 8;
				for (int j = 0; j < k; j++)
					line[len++] = 1 + rand() % 255;
			} else {
				len += snprintf(line + len, CMD_BUF_SIZE - len, "%s", words[rand() % (sizeof words / sizeof *words)]);
			}
			if (rand() % 3)
				line[len++] = rand() % 4 ? ' ' : '.';
		}
		line[len] = 0;
		const char *o = run(line);
		/* a line that is not one of the verbs prints nothing (cmd_parser says unknown) */
		if (*o && !status_ok(o)) {
			if (!bad++) printf("  no status line for \"%s\": \"%s\"\n", line, o);
		}
	}
	CHECK(bad == 0, "fuzz: 50000 random lines, no crash, each ends in one status line");
}

/* P4-1's objects: their defaults, their text, one apply each */
static void services(void)
{
	printf("services\n");
	om_init();
	expect("get sys", "sys.hostname RTLPlayground-0a0b0c\nsys.led on\nok\n");
	expect("set sys.led ports_off", "ok\n");
	expect("set sys.hostname lab 1", "ok\n");
	expect("apply", "err sys: sys.hostname: no space\n");
	expect("discard", "ok\n");
	expect("set sys.led ports_off", "ok\n");
	expect("set sys.hostname lab1", "ok\n");
	t_sysa = 0;
	expect("apply", "ok\n");
	expect("get sys.hostname sys.led", "sys.hostname lab1\nsys.led ports_off\nok\n");
	CHECK(t_sysa == 1, "sys: one apply");
	expect("set sys.hostname", "ok\n");
	expect("apply", "ok\n");
	expect("get sys.hostname", "sys.hostname RTLPlayground-0a0b0c\nok\n");
	expect("get mgmt", "mgmt.mode static\nmgmt.ip 192.168.2.2\nmgmt.netmask 255.255.255.0\nmgmt.gateway 192.168.2.22\nmgmt.vlan 1\nok\n");
	expect("do mgmt.dhcp_renew", "err mgmt.dhcp_renew: mgmt.mode is static\n");
	expect("set mgmt.ip 10.0.0.5", "ok\n");
	expect("set mgmt.netmask 255.0.255.0", "err mgmt.netmask: a.b.c.d, contiguous\n");
	expect("set mgmt.mode dhcp", "ok\n");
	t_mgmt = 0;
	expect("apply", "ok\n");
	CHECK(t_mgmt == 1 && cfg_run.mgmt.mode == E_MGMTMODE_DHCP && cfg_run.mgmt.ip[3] == 5, "mgmt: one apply, dhcp, the static address kept");
	expect("do mgmt.dhcp_renew", "ok\n");
	expect("get dns", "dns.server1 0.0.0.0\ndns.server2 0.0.0.0\nok\n");
	expect("set dns.server2 9.9.9.9", "ok\n");
	t_dns = 0;
	expect("apply", "ok\n");
	CHECK(t_dns == 1 && cfg_run.dns.server2[0] == 9 && !cfg_run.dns.server1[0], "dns: one apply, server2 alone");
	expect("get ntp", "ntp.enabled off\nntp.server pool.ntp.org\nntp.interval_min 60\nntp.timezone +00:00\nntp.dst off\nok\n");
	expect("set ntp.interval_min 1441", "err ntp.interval_min: 1-1440\n");
	expect("set ntp.timezone +05:30", "ok\n");
	expect("set ntp.dst eu", "ok\n");
	t_ntp = 0;
	expect("apply", "ok\n");
	expect("do ntp.sync", "err ntp.sync: ntp.enabled is off\n");
	CHECK(t_ntp == 1 && cfg_run.ntp.timezone == 330 && cfg_run.ntp.dst == E_DST_EU, "ntp: one apply, +05:30 is 330 minutes");
	expect("get web", "web.password *\nweb.session_timeout_s 3600\nok\n");
	expect("set web.password two words", "err web.password: printable text without \" \\ or a space, at most its length\n");
	expect("set web.password s3cret", "ok\n");
	expect("set web.session_timeout_s 59", "err web.session_timeout_s: 60-65535\n");
	t_web = 0;
	expect("apply", "ok\n");
	CHECK(t_web == 1 && !strcmp(cfg_run.web.password, "s3cret"), "web: one apply, the new password");
	expect("get web.password", "web.password *\nok\n");
	expect("get syslog", "syslog.enabled off\nsyslog.server 0.0.0.0\nsyslog.port 514\nok\n");
	expect("set syslog.port 0", "err syslog.port: 1-65535\n");
	expect("set syslog.server 10.1.2.3", "ok\n");
	expect("set syslog.port 5514", "ok\n");
	expect("set syslog.enabled on", "ok\n");
	t_syslog = 0;
	expect("apply", "ok\n");
	CHECK(t_syslog == 1 && syslog_new.enabled && syslog_new.port == 5514 && syslog_new.server[0] == 10 &&
	      syslog_new.server[3] == 3, "syslog: one apply, with the new fields");
	expect("set syslog.enabled off", "ok\n");
	expect("apply", "ok\n");
	CHECK(t_syslog == 2 && !syslog_new.enabled, "syslog off: applied again");
	om_init();
}

/* ---- Tab completion (om_tab()), through the editor ------------------------ */

/* keys typed into the editor, Tabs among them: the line they leave, and
 * (out) what the output ends with (support.c's prompt is "> ") */
static void tab(const char *keys, const char *line, const char *out)
{
	char msg[200];
	size_t n, m;
	env_reset();
	feed(keys);
	while (con_tab == OM_TAB_MORE)		/* the con task's next passes */
		cmd_edit();
	con_line[cmd_line_len] = 0;
	snprintf(msg, sizeof msg, "tab \"%s\": \"%s\"", keys, line);
	CHECK(!t_strcmp((char *)con_line, line), msg);
	if (t_strcmp((char *)con_line, line))
		printf("        got:  \"%s\"\n", con_line);
	if (!out)
		return;
	n = t_strlen(out_buf);
	m = t_strlen(out);
	snprintf(msg, sizeof msg, "tab \"%s\": prints \"%s\"", keys, out);
	CHECK(n >= m && !t_strcmp(out_buf + n - m, out), msg);
	if (n < m || t_strcmp(out_buf + n - m, out))
		printf("        got:  \"%s\"\n", out_buf);
}

static void completion(void)
{
	printf("tab completion\n");
	om_init();
	tab("g\t", "get ", "get ");
	tab("s\t", "s", "s\a");
	tab("s\t\t", "s", "\a\n set  save > s");
	tab("get tsy\t", "get tsys.", "get tsys.");
	tab("get tsys.h\t", "get tsys.h", "\a");		/* hash, hostname */
	tab("get tsys.ho\t", "get tsys.hostname ", "get tsys.hostname ");
	tab("get ts\t", "get ts", "\a");
	tab("get ts\t\t", "get ts", "\n tsys. tstp. tstp.> get ts");	/* tstp, tstp.port: no dedup */
	tab("get state.tsy\t", "get state.tsys.", NULL);
	tab("get tlag.\t\t", "get tlag.", "\n 1. 2. 3. 4.> get tlag.");
	tab("get tlag.3.\t", "get tlag.3.m", NULL);	/* the common part */
	tab("get tlag.3.\t\t\t", "get tlag.3.m", "\n mode  members > get tlag.3.m");
	tab("get tstp.port.\t\t", "get tstp.port.", "\n 1. 2. 3. 4. 5. 6. 7. 8. 9.> get tstp.port.");
	tab("get port.2.storm.u\t\t", "get port.2.storm.u", "\n ucast. umcast.> get port.2.storm.u");
	tab("get port.2.storm.b\t", "get port.2.storm.bcast.", NULL);
	tab("get port.2.storm.bcast.\t\t", "get port.2.storm.bcast.", "\n rate  unit > get port.2.storm.bcast.");
	tab("set tsys.led p\t", "set tsys.led p", "\a");	/* a value: not completed (P3D's cut) */
	tab("set tsys.led \t\t", "set tsys.led ", "\a\n> set tsys.led ");
	tab("load d\t", "load default ", NULL);
	tab("do tsys.\t", "do tsys.rnd ", NULL);
	tab("get tsys.r\t", "get tsys.r", "\a");		/* an action: only after do */
	tab("apply \t", "apply ", "\a");
	tab("get tsys.hostname\033[D\t", "get tsys.hostname", "\a");	/* not at the end */
	tab("get tvlan.\t", "get tvlan.1.", NULL);	/* the keys there are: one */
	expect("set tvlan.300.members 1", "ok\n");
	tab("get tvlan.\t\t", "get tvlan.", "\n 1. 300.> get tvlan.");
	tab("get tvlan.3\t", "get tvlan.300.", NULL);
	expect("discard", "ok\n");
	cli_job = CLI_JOB_OM;			/* a get streams: the engine is in use */
	tab("get tsy\t", "get tsy", "\a");
	cli_job = 0;
	tab("get tsy\t", "get tsys.", NULL);
	expect("get tsys.led", "tsys.led off\nok\n");	/* the commands after it */
}

/* igmp, lldp, loop, mirror (P4-5): defaults, one apply each */
static void l2svc(void)
{
	om_init();
	expect("get igmp", "igmp.enabled off\nok\n");
	expect("set igmp.enabled on", "ok\n");
	t_igmp = 0;
	expect("apply", "ok\n");
	CHECK(t_igmp == 1 && cfg_run.igmp.enabled, "igmp: one apply, on");
	expect("get lldp", "lldp.enabled off\nok\n");
	expect("set lldp.enabled on", "ok\n");
	t_lldp = 0;
	expect("apply", "ok\n");
	CHECK(t_lldp == 1 && t_igmp == 1 && cfg_run.lldp.enabled, "lldp: one apply, on; igmp not again");
	expect("get loop", "loop.enabled off\nok\n");
	expect("set loop.enabled on", "ok\n");
	t_loop = 0;
	expect("apply", "ok\n");
	CHECK(t_loop == 1 && cfg_run.loop.enabled, "loop: one apply, on");
	expect("get mirror", "mirror.enabled off\nmirror.port none\nmirror.rx none\nmirror.tx none\nok\n");
	expect("set mirror.enabled on", "ok\n");
	expect("apply", "err mirror: mirror.enabled needs a port\n");
	expect("set mirror.port 1", "ok\n");
	expect("set mirror.rx 1-3", "ok\n");
	expect("apply", "err mirror: the mirror port can't be a source\n");
	expect("set mirror.rx 2,3", "ok\n");
	expect("set mirror.tx 2,4", "ok\n");
	t_mirror = 0;
	expect("apply", "ok\n");
	CHECK(t_mirror == 1 && cfg_run.mirror.port == 0 && cfg_run.mirror.rx == 0x6 && cfg_run.mirror.tx == 0xa,
	      "mirror: one apply, port 1 (logical 0), rx 2-3, tx 2,4");
	expect("get mirror", "mirror.enabled on\nmirror.port 1\nmirror.rx 2-3\nmirror.tx 2,4\nok\n");
	expect("set mirror.enabled off", "ok\n");
	expect("set mirror.port none", "ok\n");
	expect("apply", "ok\n");
	CHECK(t_mirror == 2, "mirror off without a port: no check fails");
	om_init();
}

/* vlan.V (P4-2): om_vlan.c's table calls and checks, pvid and mgmt.vlan */
static void vlans(void)
{
	char many[64];
	int i;

	printf("vlan\n");
	om_init();
	vlan_checks = 1;
	vlan_ops[0] = 0;
	expect("get vlan", "vlan.1.name \nvlan.1.members 1-9\nvlan.1.tagged none\nok\n");
	expect("set vlan.10.members 1-3", "ok\n");
	expect("set vlan.10.tagged 3", "ok\n");
	expect("set vlan.10.name office", "ok\n");
	expect("apply", "ok\n");
	CHECK(!strcmp(vlan_ops, "+10 007/004 "), "a new VLAN: created, its members and tagged ports");
	expect("set vlan.10.tagged 4", "ok\n");
	expect("apply", "err vlan.10: tagged ports must be members\n");
	expect("discard", "ok\n");
	expect("set vlan.10.name 9x", "ok\n");
	expect("apply", "err vlan.10: a name starts with a letter and has no space\n");
	expect("set vlan.10.name a b", "ok\n");
	expect("apply", "err vlan.10: a name starts with a letter and has no space\n");
	expect("discard", "ok\n");
	expect("set port.1.pvid 20", "ok\n");
	expect("apply", "err port.1: pvid: no such vlan\n");
	expect("set vlan.20.members 1", "ok\n");
	vlan_ops[0] = 0;
	expect("apply", "ok\n");
	CHECK(!strcmp(vlan_ops, "+20 001/000 "), "pvid 20 with VLAN 20 in the same apply");
	expect("set mgmt.vlan 30", "ok\n");
	expect("apply", "err mgmt: vlan: no such vlan\n");
	expect("set mgmt.vlan 0", "ok\n");
	expect("apply", "ok\n");
	expect("del vlan.20", "ok\n");
	expect("apply", "err port.1: pvid: no such vlan\n");
	expect("set port.1.pvid 1", "ok\n");
	vlan_ops[0] = 0;
	expect("apply", "ok\n");
	CHECK(!strcmp(vlan_ops, "-20 "), "a deleted VLAN leaves the table");
	/* VLAN 10 out of its slot and back in another: never out of the table */
	expect("set vlan.30.members 2", "ok\n");
	expect("apply", "ok\n");
	expect("del vlan.10", "ok\n");
	expect("set vlan.40.name x", "ok\n");
	expect("set vlan.10.members 5", "ok\n");
	vlan_ops[0] = 0;
	expect("apply", "ok\n");
	CHECK(!strstr(vlan_ops, "-10"), "a VLAN in another slot isn't deleted");
	CHECK(strstr(vlan_ops, "+10 010/000 ") != NULL, "it is written with its new members");
	/* MAX_VLANS: the 65th refused at set */
	for (i = 0; i < MAX_VLANS; i++) {
		snprintf(many, sizeof many, "set vlan.%d.members 1", 100 + i);
		run(many);
	}
	expect("set vlan.4000.members 1", "err vlan.4000.members: table full\n");
	expect("discard", "ok\n");
	/* P4-4b/c with a keyed object: vlan refused goes back to its defaults (VLAN 20 with
	 * it); the references to it are fixed field by field (mgmt.vlan 0, port.N.pvid 1),
	 * so mgmt, the port names and speeds all stay */
	om_init();
	expect("set vlan.20.members 1-2", "ok\n");
	expect("set vlan.20.tagged 3", "ok\n");	/* vlan's own check refuses: tagged not a member */
	expect("set mgmt.ip 192.168.2.77", "ok\n");
	expect("set mgmt.vlan 20", "ok\n");
	expect("set port.1.pvid 20", "ok\n");
	expect("set port.2.pvid 20", "ok\n");
	expect("set port.1.name uplink", "ok\n");
	expect("set port.2.speed 100m", "ok\n");
	expect("set sys.hostname keep", "ok\n");
	out_reset();
	om_boot_apply();
	out_buf[out_len] = 0;
	CHECK(strstr(out_buf, "err vlan.20: tagged ports must be members\n") && !strstr(out_buf, "err mgmt")
	      && !strstr(out_buf, "err port") && !strstr(out_buf, "not applied"), "boot: vlan refused, alone");
	expect("get state.cfg.apply state.cfg.boot_defaults", "state.cfg.apply idle\nstate.cfg.boot_defaults vlan.# mgmt.vlan port.#.pvid\nok\n");
	expect("get -d mgmt port sys vlan", "mgmt.ip 192.168.2.77\nmgmt.vlan 0\nport.1.name uplink\nport.2.speed 100m\n"
	       "sys.hostname keep\nok\n");	/* the pvids 1, VLAN 1 back: defaults */
	CHECK(cfg_run.mgmt.vlan == 0 && cfg_run.port[0].pvid == 1 && cfg_run.port[1].pvid == 1, "and running has them");
	/* VLAN 1 deleted with the default pvids and mgmt.vlan 1: mgmt.vlan 0; a pvid 1 still
	 * names nothing, so port names vlan, and vlan's reset brings VLAN 1 back */
	om_init();
	expect("del vlan.1", "ok\n");
	expect("set vlan.5.members 1-2", "ok\n");
	expect("set mgmt.ip 192.168.2.77", "ok\n");
	expect("set port.3.mtu 2000", "ok\n");
	out_reset();
	om_boot_apply();
	out_buf[out_len] = 0;
	CHECK(!strstr(out_buf, "err ") && !strstr(out_buf, "not applied"), "boot: no refusal said");
	expect("get state.cfg.apply state.cfg.boot_defaults mgmt.ip mgmt.vlan port.3.mtu vlan.1.members",
	       "state.cfg.apply idle\nstate.cfg.boot_defaults mgmt.vlan port.#.pvid vlan.#\nmgmt.ip 192.168.2.77\n"
	       "mgmt.vlan 0\nport.3.mtu 2000\nvlan.1.members 1-9\nok\n");
	expect("set mgmt.vlan 30", "ok\n");	/* a live apply still refuses */
	expect("apply", "err mgmt: vlan: no such vlan\n");
	expect("discard", "ok\n");
	expect("set port.1.pvid 30", "ok\n");
	expect("apply", "err port.1: pvid: no such vlan\n");
	expect("discard", "ok\n");
	om_init();
	om_boot_apply();		/* a clean boot: boot_defaults empty again */
	vlan_checks = 0;

	/* the text views of a keyed object: a default entry deleted, an entry at
	 * its defaults, a default entry at its own default */
	om_init();
	expect("get -d vlan", "ok\n");
	expect("get -d vlan.1", "ok\n");	/* an untouched default entry: nothing (review2's SHOULD 1) */
	expect("del vlan.1", "ok\n");
	expect("set vlan.20.name", "ok\n");
	expect("set vlan.30.members 2", "ok\n");
	expect("diff", "- vlan.1\n- vlan.1.members 1-9\n+ vlan.20\n+ vlan.30\n+ vlan.30.members 2\nok\n");
	expect("apply", "ok\n");
	expect("get -d", "del vlan.1\nvlan.20.name \nvlan.30.members 2\nok\n");	/* each replays */
	expect("del vlan", "ok\n");
	expect("diff", "+ vlan.1\n+ vlan.1.members 1-9\n- vlan.20\n- vlan.30\n- vlan.30.members 2\nok\n");
	expect("discard", "ok\n");
}

/* help (DESIGN.md 13): its lines, from the tables and om_gen's blob */
#if OM_HELP
static const char *const h_types[] = { "object", "bool", "u8", "u16", "u32", "u64", "enum", "str",
	"ports", "decimal", "action", "ipv4", "netmask", "mac", "flags", "secret", "i16", "tz", "port",
	"bridge", "hex32", NULL };

/* "<path> <type> ... "<tip>"": one word, a type, a tip in quotes at the end */
static int help_line_ok(const char *l, size_t n)
{
	const char *sp = memchr(l, ' ', n), *q;
	if (!sp || n < 4 || l[n - 1] != '"' || memchr(l, '"', sp - l))
		return 0;
	for (int i = 0; h_types[i]; i++) {
		size_t k = strlen(h_types[i]);
		if (!strncmp(sp + 1, h_types[i], k) && (sp[1 + k] == ' '))
			goto typed;
	}
	return 0;
typed:
	for (q = l + n - 2; q > sp && *q != '"'; q--);	/* the tip's opening quote */
	return q > sp && q[-1] == ' ';
}

static void help(void)
{
	printf("help\n");
	static const char usage[] =
		"get [-c|-s|-d] PATH...\nset PATH VALUE\ndel PATH\ndiff\napply [N]\nconfirm\ndiscard\n"
		"do ACTION\nsave\nload saved|default\nhelp [-a|PATH]\nTab completes\nok\n";
	expect("help", usage);
	expect("h", usage);
	expect("?", usage);
	expect("set port.5.name a?b", "ok\n");		/* '?' past the first word: a character */
	expect("get -c port.5.name", "port.5.name a?b\nok\n");
	expect("discard", "ok\n");
	/* the test schema's: every type, the tables' forms, the chip's default (an 8373) */
	expect("help tsys", "tsys object \"\"\n"
	       "tsys.hostname str 0-23 default \"\" \"empty: RTLPlayground-<mac>\"\n"
	       "tsys.ip ipv4 a.b.c.d default 192.168.2.2 \"\"\n"
	       "tsys.mask netmask a.b.c.d default 255.255.255.0 \"\"\n"
	       "tsys.mac mac aa:bb:cc:dd:ee:ff default auto \"\"\n"
	       "tsys.password secret 0-20 default \"1234\" \"\"\n"
	       "tsys.offset i16 -600..600 default 0 \"\"\n"
	       "tsys.tz tz -12:00..+14:00 default +00:00 \"\"\n"
	       "tsys.mirror port 1-9 default none \"\"\n"
	       "tsys.led enum on|ports_off|off default off \"\"\n"
	       "tsys.hash flags spa|smac|dmac|sip|dip|sport|dport default smac,dmac \"\"\n"
	       "tsys.lp flags 10h|10f|100h|100f|1000h|1000f|2g5|5g|10g default none \"\"\n"
	       "tsys.rnd action - \"\"\n"
	       "ok\n");
	expect("help port.2.storm", "port.*.storm.* object 1-9 bcast|mcast|ucast|umcast \"Storm control\"\n"
	       "port.*.storm.*.rate u32 0-10000000 default 0 \"\"\n"
	       "port.*.storm.*.unit enum pps|kbps default pps \"\"\n"
	       "ok\n");
	expect("help tvlan.1.members", "tvlan.*.members ports 1-9 default none \"\"\nok\n");
	expect("help tvlan", "tvlan.* object 1-4094 \"\"\ntvlan.*.name str 0-15 default \"\" \"\"\n"
	       "tvlan.*.members ports 1-9 default none \"\"\ntvlan.*.tagged ports 1-9 default none \"\"\nok\n");
	/* the real schema's, from its tips: a unit, a CPU port mask, a field under a path */
	expect("help port.3.mtu", "port.*.mtu u16 64-16383 default 16383 unit B \"Largest frame accepted, in bytes\"\nok\n");
	expect("help port.isolate", "port.*.isolate ports 1-9,cpu default 1-9,cpu \"Ports this port may send to\"\nok\n");
	const char *o = run("help state.port.cabletest");
	CHECK(!strncmp(o, "state.port.*.cabletest.state enum idle|running|done|timeout \"\"\n", 63) &&
	      strstr(o, "\nstate.port.*.cabletest.pair.d.length_m decimal - unit m \"") && !strstr(o, "object") &&
	      !strstr(o, ".link ") && status_ok(o), "help state.port.cabletest: those fields, no other");
	expect("help sys.reset", "sys.reset action - \"Reset the switch; the web's reply goes out first\"\nok\n");
	/* errors as get's */
	expect("help port.12", "err port.12: no such port\n");
	expect("help port.foo", "err port.foo: name enabled speed duplex mtu pvid ingress ingress_filter lldp eee eee_speeds isolate bw_in_kbps bw_in_mode bw_out_kbps cabletest\n");
	o = run("help nosuch");
	CHECK(!strncmp(o, "err nosuch: ", 12) && strstr(o, " port.* ") && status_ok(o), "help nosuch: the objects");
	/* -a: the version, then every object and field, each line in the format */
	o = run("help -a");
	int lines = 0, bad = 0, nf = 0, longest = 0;
	for (const char *l = o, *e; *l; l = e + 1) {
		e = strchr(l, '\n');
		if (!e)
			break;
		if (e - l > longest)
			longest = e - l;
		if (lines && strncmp(l, "ok\n", 3) && !help_line_ok(l, e - l)) {
			if (!bad++)
				printf("        bad line: %.*s\n", (int)(e - l), l);
		}
		lines++;
	}
	for (int i = 0; i < OM_N_OBJ; i++)
		nf += om_obj[i].nf;
	printf("        help -a: %d lines, the longest %d B (OM_H_LINE %d)\n", lines, longest, OM_H_LINE);
	CHECK(longest < OM_H_LINE, "help -a: its longest line within om_gen's bound (OM_H_LINE)");
	CHECK(!strncmp(o, "help 1\n", 7) && strstr(o, "\nsys object \"The switch\"\n") && !bad && lines == OM_N_OBJ + nf + 2 &&
	      strstr(o, "\nstate.sys object ") && strstr(o, "\nsys.factory_reset action - ") && status_ok(o),
	      "help -a: help 1, a line per object and field, each <path> <type> ... \"<tip>\"");
	/* a line a step, as a job */
	run("discard");
	out_reset();
	snprintf((char *)cmd_buffer, CMD_BUF_SIZE, "help tsys");
	tokenize();
	om_cmd();
	int steps = 0, one = 1;
	while (cli_job) {
		size_t at = out_len;
		if (!om_line())
			cli_job = 0;
		int n = 0;
		for (size_t i = at; i < out_len; i++)
			n += out_buf[i] == '\n';
		if (n != 1)
			one = 0;
		steps++;
	}
	if (!one || steps != 14)
		printf("        %d steps, %s\n", steps, one ? "a line each" : "not a line each");
	CHECK(one && steps == 14, "help tsys: a job, one line a step (the object, 12 fields, ok)");
	/* the console's ring without room for the longest line: the step sleeps, prints nothing */
	out_reset();
	snprintf((char *)cmd_buffer, CMD_BUF_SIZE, "help tsys");
	tokenize();
	om_cmd();
	t_tx_room = OM_H_LINE - 1;
	cli_job_sleep = 0;
	CHECK(om_line() && cli_job_sleep && !out_len, "help: no room on the console for a line: it sleeps");
	t_tx_room = 2048;
	cli_job_sleep = 0;
	cli_job_one = 0;
	CHECK(om_line() && !cli_job_sleep && !strncmp(out_buf, "tsys object", 11) && cli_job_one,
	      "and then prints it, one line in the step (cli_job_one: up to ~4 ms a line)");
	t_tx_room = 0;				/* a web reply (cmd_capture): the console's room doesn't count */
	cmd_capture = 1;
	CHECK(om_line() && !cli_job_sleep && strstr(out_buf, "\ntsys.hostname "), "a web reply's line: no wait for the console");
	cmd_capture = 0;
	t_tx_room = 2048;
	cmd_finish();
	/* after it: get's '*', set's words and a Secret as before */
	o = run("get");
	CHECK(strstr(o, "\nport.*\n") && strstr(o, "\nport.*.storm.*\n"), "get: the objects with '*' after help");
	expect("set port.1.speed fast", "err port.1.speed: auto 10m 100m 1g 2g5 5g 10g\n");
	expect("get tsys.password", "tsys.password *\nok\n");
	/* Tab after help: paths, actions too; h is help's */
	tab("help po\t", "help port.", NULL);
	tab("help tsys.r\t", "help tsys.rnd ", NULL);
	tab("h\t", "help ", NULL);
	tab("? tsys.h\t", "? tsys.h", NULL);
}
#else
static void help(void)
{
	printf("help (OM_HELP=0)\n");
	expect("help", "err help: not in this build\n");
	expect("? port", "err help: not in this build\n");
	expect("help -a", "err help: not in this build\n");
	tab("h\t", "help ", NULL);
}
#endif

/* lag.L (P4-3): om_lag.c's trunk and LACP calls, and its checks */
static void lags(void)
{
	printf("lag\n");
	om_init();
	expect("get lag.1", "lag.1.mode off\nlag.1.members none\nlag.1.hash smac,dmac,sip,dip,sport,dport\nok\n");
	lag_ops[0] = 0;
	expect("set lag.1.mode static", "ok\n");
	expect("set lag.1.members 2,3", "ok\n");
	expect("apply", "ok\n");
	CHECK(!strcmp(lag_ops, "L0 000 S0 006 "), "static: out of LACP, the members to the trunk");
	lag_ops[0] = 0;
	expect("set lag.1.hash spa", "ok\n");
	expect("apply", "ok\n");
	CHECK(!strcmp(lag_ops, "H0 01 "), "the hash to its register, alone");
	expect("set lag.1.hash none", "ok\n");
	expect("apply", "err lag.1: hash needs a field\n");
	expect("discard", "ok\n");
	expect("set lag.2.mode lacp", "ok\n");
	expect("set lag.2.members 3-4", "ok\n");
	expect("apply", "err lag.1: a port is in one lag at most\n");
	expect("set lag.2.members 4-5", "ok\n");
	lag_ops[0] = 0;
	expect("apply", "ok\n");
	CHECK(!strcmp(lag_ops, "L1 018 "), "lacp: the members to LACP");
	lag_ops[0] = 0;
	expect("set lag.2.mode off", "ok\n");
	expect("set lag.3.members 2", "ok\n");	/* off: kept, unused, no overlap */
	expect("apply", "ok\n");
	CHECK(!strcmp(lag_ops, "L1 000 S1 000 "), "off: out of LACP and the trunk; an off LAG's members unused");
	expect("get lag.2.members", "lag.2.members 4-5\nok\n");
	expect("set lag.1.members 2-4", "ok\n");
	expect("set port.4.pvid 0", "ok\n");
	expect("apply", "err lag.1: a lag's members need one pvid; they differ on 2,4\n");	/* the first, and who differs */
	expect("set port.2.pvid 0", "ok\n");
	expect("apply", "err lag.1: a lag's members need one pvid; they differ on 2-3\n");
	expect("set port.3.pvid 0", "ok\n");
	expect("apply", "ok\n");
	expect("get -d lag", "lag.1.mode static\nlag.1.members 2-4\nlag.1.hash spa\nlag.2.members 4-5\nlag.3.members 2\nok\n");
	/* a static move (P4-3b): lag.1 takes port 3 from lag.2, which applies after it */
	om_init();
	memset(trunk, 0, sizeof(trunk));
	expect("set lag.*.mode static", "ok\n");
	expect("set lag.1.members 2", "ok\n");
	expect("set lag.2.members 3-4", "ok\n");
	expect("apply", "ok\n");
	lag_ops[0] = 0;
	two_trunks = 0;
	expect("set lag.1.members 2-3", "ok\n");
	expect("set lag.2.members 4", "ok\n");
	expect("apply", "ok\n");
	CHECK(!two_trunks && trunk[0] == 0x006 && trunk[1] == 0x008,
	      "a port moving between static LAGs is never in two trunks");
	CHECK(!strcmp(lag_ops, "S1 008 L0 000 S0 006 L1 000 S1 008 "), "out of lag.2's trunk first, then into lag.1's");
	om_init();
}

static void stps(void)
{
	printf("stp\n");
	om_init();
	{
		const char *o = run("get stp"), *w = "stp.enabled off\nstp.version rstp\nstp.priority 8\nstp.hello_s 2\n"
			"stp.maxage_s 20\nstp.fwd_delay_s 15\nstp.txhold 6\nstp.pathcost long\nstp.bpdu flood\n"
			"stp.lag.1.enabled on\n";
		CHECK(!strncmp(o, w, t_strlen(w)) && strstr(o, "stp.port.9.p2p auto\nok\n"),
		      "get stp: the defaults, then the LAGs and the ports");
	}
	expect("get stp.port.3", "stp.port.3.enabled on\nstp.port.3.edge auto\nstp.port.3.cost 0\nstp.port.3.priority 128\n"
	       "stp.port.3.guard none\nstp.port.3.filter off\nstp.port.3.p2p auto\nok\n");
	stp_ops[0] = 0;
	expect("set stp.priority 2", "ok\n");
	expect("set stp.version stp", "ok\n");
	expect("set stp.pathcost short", "ok\n");
	expect("set stp.hello_s 1", "ok\n");
	expect("set stp.txhold 3", "ok\n");
	expect("apply", "ok\n");
	CHECK(!strcmp(stp_ops, "prio ") && stp_prio == 0x20 && !stp_rstp && stp_pcost_short && stp_hello_s == 1
	      && stp_txhold == 3 && stp_maxage_s == 20 && stp_fwddelay_s == 15 && !stp_enabled,
	      "stp: the globals, the priority as the BPDU's byte");
	stp_ops[0] = 0;
	expect("set stp.bpdu filter", "ok\n");
	expect("apply", "ok\n");
	CHECK(!strcmp(stp_ops, "off ") && stp_bpdu_filter, "bpdu while off: stp_off floods or filters them now");
	stp_ops[0] = 0;
	expect("set stp.enabled on", "ok\n");
	expect("set stp.priority 15", "ok\n");
	expect("apply", "ok\n");
	CHECK(!strcmp(stp_ops, "on prio ") && stp_enabled && stp_prio == 0xf0, "on: stp_setup");
	expect("set stp.maxage_s 40", "ok\n");
	expect("apply", "err stp: needs 2*(fwd_delay_s-1) >= maxage_s >= 2*(hello_s+1)\n");
	expect("set stp.fwd_delay_s 21", "ok\n");
	expect("apply", "ok\n");
	expect("set stp.hello_s 10", "ok\n");	/* 6.20.5's edges: 2*(12-1) = 22 = 2*(10+1) */
	expect("set stp.maxage_s 22", "ok\n");
	expect("set stp.fwd_delay_s 12", "ok\n");
	expect("apply", "ok\n");
	expect("set stp.maxage_s 21", "ok\n");
	expect("apply", "err stp: needs 2*(fwd_delay_s-1) >= maxage_s >= 2*(hello_s+1)\n");
	expect("set stp.maxage_s 23", "ok\n");
	expect("apply", "err stp: needs 2*(fwd_delay_s-1) >= maxage_s >= 2*(hello_s+1)\n");
	expect("set stp.hello_s 1", "ok\n");
	expect("set stp.maxage_s 40", "ok\n");
	expect("set stp.fwd_delay_s 21", "ok\n");
	expect("apply", "ok\n");
	expect("set stp.maxage_s 6", "ok\n");
	expect("set stp.hello_s 3", "ok\n");
	expect("apply", "err stp: needs 2*(fwd_delay_s-1) >= maxage_s >= 2*(hello_s+1)\n");
	expect("discard", "ok\n");
	expect("set stp.port.2.priority 100", "ok\n");
	expect("apply", "err stp.port.2: priority is a multiple of 16\n");
	expect("discard", "ok\n");
	expect("set stp.lag.4.priority 241", "err stp.lag.4.priority: 0-240\n");
	expect("set stp.lag.4.priority 8", "ok\n");
	expect("apply", "err stp.lag.4: priority is a multiple of 16\n");
	expect("discard", "ok\n");
	expect("set stp.port.9.cost 200000001", "err stp.port.9.cost: 0-200000000\n");
	/* port.3 is logical 2 (the test's machine: front = logical + 1) */
	stp_ops[0] = 0;
	expect("set stp.port.3.edge on", "ok\n");
	expect("set stp.port.3.guard root", "ok\n");
	expect("set stp.port.3.filter on", "ok\n");
	expect("set stp.port.3.cost 200000000", "ok\n");
	expect("set stp.port.3.priority 240", "ok\n");
	expect("set stp.port.3.p2p off", "ok\n");
	expect("apply", "ok\n");
	CHECK(stp_pflags[2] == (STP_PF_ENABLED | STP_PF_ADMEDGE | STP_PF_OPEREDGE | STP_PF_ROOTGUARD | STP_PF_FILTER)
	      && stp_pcost[2] == 200000000 && stp_pprio[2] == 240 && stp_pp2p[2] == 2 && !strcmp(stp_ops, "A2 0 "),
	      "stp.port.N: the entity's flags, cost, priority, p2p; an admin edge forwards at once");
	stp_ops[0] = 0;
	expect("set stp.port.3.edge off", "ok\n");
	expect("set stp.port.3.guard bpdu", "ok\n");
	expect("apply", "ok\n");
	CHECK(stp_pflags[2] == (STP_PF_ENABLED | STP_PF_BPDUGUARD | STP_PF_FILTER) && !strcmp(stp_ops, "A2 1 "),
	      "edge off drops the operational edge, listens first");
	stp_pflags[2] |= STP_PF_TRIPPED | STP_PF_OPEREDGE;	/* runtime, as the tree sets them */
	expect("set stp.port.3.p2p on", "ok\n");
	expect("apply", "ok\n");
	CHECK(stp_pflags[2] == (STP_PF_ENABLED | STP_PF_BPDUGUARD | STP_PF_FILTER | STP_PF_TRIPPED | STP_PF_OPEREDGE)
	      && stp_pp2p[2] == 1, "the runtime bits stay");
	stp_ops[0] = 0;
	expect("set stp.port.3.enabled off", "ok\n");
	expect("apply", "ok\n");
	expect("set stp.port.3.enabled on", "ok\n");
	expect("apply", "ok\n");
	CHECK(!strcmp(stp_ops, "A2 0 A2 1 ") && !(stp_pflags[2] & STP_PF_TRIPPED),
	      "enabled off: forwarding; on: listening, the guard untripped");
	stp_ops[0] = 0;
	expect("do stp.port.3.clear_guard", "ok\n");
	CHECK(!stp_ops[0], "clear_guard, not tripped: nothing");
	stp_pflags[2] |= STP_PF_TRIPPED;
	expect("do stp.port.3.clear_guard", "ok\n");
	CHECK(!strcmp(stp_ops, "R2 ") && !(stp_pflags[2] & STP_PF_TRIPPED), "clear_guard: the entity starts over");
	stp_pflags[STP_LAG_BASE + 3] |= STP_PF_TRIPPED;
	stp_ops[0] = 0;
	expect("do stp.lag.4.clear_guard", "ok\n");
	CHECK(!strcmp(stp_ops, "R13 "), "stp.lag.L's: entity STP_LAG_BASE + L - 1");
	stp_pflags[STP_LAG_BASE + 3] = 0;
	stp_ops[0] = 0;
	stp_gone = 1 << 4;		/* port.5 in a LAG */
	expect("set stp.port.5.enabled off", "ok\n");
	expect("set stp.lag.2.enabled off", "ok\n");
	expect("set stp.lag.2.edge on", "ok\n");
	expect("apply", "ok\n");
	CHECK(!strcmp(stp_ops, "A11 0 ") && stp_pflags[4] == STP_PF_AUTOEDGE
	      && stp_pflags[STP_LAG_BASE + 1] == (STP_PF_ADMEDGE | STP_PF_OPEREDGE),
	      "stp.lag.L is entity STP_LAG_BASE + L - 1; a LAG member's stp.port.N stored, no call");
	stp_ops[0] = 0;
	expect("do stp.port.5.mcheck", "ok\n");
	expect("do stp.port.1.mcheck", "ok\n");
	expect("do stp.lag.2.mcheck", "ok\n");
	expect("do stp.counters_clear", "ok\n");
	CHECK(!strcmp(stp_ops, "M0 M11 clear "), "mcheck: an entity's, none for a LAG member");
	stp_gone = 0;
	stp_ops[0] = 0;
	expect("set stp.enabled off", "ok\n");
	expect("set stp.port.1.enabled off", "ok\n");
	expect("apply", "ok\n");
	CHECK(!strcmp(stp_ops, "off ") && !stp_enabled, "off: stp_off; an entity off while the tree is off: stored");
	stp_ops[0] = 0;
	expect("do stp.port.1.mcheck", "ok\n");
	CHECK(!stp_ops[0], "mcheck while off: nothing");
	expect("get -d stp", "stp.version stp\nstp.priority 15\nstp.hello_s 1\nstp.maxage_s 40\nstp.fwd_delay_s 21\nstp.txhold 3\n"
	       "stp.pathcost short\nstp.bpdu filter\n"
	       "stp.lag.2.enabled off\nstp.lag.2.edge on\n"
	       "stp.port.1.enabled off\nstp.port.3.edge off\nstp.port.3.cost 200000000\nstp.port.3.priority 240\n"
	       "stp.port.3.guard bpdu\nstp.port.3.filter on\nstp.port.3.p2p on\nstp.port.5.enabled off\nok\n");
	om_init();
}

int main(int argc, char **argv)
{
	(void)argc;
	snprintf(out_dir, sizeof out_dir, "%s", argv[0]);
	if (strrchr(out_dir, '/'))
		*strrchr(out_dir, '/') = 0;
	else
		snprintf(out_dir, sizeof out_dir, ".");
	help_flash();
	help();
	cases();
	kinds();
	services();
	completion();
	l2svc();
	vlans();
	lags();
	stps();
	staging();
	binary();
	roundtrip();
	roundtrip_kinds();
	help_flash();			/* binary() freed it: help reads it */
	fuzz();
	printf("\n%d checks, %d failed\n", tests_run, tests_failed);
	return tests_failed != 0;
}
