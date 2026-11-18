/*
 * con_ring.c - the console UART rings; see con_ring.h. Resident: the ISR
 * reads them and console output may come from any bank.
 */
#include <8051.h>
#include <stdint.h>
#include "con_ring.h"

__xdata uint8_t con_rx_buf[CON_RX_SIZE];
volatile __xdata uint8_t con_rx_head;
__xdata uint8_t con_rx_tail;
volatile __xdata uint16_t con_rx_overruns;
__xdata uint8_t con_tx_buf[CON_TX_SIZE];
__xdata uint8_t con_tx_head;
volatile __xdata uint8_t con_tx_tail;
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
	con_rx_tail = (con_rx_tail + 1) & (CON_RX_SIZE - 1);
	return c;
}

uint8_t con_tx_room(void)
{
	return (con_tx_tail - con_tx_head - 1) & (CON_TX_SIZE - 1);
}

void con_putc(uint8_t c)
{
	while (!con_tx_room())
		;
	con_tx_buf[con_tx_head] = c;
	/* The ISR must not see the transmitter idle between the store and the
	 * start: hold the serial interrupt off across both. */
	ES = 0;
	con_tx_head = (con_tx_head + 1) & (CON_TX_SIZE - 1);
	if (!con_tx_busy) {
		con_tx_busy = 1;
		SBUF = con_tx_buf[con_tx_tail];
		con_tx_tail = (con_tx_tail + 1) & (CON_TX_SIZE - 1);
	}
	ES = 1;
}
