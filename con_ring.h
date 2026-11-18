/*
 * con_ring.h - the console UART rings (doc/sched/console.md).
 *
 * RX, 64 B: the serial ISR stores each byte, or counts an overrun when the
 * ring is full; the con task drains it. TX, 128 B: con_putc() queues a byte
 * and starts the transmitter if it is idle, busy-waiting while the ring is
 * full; the ISR sends the next byte on each TI.
 *
 * The ISR side is macros, expanded in isr_serial: a call from an ISR makes
 * SDCC save every register on each entry.
 */
#ifndef CON_RING_H
#define CON_RING_H

#include <stdint.h>

#define CON_RX_SIZE	64	/* powers of 2 */
#define CON_TX_SIZE	128
#define CON_TX_LINE	100	/* EV_CON_TX: room for a line */

extern __xdata uint8_t con_rx_buf[CON_RX_SIZE];
extern volatile __xdata uint8_t con_rx_head;	/* written by the ISR */
extern __xdata uint8_t con_rx_tail;		/* written by the reader */
extern volatile __xdata uint16_t con_rx_overruns;
extern __xdata uint8_t con_tx_buf[CON_TX_SIZE];
extern __xdata uint8_t con_tx_head;		/* written by con_putc */
extern volatile __xdata uint8_t con_tx_tail;	/* written by the ISR */
extern volatile __xdata uint8_t con_tx_busy;	/* the transmitter is sending */

/* In isr_serial, for a received byte c (an 8-bit local or SBUF). */
#define CON_ISR_RX(c) do { \
	if (((con_rx_head + 1) & (CON_RX_SIZE - 1)) == con_rx_tail) { \
		con_rx_overruns++; \
	} else { \
		con_rx_buf[con_rx_head] = (c); \
		con_rx_head = (con_rx_head + 1) & (CON_RX_SIZE - 1); \
	} \
} while (0)

/* In isr_serial on TI, after clearing TI. */
#define CON_ISR_TX() do { \
	if (con_tx_tail != con_tx_head) { \
		SBUF = con_tx_buf[con_tx_tail]; \
		con_tx_tail = (con_tx_tail + 1) & (CON_TX_SIZE - 1); \
	} else { \
		con_tx_busy = 0; \
	} \
} while (0)

#define con_rx_ready()	(con_rx_head != con_rx_tail)
uint8_t con_getc(void);		/* the next RX byte; call only when con_rx_ready() */
uint8_t con_tx_room(void);	/* free bytes in the TX ring */
void con_putc(uint8_t c);	/* waits while the TX ring is full */
void con_ring_init(void);

#endif
