/*
 * test_ntp.c - state.ntp's getter (ntp.c's om_get_ntp) on the host, with a
 * made-up ntp_state and uptime: the EU and US DST edges, the time zone, and
 * the date split (ntp_split) it rests on.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ntp.c"			/* its statics: ntp_split, the DST rules */

/* the uptime ntp_uptime() reads: RTL837X_REG_SEC_COUNTER, big endian */
uint8_t sfr_data[4];
static uint32_t up;
void reg_read_m(uint16_t reg)
{
	(void)reg;
	sfr_data[0] = up >> 24; sfr_data[1] = up >> 16; sfr_data[2] = up >> 8; sfr_data[3] = up;
}

/* ntp.c's other edges, never reached here */
union om_st om_st;
struct dns_state dns_state;
struct uip_udp_conn *uip_udp_conn;
struct uip_udp_conn uip_udp_conns[UIP_UDP_CONNS];
void *uip_appdata;
u8_t uip_flags;
u16_t uip_len;
struct uip_udp_conn *uip_udp_new(uip_ipaddr_t *a, u16_t p) { (void)a; (void)p; return 0; }
void uip_send(const void *d, uint16_t len) { (void)d; (void)len; }
void dns_lookup(void) { }
void timer_set(struct timer *t, clock_time_t i) { (void)t; (void)i; }
int timer_expired(struct timer *t) { (void)t; return 0; }
void print_string(const char *s) { (void)s; }
void print_string_x(char *s) { (void)s; }
void print_ip(uint8_t *a) { (void)a; }
void write_char(char c) { (void)c; }
void itoa_short(uint16_t v) { (void)v; }

static int fails, checks;
#define CHECK(c, m) do { checks++; if (!(c)) { fails++; printf("  FAIL: %s  (%s:%d)\n", m, __FILE__, __LINE__); } \
			 else printf("  ok:   %s\n", m); } while (0)

/* synchronised at uptime 100 to utc t, read at uptime 100 + d */
static void at(uint32_t t, uint32_t d, uint8_t dst, int16_t off)
{
	memset(&ntp_state, 0, sizeof(ntp_state));
	ntp_state.synced = 1;
	ntp_state.utc = t;
	ntp_state.at = 100;
	ntp_state.dst = dst;
	ntp_state.offset = off;
	ntp_state.stratum = 2;
	up = 100 + d;
	om_get_ntp(0);
}

static void split(uint32_t t, uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s, uint8_t wd, const char *m)
{
	ntp_split(t);
	CHECK(ntp_year == y && ntp_mon == mo && ntp_mday == d && ntp_hour == h && ntp_min == mi && ntp_sec == s &&
	      ntp_wday == wd, m);
}

int main(void)
{
	printf("state.ntp: not synchronised\n");
	memset(&ntp_state, 0, sizeof(ntp_state));
	ntp_state.stratum = 3;
	ntp_state.addr[0] = 10;
	om_get_ntp(0);
	CHECK(!om_st.ntp.synced && !om_st.ntp.time && !om_st.ntp.last_sync_s && !om_st.ntp.local_offset_min &&
	      om_st.ntp.stratum == 3 && om_st.ntp.server_ip[0] == 10, "all 0 but the stratum and the server");

	printf("the time, the last sync, the zone\n");
	at(1774745999, 0, NTP_DST_OFF, 330);
	CHECK(om_st.ntp.synced && om_st.ntp.time == 1774745999 && om_st.ntp.last_sync_s == 0 &&
	      om_st.ntp.local_offset_min == 330 && !om_st.ntp.dst_active && om_st.ntp.stratum == 2, "synced now, +05:30");
	at(1774745999, 3601, NTP_DST_OFF, -720);
	CHECK(om_st.ntp.time == 1774745999 + 3601 && om_st.ntp.last_sync_s == 3601 && om_st.ntp.local_offset_min == -720,
	      "an hour since: the time moves on, -12:00");

	printf("EU: 01:00 UTC, last Sunday of March and of October\n");
	at(1774745999, 0, NTP_DST_EU, 60);		/* 2026-03-29 00:59:59 UTC */
	CHECK(!om_st.ntp.dst_active && om_st.ntp.local_offset_min == 60, "2026-03-29 00:59:59 UTC: not yet");
	at(1774745999, 1, NTP_DST_EU, 60);		/* 01:00:00 */
	CHECK(om_st.ntp.dst_active && om_st.ntp.local_offset_min == 120, "01:00:00 UTC: DST, +02:00");
	at(1792889999, 0, NTP_DST_EU, 60);		/* 2026-10-25 00:59:59 UTC */
	CHECK(om_st.ntp.dst_active && om_st.ntp.local_offset_min == 120, "2026-10-25 00:59:59 UTC: still DST");
	at(1792890000, 0, NTP_DST_EU, 60);
	CHECK(!om_st.ntp.dst_active && om_st.ntp.local_offset_min == 60, "01:00:00 UTC: over");
	at(1774745999, 1, NTP_DST_EU, -300);		/* EU's rule is in UTC, whatever the zone */
	CHECK(om_st.ntp.dst_active && om_st.ntp.local_offset_min == -240, "EU at -05:00: the same UTC instant");

	printf("US: 02:00 local, second Sunday of March, first of November\n");
	at(1772953199, 0, NTP_DST_US, -300);		/* 2026-03-08 06:59:59 UTC = 01:59:59 EST */
	CHECK(!om_st.ntp.dst_active && om_st.ntp.local_offset_min == -300, "2026-03-08 01:59:59 EST: not yet");
	at(1772953200, 0, NTP_DST_US, -300);		/* 02:00 EST */
	CHECK(om_st.ntp.dst_active && om_st.ntp.local_offset_min == -240, "02:00 EST: EDT");
	at(1793512799, 0, NTP_DST_US, -300);		/* 2026-11-01 05:59:59 UTC = 01:59:59 EDT */
	CHECK(om_st.ntp.dst_active && om_st.ntp.local_offset_min == -240, "2026-11-01 01:59:59 EDT: still");
	at(1793512800, 0, NTP_DST_US, -300);		/* 02:00 EDT = 01:00 EST */
	CHECK(!om_st.ntp.dst_active && om_st.ntp.local_offset_min == -300, "02:00 EDT: back to EST");
	at(1772953200, 0, NTP_DST_US, -480);		/* 07:00 UTC is 23:00 PST the day before */
	CHECK(!om_st.ntp.dst_active, "US at -08:00: its own local 02:00, not the east's");

	printf("the date split\n");
	split(0, 1970, 1, 1, 0, 0, 0, 4, "1970-01-01 00:00:00, a Thursday");
	split(951914096, 2000, 3, 1, 12, 34, 56, 3, "2000-03-01 12:34:56 (2000 is a leap year)");
	split(1709251199, 2024, 2, 29, 23, 59, 59, 4, "2024-02-29 23:59:59");
	split(1709251200, 2024, 3, 1, 0, 0, 0, 5, "2024-03-01 00:00:00");
	split(1798761599, 2026, 12, 31, 23, 59, 59, 4, "2026-12-31 23:59:59");

	printf("\n%s (%d checks)\n", fails ? "TEST_NTP FAILED" : "TEST_NTP PASSED", checks);
	return fails != 0;
}
