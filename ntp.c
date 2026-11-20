/*
 * SNTP client and local time for the RTL837x platform
 * This code is in the Public Domain
 */

#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "cmd_parser.h"
#include "uip/uip.h"
#include "dns.h"
#include "ntp.h"
#include "om_gen.h"


#define NTP_PORT	123
#define NTP_TRIES	3
#define NTP_TIMEOUT	(3 * SYS_TICK_HZ)
#define NTP_RETRY	30
#define NTP_UNIX_EPOCH	2208988800UL

#define NTP_IDLE	0
#define NTP_RESOLVE	1
#define NTP_SEND	2
#define NTP_WAIT	3

extern __xdata uint8_t sfr_data[4];

__xdata struct ntp_state ntp_state;
__xdata uip_ipaddr_t ntp_ip;
__xdata uint32_t ntp_now;
__xdata uint32_t ntp_t;
__xdata uint16_t ntp_year;
__xdata uint8_t  ntp_mon, ntp_mday, ntp_hour, ntp_min, ntp_sec, ntp_wday, ntp_mlen;

/* days in each month of a common year */
static __code const uint8_t ntp_mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static __code const char ntp_default_server[] = "pool.ntp.org";


static uint32_t ntp_uptime(void) __reentrant
{
	reg_read_m(RTL837X_REG_SEC_COUNTER);
	((__xdata uint8_t *)&ntp_now)[0] = sfr_data[3];
	((__xdata uint8_t *)&ntp_now)[1] = sfr_data[2];
	((__xdata uint8_t *)&ntp_now)[2] = sfr_data[1];
	((__xdata uint8_t *)&ntp_now)[3] = sfr_data[0];
	return ntp_now;
}


static uint8_t ntp_divmod(uint8_t d) __reentrant
{
	uint8_t i, r = 0;

	for (i = 0; i < 32; i++) {
		r = (r << 1) | (((uint8_t *)&ntp_t)[3] >> 7);
		ntp_t <<= 1;
		if (r >= d) {
			r -= d;
			ntp_t |= 1;
		}
	}
	return r;
}


static uint8_t ntp_leap(uint16_t y) __reentrant
{
	return !(y % 4) && ((y % 100) || !(y % 400));
}


static void ntp_split(uint32_t t) __reentrant
{
	uint16_t days;

	ntp_t = t;
	ntp_sec = ntp_divmod(60);
	ntp_min = ntp_divmod(60);
	ntp_hour = ntp_divmod(24);
	days = ntp_t;
	ntp_wday = (days + 4) % 7;
	for (ntp_year = 1970; days >= 365 + ntp_leap(ntp_year); ntp_year++)
		days -= 365 + ntp_leap(ntp_year);
	for (ntp_mon = 1; ; ntp_mon++) {
		ntp_mlen = ntp_mdays[ntp_mon - 1] + (ntp_mon == 2 && ntp_leap(ntp_year));
		if (days < ntp_mlen)
			break;
		days -= ntp_mlen;
	}
	ntp_mday = days + 1;
}


static uint8_t ntp_sunday(uint8_t n) __reentrant
{
	uint8_t first = (uint16_t)(ntp_wday + 36 - ntp_mday) % 7U;

	first = 1 + (uint16_t)(7 - first) % 7U;
	if (n)
		return first + 7 * (n - 1);
	return first + 7 * ((uint16_t)(ntp_mlen - first) / 7U);
}


static uint8_t ntp_in_dst(uint8_t sm, uint8_t sn, uint8_t sh, uint8_t em, uint8_t en, uint8_t eh) __reentrant
{
	uint8_t day;

	if (ntp_mon < sm || ntp_mon > em)
		return 0;
	if (ntp_mon > sm && ntp_mon < em)
		return 1;
	if (ntp_mon == sm) {
		day = ntp_sunday(sn);
		return ntp_mday > day || (ntp_mday == day && ntp_hour >= sh);
	}
	day = ntp_sunday(en);
	return ntp_mday < day || (ntp_mday == day && ntp_hour < eh);
}


static void ntp_print2(uint8_t v) __reentrant
{
	write_char('0' + v / 10);
	write_char('0' + v % 10);
}


static void ntp_print_offset(int16_t off) __reentrant
{
	uint16_t a = off < 0 ? -off : off;

	write_char(off < 0 ? '-' : '+');
	ntp_print2(a / 60);
	write_char(':');
	ntp_print2(a % 60);
}


static void ntp_connect(__xdata uint8_t *a) __reentrant
{
	if (ntp_state.conn)
		uip_udp_remove(ntp_state.conn);
	uip_ipaddr(ntp_ip, a[0], a[1], a[2], a[3]);
	ntp_state.conn = uip_udp_new(&ntp_ip, HTONS(NTP_PORT));
	if (!ntp_state.conn)
		print_string("NTP: no free UDP connection\n");
}


static void ntp_retry(void) __reentrant
{
	ntp_state.phase = NTP_IDLE;
	ntp_state.next = ntp_uptime() + NTP_RETRY;
}


static void ntp_request(void) __reentrant
{
	__xdata uint8_t *p = (__xdata uint8_t *)uip_appdata;
	uint8_t i;

	for (i = 1; i < 48; i++)
		p[i] = 0;
	p[0] = 0x23;
	uip_udp_send(48);
}


