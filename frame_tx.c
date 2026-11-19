/*
 * frame_tx.c - sending a frame to the NIC: uIP's from uip_buf
 * (tcpip_output), the control frames' from their own buffer (ctl_output:
 * STP, LACP, LLDP, net.md), so a control frame built between two steps
 * can't land in a response httpd still holds in uip_buf. Banked.
 */
#include <stdint.h>
#include <stdbool.h>
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "rtl837x_sfr.h"
#include "rtl837x_nic.h"
#include "uip/uip.h"

extern __xdata uint8_t uip_buf[UIP_CONF_BUFFER_ALLOC_SIZE];
extern __xdata uint8_t tx_seq;
extern __xdata uint16_t management_vlan;
extern __xdata uint8_t sfr_data[4];

__xdata bool frame_tagged;
/* The control frames' own buffer (STP, LACP, LLDP: net.md): built and sent
 * from here, so uip_buf belongs to uIP alone */
__xdata uint8_t ctl_buf[CTL_BUF_SIZE];
__xdata uint16_t ctl_len;
static __xdata uint8_t * __xdata tx_buf;	/* the frame frame_output() sends */
static __xdata uint16_t tx_len;

// Dot 1Q tag size is the size of tpid + tci
#define DOT_1Q_TAG_SIZE 4

struct q_frame {
	uint8_t tx_seq;
	uint8_t chksum_flags;	// 0x7 enables Checksums for frame header, L2 and L3
	uint8_t reserved_1 [2];
	uint16_t len; // Length is Little Endian
	uint8_t reserved_2 [2];
	struct uip_eth_addr dst;
	struct uip_eth_addr src;
	uint16_t tpid;
	uint16_t tci;
};

struct nonq_frame {
	uint8_t padding[DOT_1Q_TAG_SIZE];
	uint8_t tx_seq;
	uint8_t chksum_flags;	// 0x7 enables Checksums for frame header, L2 and L3
	uint8_t reserved_1 [2];
	uint16_t len; // Length is Little Endian
	uint8_t reserved_2 [2];
	struct uip_eth_addr dst;
	struct uip_eth_addr src;
};

// The output frame structure with initial frame descriptor including padding
#define FRAME ((__xdata struct nonq_frame *)tx_buf)

// The output frame structure with 802.1Q field and the padding moved before the buffer-start
#define FRAME_Q ((__xdata struct q_frame *)tx_buf)

// Ether-type of the output frame, which is the RTL tag on a CPU-tagged frame
#define FRAME_ETHERTYPE (*(__xdata uint16_t *)&tx_buf[RTL_FRAME_DESC_SIZE + 2 * sizeof(struct uip_eth_addr)])

/* The frame at tx_buf, tx_len long: the TX descriptor, the management VLAN
 * tag unless it is CPU-tagged, then to the NIC */
static void frame_output(void)
{
	// Add TX-TAG
	FRAME->tx_seq = tx_seq++;
	FRAME->chksum_flags = 0x07;    // Enable all checksums
	FRAME->reserved_1[0] = 0x00; FRAME->reserved_1[1] = 0x00;
	FRAME->len = tx_len;
	FRAME->reserved_2[0] = 0x00; FRAME->reserved_2[1] = 0x00;

	// For the management VLAN we insert an 802.1Q VLAN tag, but never into a
	// CPU-tagged frame, where the ASIC expects its tag right behind the addresses
	frame_tagged = false;
	if (management_vlan && FRAME_ETHERTYPE != HTONS(RTL_FRAME_TAG_ID)) {
		frame_tagged = true;
		// Shift the ethernet header before the HW type including the rtl_frame_desc to the beginning of the buffer
		// to allow space to insert the dot 1Q tag
		for (uint8_t i = 0; i < sizeof(struct q_frame) - DOT_1Q_TAG_SIZE; i++)
			tx_buf[i] = tx_buf[i + DOT_1Q_TAG_SIZE];
		FRAME_Q->len += DOT_1Q_TAG_SIZE;
		FRAME_Q->tpid = HTONS(0x8100);  // Change ether-type to Dot1Q
		FRAME_Q->tci = HTONS(management_vlan);
	}

	reg_read_m(RTL837X_REG_CPU_TX_CURR_PKT);
	uint16_t ring_ptr = ((uint16_t)sfr_data[2]) << 8;
	ring_ptr |= sfr_data[3];

	// Move data over from xmem buffer to ASIC side using DMA. A VLAN-tagged
	// frame starts at the buffer; an untagged frame retains four bytes of padding.
	if (frame_tagged)
		nic_tx_packet(tx_buf, FRAME_Q->len, ring_ptr);
	else
		nic_tx_packet(tx_buf + VLAN_TAG_SIZE, FRAME->len, ring_ptr);

	// New position of the ring-pointer on the NIC-side indicates number of bytes transmitted
	reg_read_m(RTL837X_REG_NIC_TX_CURR_PKT);

	// Do actual TX of data on ASIC side
	REG_SET(RTL837X_REG_NIC_TXCMD, 1);
}

/* uIP's frame, in uip_buf */
void tcpip_output(void) __banked
{
	tx_buf = uip_buf;
	tx_len = uip_len;
	frame_output();
}

/* A control frame, in ctl_buf */
void ctl_output(void) __banked
{
	tx_buf = ctl_buf;
	tx_len = ctl_len;
	frame_output();
}
