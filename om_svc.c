/*
 * om_svc.c - the network services for the object model (schema/syslog.py):
 * apply syslog's config. The hardware-free half of the old syslog command:
 * stop, take the fields, start if enabled.
 */
#include <stdint.h>
#include <stdbool.h>
#include "rtl837x_common.h"
#include "syslog.h"
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
