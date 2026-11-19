/*
 * con_ring.c - the console UART rings (doc/sched/console.md); see
 * con_ring.h. Resident: the ISR reads them and console output may come from
 * any bank.
 */
#include <8051.h>
#include <stdint.h>
#include "con_ring.h"

__xdata uint8_t con_rx_buf[CON_RX_SIZE];
volatile __xdata uint8_t con_rx_head;
__xdata uint8_t con_rx_tail;
volatile __xdata uint16_t con_rx_overruns;
__xdata uint8_t con_tx_buf[CON_TX_SIZE];
__xdata uint16_t con_tx_head;
volatile __xdata uint16_t con_tx_tail;
volatile __xdata uint8_t con_tx_busy;

void con_ring_init(void)
{
	con_rx_head = con_rx_tail = 0;
	con_tx_head = con_tx_tail = 0;
	con_tx_busy = 0;
	con_rx_overruns = 0;
}

uint8_t con_getc(void)
{
	uint8_t c = con_rx_buf[con_rx_tail];
	con_rx_tail++;			/* 8 bits: wraps at CON_RX_SIZE */
	return c;
}

static __xdata uint8_t con_es;
static __xdata uint16_t con_t;

uint16_t con_tx_room(void)
{
	con_es = ES;
	ES = 0;			/* the ISR writes the tail: both bytes at once */
	con_t = con_tx_tail;
	ES = con_es;
	return (con_t - con_tx_head - 1) & CON_TX_MASK;
}

/* Waits while the ring is full. If the serial interrupt can't run (EA or ES
 * off: code that prints with interrupts off), the wait does the ISR's part
 * itself, polling TI, so it never hangs. ES is left as it was found.
 * The wait no longer kicks the watchdog (E1): a step waits only if it
 * prints more than the free ring, and the dumps that can are cli jobs, a
 * line a step (watchdog.md).
 * One ES-off section a byte: the tail (the ISR's) read whole, the store, and
 * the start of an idle transmitter, which the ISR must not see between. */
void con_putc(uint8_t c)
{
	con_t = (con_tx_head + 1) & CON_TX_MASK;	/* the head after this byte */
	con_es = ES;
	ES = 0;
	while (con_t == con_tx_tail) {		/* full: one byte has to go first */
		ES = con_es;
		if ((!EA || !con_es) && TI) {
			TI = 0;
			CON_ISR_TX();
		}
		ES = 0;
	}
	con_tx_buf[con_tx_head] = c;
	con_tx_head = con_t;
	if (!con_tx_busy) {
		con_tx_busy = 1;
		SBUF = con_tx_buf[con_tx_tail];
		con_tx_tail = (con_tx_tail + 1) & CON_TX_MASK;
	}
	ES = con_es;
}

/* Until the TX ring is empty and its last byte is out (the transmitter idle:
 * the TI after it found nothing more): before a reset that follows a print.
 * Polls TI itself when the ISR can't run, as con_putc does. */
void con_drain(void)
{
	while (con_tx_busy) {
		if ((!EA || !ES) && TI) {
			TI = 0;
			CON_ISR_TX();
		}
	}
}