static void ntp_receive(void) __reentrant
{
	__xdata uint8_t *p = (__xdata uint8_t *)uip_appdata;

	if (ntp_state.phase != NTP_WAIT || uip_datalen() < 48)
		return;
	if ((p[0] & 7) != 4 || (p[0] & 0xc0) == 0xc0 || !p[1] || p[1] > 15) {
		ntp_retry();
		return;
	}
	((__xdata uint8_t *)&ntp_state.utc)[0] = p[43];
	((__xdata uint8_t *)&ntp_state.utc)[1] = p[42];
	((__xdata uint8_t *)&ntp_state.utc)[2] = p[41];
	((__xdata uint8_t *)&ntp_state.utc)[3] = p[40];
	ntp_state.utc -= NTP_UNIX_EPOCH;
	ntp_state.at = ntp_uptime();
	ntp_state.stratum = p[1];
	if (!ntp_state.synced) {
		ntp_state.synced = 1;
		print_string("NTP: time set from ");
		print_ip(ntp_state.addr);
		write_char('\n');
	}
	ntp_state.phase = NTP_IDLE;
	ntp_state.next = ntp_state.at + (uint32_t)ntp_state.interval * 60;
}


void ntp_init(void) __banked
{
	for (ntp_mon = 0; ntp_mon < sizeof(ntp_default_server); ntp_mon++)
		ntp_state.server[ntp_mon] = ntp_default_server[ntp_mon];
	ntp_state.enabled = 0;
	ntp_state.interval = 60;
	ntp_state.offset = 0;
	ntp_state.dst = NTP_DST_OFF;
	ntp_state.phase = NTP_IDLE;
	ntp_state.synced = 0;
	ntp_state.conn = 0;
}


void ntp_start(void) __banked
{
	ntp_state.enabled = 1;
	ntp_state.phase = NTP_IDLE;
	ntp_state.next = 0;
	ntp_state.addr[0] = ntp_state.addr[1] = ntp_state.addr[2] = ntp_state.addr[3] = 0;
	ntp_connect(ntp_state.addr);
}


void ntp_stop(void) __banked
{
	ntp_state.enabled = 0;
	if (ntp_state.conn) {
		uip_udp_remove(ntp_state.conn);
		ntp_state.conn = 0;
	}
}


/* state.ntp (schema/state_svc.py): the time from the last synchronisation */
#define N	om_st.ntp

static __xdata int16_t g_off;
static __xdata uint8_t g_dst;

uint8_t om_get_ntp(uint8_t idx) __banked
{
	(void)idx;
	memset((__xdata uint8_t *)&N, 0, sizeof(N));
	memcpy(N.server_ip, ntp_state.addr, 4);
	N.stratum = ntp_state.stratum;
	if (!ntp_state.synced)
		return OM_OK;
	N.synced = 1;
	ntp_uptime();		/* its value is unused: it leaves the uptime in ntp_now */
	N.last_sync_s = ntp_now - ntp_state.at;
	N.time = ntp_state.utc + N.last_sync_s;
	g_off = ntp_state.offset;
	g_dst = ntp_state.dst;
	if (g_dst) {				/* EU's rule is in UTC, the US's in local time */
		ntp_split(g_dst == NTP_DST_US ? N.time + (int32_t)g_off * 60 : N.time);	/* sets ntp_mon.. */
		g_dst = g_dst == NTP_DST_EU ? ntp_in_dst(3, 0, 1, 10, 0, 1) : ntp_in_dst(3, 2, 2, 11, 1, 1);
	}
	N.dst_active = g_dst;
	N.local_offset_min = g_dst ? g_off + 60 : g_off;
	return OM_OK;
}


void ntp_show(void) __banked __reentrant
{
	int16_t off = ntp_state.offset;

	print_string(ntp_state.enabled ? "NTP on, server " : "NTP off, server ");
	print_string_x(ntp_state.server);
	print_string(", address ");
	print_ip(ntp_state.addr);
	print_string(", interval ");
	itoa_short(ntp_state.interval);
	print_string(" min, timezone ");
	ntp_print_offset(off);
	print_string(ntp_state.dst == NTP_DST_EU ? ", dst eu\n" : ntp_state.dst == NTP_DST_US ? ", dst us\n" : ", dst off\n");
}


void ntp_callback(uint16_t lport) __banked __reentrant
{
	if (!ntp_state.conn || lport != ntp_state.conn->lport)
		return;
	if (uip_newdata()) {
		ntp_receive();
		return;
	}
	switch (ntp_state.phase) {
	case NTP_IDLE:
		if (ntp_uptime() < ntp_state.next || dns_state.status == DNS_PENDING)
			return;
		memcpy(dns_state.name, ntp_state.server, DNS_NAME_LEN);
		dns_state.verbose = 0;
		dns_lookup();
		ntp_state.phase = NTP_RESOLVE;
		return;
	case NTP_RESOLVE:
		if (dns_state.status == DNS_PENDING)
			return;
		if (dns_state.status != DNS_DONE) {
			ntp_retry();
			return;
		}
		memcpy(ntp_state.addr, dns_state.addr, 4);
		ntp_state.tries = 0;
		ntp_state.phase = NTP_SEND;
		ntp_connect(ntp_state.addr);
		return;
	case NTP_SEND:
		timer_set(&ntp_state.wait, NTP_TIMEOUT);
		ntp_state.phase = NTP_WAIT;
		ntp_request();
		return;
	case NTP_WAIT:
		if (!timer_expired(&ntp_state.wait))
			return;
		if (++ntp_state.tries < NTP_TRIES)
			ntp_state.phase = NTP_SEND;
		else
			ntp_retry();
	}
}
