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
	om_st.port.cabletest_a_length_m = st_len;
	om_st.port.cabletest_a_result = E_CTRES_OPEN;
	return OM_OK;
}

uint8_t om_do_port_cabletest(uint8_t idx)
{
	ct_calls++;
	return OM_OK;
}

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
uint8_t om_apply_tstorm(uint8_t idx) { t_storm[idx]++; return OM_OK; }

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
			job_on = 0;
			return;
		}
		om_apply_st = E_CFGAPPLY_CONFIRM;
		job_st = 1;
		return;
	case 1:			/* CFG_WINDOW: due a second at a time (seconds()), then 100 ms */
		if (om_confirmed) {
			om_settle(OM_A_DONE);
			job_on = 0;
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
	default:		/* CFG_UNDO */
		r = om_apply_next();
		if (r == OM_A_MORE)
			return;
		om_settle(r);
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

/* the test schema's port.N.tstorm.T, at its defaults */
#define STORM1(n, t) "port." n ".tstorm." t ".rate 0\nport." n ".tstorm." t ".unit pps\n"
#define STORM(n) STORM1(n, "bcast") STORM1(n, "mcast") STORM1(n, "ucast") STORM1(n, "umcast")

static void cases(void)
{
	om_init();

	printf("get\n");
	expect("get port.3.mtu", "port.3.mtu 16383\nok\n");
	expect("get port.9.eee", "err port.9.eee: not on this port\n");
	expect("get port.3.isolate", "port.3.isolate 1-9,cpu\nok\n");
	expect("get port.2", "port.2.name \nport.2.enabled on\nport.2.speed auto\nport.2.duplex auto\n"
	       "port.2.mtu 16383\nport.2.pvid 1\nport.2.ingress all\nport.2.ingress_filter on\n"
	       "port.2.lldp permit\nport.2.eee on\nport.2.isolate 1-9,cpu\n" STORM("2") "ok\n");
	expect("get port.9", "port.9.name \nport.9.mtu 16383\nport.9.pvid 1\nport.9.ingress all\n"
	       "port.9.ingress_filter on\nport.9.lldp permit\nport.9.isolate 1-9,cpu\n" STORM("9") "ok\n");
	expect("get port.*.pvid", "port.1.pvid 1\nport.2.pvid 1\nport.3.pvid 1\nport.4.pvid 1\n"
	       "port.5.pvid 1\nport.6.pvid 1\nport.7.pvid 1\nport.8.pvid 1\nport.9.pvid 1\nok\n");
	{
		char all[512];
		snprintf(all, sizeof all, "%s", run("get port.*.pvid"));
		expect("get port.pvid", all);
	}
	expect("get vlan.1", "err vlan.1: port.* tsys tvlan.* tlag.* tstp tstp.port.* port.*.tstorm.*\n");
	expect("get port.10", "err port.10: no such port\n");
	expect("get port.0.mtu", "err port.0.mtu: no such port\n");
	expect("get", "port.*\nstate.port.*\nstate.cfg\ntsys\ntvlan.*\ntlag.*\ntstp\ntstp.port.*\nport.*.tstorm.*\n"
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
	expect("set port.1.nonesuch 1", "err port.1.nonesuch: name enabled speed duplex mtu pvid ingress ingress_filter lldp eee isolate cabletest\n");
	expect("set port.1", "err port.1: name enabled speed duplex mtu pvid ingress ingress_filter lldp eee isolate cabletest\n");
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
	expect("get state.port.3.cabletest.a",
	       "state.port.3.cabletest.a.result open\nstate.port.3.cabletest.a.length_m 12.3\nok\n");
	st_len = 5;
	expect("get state.port.3.cabletest.a.length_m", "state.port.3.cabletest.a.length_m 0.5\nok\n");
	st_tx_good = 18446744073709551615ULL;
	expect("get state.port.3.tx_good", "state.port.3.tx_good 18446744073709551615\nok\n");
	st_tx_good = 0;
	expect("get state.port.3.tx_good", "state.port.3.tx_good 0\nok\n");
	gets = 0;
	run("get state.port");
	CHECK(gets == 9, "get state.port: the getter once per port");
	expect("get state.port.9.cabletest", "err state.port.9.cabletest: not on this port\n");
	expect("get state.port.9.media", "state.port.9.media sfp\nok\n");

	printf("do\n");
	expect("do port.2.cabletest", "ok\n");
	CHECK(ct_calls == 1, "do calls the action once");
	expect("do port.9.cabletest", "err port.9.cabletest: not on this port\n");
	expect("do port.2.mtu", "err port.2.mtu: not an action\n");
	expect("set port.2.cabletest 1", "err port.2.cabletest: an action: do it\n");
	expect("get port.2.cabletest", "err port.2.cabletest: name enabled speed duplex mtu pvid ingress ingress_filter lldp eee isolate cabletest\n");
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
	expect("set tvlan.40.name y", "err tvlan.40.name: no free entry\n");
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
	expect("get port.3.tstorm.mcast", "port.3.tstorm.mcast.rate 0\nport.3.tstorm.mcast.unit pps\nok\n");
	expect("set port.3.tstorm.mcast.rate 1000", "ok\n");
	CHECK(cfg_cand.port_tstorm[2][1].rate == 1000, "port.3.tstorm.mcast is [logical 2][1]");
	expect("set port.3.tstorm.mcast.unit kbps", "ok\n");
	expect("get -c port.3.tstorm.mcast", "port.3.tstorm.mcast.rate 1000\nport.3.tstorm.mcast.unit kbps\nok\n");
	expect("get port.3.tstorm.xcast", "err port.3.tstorm.xcast: bcast mcast ucast umcast\n");
	expect("get port.3.tstorm.*.unit", "port.3.tstorm.bcast.unit pps\nport.3.tstorm.mcast.unit pps\n"
	       "port.3.tstorm.ucast.unit pps\nport.3.tstorm.umcast.unit pps\nok\n");
	expect("get port.*.tstorm.umcast.rate", "port.1.tstorm.umcast.rate 0\nport.2.tstorm.umcast.rate 0\n"
	       "port.3.tstorm.umcast.rate 0\nport.4.tstorm.umcast.rate 0\nport.5.tstorm.umcast.rate 0\n"
	       "port.6.tstorm.umcast.rate 0\nport.7.tstorm.umcast.rate 0\nport.8.tstorm.umcast.rate 0\n"
	       "port.9.tstorm.umcast.rate 0\nok\n");
	{
		char a[1024];
		snprintf(a, sizeof a, "%s", run("get port.*.tstorm.umcast.rate"));
		expect("get port.tstorm.umcast.rate", a);
	}
	{
		const char *o = run("get port.2");
		CHECK(strstr(o, "port.2.isolate 1-9,cpu\nport.2.tstorm.bcast.rate 0\n") &&
		      strstr(o, "port.2.tstorm.umcast.unit pps\nok\n"), "get port.2: the port, then its sub-objects");
	}
	memset(t_storm, 0, sizeof t_storm);
	memset(t_stpp, 0, sizeof t_stpp);
	expect("apply", "ok\n");
	CHECK(t_storm[2 * 4 + 1] == 1 && t_stpp[2] == 1, "a nested handler gets logical * 4 + type");
	expect("diff", "ok\n");
	expect("del port.3.tstorm.mcast", "ok\n");
	expect("diff", "- port.3.tstorm.mcast.rate 1000\n+ port.3.tstorm.mcast.rate 0\n"
	       "- port.3.tstorm.mcast.unit kbps\n+ port.3.tstorm.mcast.unit pps\nok\n");
	expect("discard", "ok\n");

	printf("several paths\n");
	expect("get port.1.mtu tlag.1.mode state.tsys.ip", "port.1.mtu 16383\ntlag.1.mode static\nstate.tsys.ip 10.0.0.7\nok\n");
	expect("get -c port.1.mtu tvlan.1.members", "port.1.mtu 16383\ntvlan.1.members 1-9\nok\n");
	expect("get port.1.mtu nope", "port.1.mtu 16383\nerr nope: port.* tsys tvlan.* tlag.* tstp tstp.port.* port.*.tstorm.*\n");
	expect("get port.1.mtu port.9.eee", "port.1.mtu 16383\nerr port.9.eee: not on this port\n");
	expect("get port.1.mtu tlag.9", "port.1.mtu 16383\nerr tlag.9: 1-4\n");
	{
		const char *o = run("get state");
		CHECK(!strncmp(o, "state.port.1.link ", 18) && strstr(o, "state.port.9.rx_bad 0\nstate.cfg.pending ") && strstr(o, "state.cfg.confirm_s 0\nstate.tsys.bridge ") &&
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
	expect("get state.cfg", "state.cfg.pending 0\nstate.cfg.dirty off\nstate.cfg.apply idle\nstate.cfg.confirm_s 0\nok\n");
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
	CHECK(job_steps == 3, "apply: a step for each changed instance (port.3, tlag.2), and the last");
	CHECK(!memcmp(&cfg_roll, &cfg_run, sizeof cfg_run), "apply without a window: the rollback copy is running again");
	expect("get state.cfg", "state.cfg.pending 0\nstate.cfg.dirty on\nstate.cfg.apply done\nstate.cfg.confirm_s 0\nok\n");
	memset(applied, 0, sizeof applied);
	job_steps = 0;
	expect("apply", "ok\n");
	CHECK(!applied[2] && job_steps == 1, "an empty diff: apply calls no handler");

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
	om_cmd();		/* the walk starts; nobody steps it yet */
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

	printf("boot\n");
	expect("set port.5.mtu 5000", "ok\n");
	memcpy(&mid, &cfg_cand, sizeof mid);
	fail_apply_on = 4;
	out_reset();
	om_boot_apply();
	out_buf[out_len] = 0;
	CHECK(strstr(out_buf, "Boot config not applied\n") && !memcmp(&cfg_run, &before, sizeof before) &&
	      !memcmp(&cfg_cand, &mid, sizeof mid), "a boot handler fails: rolled back, said once");
	expect("get state.cfg.apply", "state.cfg.apply rolled_back\nok\n");
	expect("discard", "ok\n");
	om_boot_apply();
	expect("get state.cfg.apply", "state.cfg.apply idle\nok\n");
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
			sprintf(path, "port.%d.tstorm.%s.rate", 1 + rand() % 9, storm[rand() % 4]);
			sprintf(val, "%d", rand() % 1048576);
			break;
		default:
			if (rand() % 3 == 0) {	/* apply now and then: running moves too */
				run("apply");
				continue;
			}
			sprintf(path, "port.%d.tstorm.%s.unit", 1 + rand() % 9, storm[rand() % 4]);
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
		"tstp.port.3", "port.3.tstorm", "port.3.tstorm.mcast", "port.*.tstorm.*.rate", "port.tstorm.x",
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

int main(void)
{
	cases();
	kinds();
	staging();
	roundtrip();
	roundtrip_kinds();
	fuzz();
	printf("\n%d checks, %d failed\n", tests_run, tests_failed);
	return tests_failed != 0;
}
