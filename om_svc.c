/*
 * om_svc.c - the network services for the object model (schema/dns.py,
 * syslog.py): apply their config. What the old dns server and syslog
 * commands did; syslog stops, takes the fields, starts if enabled.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "rtl837x_common.h"
#include "syslog.h"
#include "dns.h"
#include "om_gen.h"

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
