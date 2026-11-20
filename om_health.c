/*
 * om_health.c - state.health, state.con and state.web for the object model
 * (schema/state_health.py), and the stack paint state.health.stack_headroom
 * reads. state.sched is sched_health.c's (it reads the scheduler's tables).
 */
#include <8051.h>
#include <stdint.h>
#include "rtl837x_common.h"
#include "con_ring.h"
#include "sessions.h"
#include "uip/uip.h"
#include "om_gen.h"

extern __xdata uint32_t last_session_use[SESSIONS];

#define STACK_PAINT	0xa5

static __xdata uint8_t i, from, ea;
static __xdata uint32_t t_now;
static __xdata uint16_t * __xdata dp, * __xdata lp;
#define NOW(h)	(((__xdata uint16_t *)&t_now)[h])

/* At boot, before the scheduler: the stack area above the current frame
 * painted, so the getter can tell how much was ever used (it grows up to
 * 0xff). Interrupts held off, so an ISR doesn't push into it mid-paint. */
void health_stack_paint(void) __banked
{
	from = SP + 4;
	ea = EA;
	EA = 0;
	for (i = 0xff; i >= from; i--)
		*(__idata uint8_t *)i = STACK_PAINT;
	EA = ea;
}

uint8_t om_get_health(uint8_t idx) __banked
{
	(void)idx;
	om_st.health.hw_timeouts = hw_timeouts;
	om_st.health.hw_timeout_reg = hw_timeout_reg;
	for (i = 0; i < 0x80 && *(__idata uint8_t *)(0xff - i) == STACK_PAINT; i++)
		;
	om_st.health.stack_headroom = i;
	return OM_OK;
}

uint8_t om_get_con(uint8_t idx) __banked
{
	(void)idx;
	ES = 0;			/* the ISR writes them: both bytes at once */
	om_st.con.rx_overruns = con_rx_overruns;
	om_st.con.rx_used = con_rx_head - con_rx_tail;
	ES = 1;
	om_st.con.tx_free = con_tx_room();
	return OM_OK;
}



