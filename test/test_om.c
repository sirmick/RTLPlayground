/*
 * test_om.c - the object model's engine (om.c) on the host, against the
 * tables generated from the real schema (build/om). The port handlers are
 * stubs that record their calls; the engine is the firmware source.
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

uint8_t om_apply_port(uint8_t idx)
{
	applied[idx]++;
	applied_old[idx] = cfg_roll.port[idx];
	return OM_OK;
}

uint8_t om_check_port(uint8_t idx)
{
	checks++;
	if (idx == fail_check_on) {
		om_fail("test check failed");
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

/* ---- driving it ---------------------------------------------------------- */

static const char *run(const char *line)
{
	out_reset();
	snprintf((char *)cmd_buffer, CMD_BUF_SIZE, "%s", line);
	err_status = 0;
	tokenize();
	om_cmd();
	while (cli_job) {
		if (!om_line())
			cli_job = 0;
	}
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

static void cases(void)
{
	om_init();

	printf("get\n");
	expect("get port.3.mtu", "port.3.mtu 16383\nok\n");
	expect("get port.9.eee", "err port.9.eee: not on this port\n");
	expect("get port.3.isolate", "port.3.isolate 1-9,cpu\nok\n");
	expect("get port.2", "port.2.name \nport.2.enabled on\nport.2.speed auto\nport.2.duplex auto\n"
	       "port.2.mtu 16383\nport.2.pvid 1\nport.2.ingress all\nport.2.ingress_filter on\n"
	       "port.2.lldp permit\nport.2.eee on\nport.2.isolate 1-9,cpu\nok\n");
	expect("get port.9", "port.9.name \nport.9.mtu 16383\nport.9.pvid 1\nport.9.ingress all\n"
	       "port.9.ingress_filter on\nport.9.lldp permit\nport.9.isolate 1-9,cpu\nok\n");
	expect("get port.*.pvid", "port.1.pvid 1\nport.2.pvid 1\nport.3.pvid 1\nport.4.pvid 1\n"
	       "port.5.pvid 1\nport.6.pvid 1\nport.7.pvid 1\nport.8.pvid 1\nport.9.pvid 1\nok\n");
	{
		char all[512];
		snprintf(all, sizeof all, "%s", run("get port.*.pvid"));
		expect("get port.pvid", all);
	}
	expect("get vlan.1", "err vlan.1: port\n");
	expect("get port.10", "err port.10: no such port\n");
	expect("get port.0.mtu", "err port.0.mtu: no such port\n");
	expect("get", "port\nstate.port\nok\n");

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

/* random lines: never a crash (ASan), always one status line */
static void fuzz(void)
{
	static const char *words[] = {"get", "set", "del", "diff", "apply", "discard", "do", "-c",
		"port", "port.", "port.1", "port.*", "port.9.", "state.port.3", "state.", ".", "..",
		"port.3.mtu", "port.3.name", "port.1.isolate", "port.2.cabletest", "port.4.speed",
		"1", "0", "-", ",", "1-", "1-9", "cpu", "all", "none", "on", "off", "auto", "10m",
		"4294967296", "18446744073709551616", "\"", "\\", "*", "port.1.cabletest.a"};
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
	roundtrip();
	fuzz();
	printf("\n%d checks, %d failed\n", tests_run, tests_failed);
	return tests_failed != 0;
}
