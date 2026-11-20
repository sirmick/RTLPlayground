/*
 * om_svc.c - the network services for the object model (schema/dns.py,
 * ntp.py, syslog.py, web.py): apply their config. What the old dns server,
 * ntp, syslog, passwd and session commands did; ntp and syslog stop, take
 * the fields, start if enabled; a new password logs every session out.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "rtl837x_common.h"
#include "syslog.h"
#include "dns.h"
#include "ntp.h"
#include "sessions.h"
#include "om_gen.h"

extern __xdata char passwd[21];
extern __xdata uint16_t session_timeout;

uint8_t om_apply_syslog(uint8_t idx) __banked
{
	(void)idx;
	if (syslog_state.enabled)
		syslog_stop();
	memcpy(syslog_state.server_ip, cfg_run.syslog.server, 4);
	syslog_state.server_port = cfg_run.syslog.port;
	if (cfg_run.syslog.enabled)
		syslog_start();
	return OM_OK;
}

/* server1, server2: dns_state.server[2][4] (SDCC's _Static_assert only warns) */
typedef char dns_fits[offsetof(struct cfg_dns, server2) == 4 && sizeof(struct cfg_dns) == 8 ? 1 : -1];

uint8_t om_apply_dns(uint8_t idx) __banked
{
	(void)idx;
	memcpy(dns_state.server, &cfg_run.dns, 8);
	return OM_OK;
}

typedef char ntp_fits[sizeof(cfg_run.ntp.server) == sizeof(ntp_state.server) &&
		      E_DST_EU == NTP_DST_EU && E_DST_US == NTP_DST_US ? 1 : -1];

uint8_t om_apply_ntp(uint8_t idx) __banked
{
	(void)idx;
	ntp_stop();
	memcpy(ntp_state.server, cfg_run.ntp.server, sizeof(ntp_state.server));
	ntp_state.interval = cfg_run.ntp.interval_min;
	ntp_state.offset = cfg_run.ntp.timezone;
	ntp_state.dst = cfg_run.ntp.dst;
	if (cfg_run.ntp.enabled)
		ntp_start();
	return OM_OK;
}

typedef char web_fits[sizeof(cfg_run.web.password) == sizeof(passwd) ? 1 : -1];

uint8_t om_apply_web(uint8_t idx) __banked
{
	(void)idx;
	session_timeout = cfg_run.web.session_timeout_s;
	if (memcmp(passwd, cfg_run.web.password, sizeof(passwd))) {
		memcpy(passwd, cfg_run.web.password, sizeof(passwd));
		session_init();		/* the old password's logins end */
	}
	return OM_OK;
}
