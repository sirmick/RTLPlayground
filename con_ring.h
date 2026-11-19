/*
 * con_ring.h - the console UART rings (doc/sched/console.md).
 *
 * RX, 256 B: the serial ISR stores each byte, or counts an overrun when the
 * ring is full; the con task drains it. TX, 2 KiB (decision 21): con_putc()
 * queues a byte and starts the transmitter if it is idle, busy-waiting while
 * the ring is full; the ISR sends the next byte on each TI. Its indexes are
 * 16 bits, so neither side may see the other's half-written: the head is
 * written by con_putc only, with ES off, and read by the ISR; the tail is
 * written by the ISR only, and read by con_tx_room() with ES off.
 *
 * The ISR side is macros, expanded in isr_serial: a call from an ISR makes
 * SDCC save every register on each entry.
 */
#ifndef CON_RING_H
#define CON_RING_H

#include <stdint.h>

#define CON_RX_SIZE	256	/* 8-bit indexes wrap by themselves: ~22 ms of input at
				 * 115200, more than a 20 ms step (decision 19) */
#define CON_RX_MASK	(CON_RX_SIZE - 1)
#define CON_TX_SIZE	2048	/* a power of 2: most commands print without waiting */
#define CON_TX_MASK	(CON_TX_SIZE - 1)
#define CON_TX_LINE	100	/* HW_CON_TX: room for a line */

extern __xdata uint8_t con_rx_buf[CON_RX_SIZE];
extern volatile __xdata uint8_t con_rx_head;	/* written by the ISR */
extern __xdata uint8_t con_rx_tail;		/* written by the reader */
extern volatile __xdata uint16_t con_rx_overruns;
extern __xdata uint8_t con_tx_buf[CON_TX_SIZE];
extern __xdata uint16_t con_tx_head;		/* written by con_putc, ES off */
extern volatile __xdata uint16_t con_tx_tail;	/* written by the ISR */
extern volatile __xdata uint8_t con_tx_busy;	/* the transmitter is sending */

/* In isr_serial, for a received byte c (an 8-bit local or SBUF). */
#define CON_ISR_RX(c) do { \
	if ((uint8_t)(con_rx_head + 1) == con_rx_tail) { \
		con_rx_overruns++; \
	} else { \
		con_rx_buf[con_rx_head] = (c); \
		con_rx_head++; \
	} \
} while (0)

/* In isr_serial on TI, after clearing TI. */
#define CON_ISR_TX() do { \
	if (con_tx_tail != con_tx_head) { \
		SBUF = con_tx_buf[con_tx_tail]; \
		con_tx_tail = (con_tx_tail + 1) & CON_TX_MASK; \
	} else { \
		con_tx_busy = 0; \
	} \
} while (0)

#define con_rx_ready()	(con_rx_head != con_rx_tail)
uint8_t con_getc(void);		/* the next RX byte; call only when con_rx_ready() */
uint16_t con_tx_room(void);	/* free bytes in the TX ring */
void con_putc(uint8_t c);	/* waits while the TX ring is full */
void con_drain(void);		/* waits until the TX ring is out: before a reset */
void con_ring_init(void);

#endif
