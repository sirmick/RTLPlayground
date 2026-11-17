# RTL8372/RTL8373 8051 SFRs

The 8051 core is a DW8051 (DS80C320-style) with the chip's peripherals in
its SFR space. This page lists the SFRs the firmware uses, what they do and how
sure we are. The switch registers behind the register handshake are in
[registers.md](registers.md). The names are those of
[rtl837x_sfr.h](../rtl837x_sfr.h).

Each entry says how sure we are:

| Mark | Meaning |
|---|---|
| **confirmed** | A working feature depends on it, or it was measured on hardware |
| **sim** | The ovs-rtlpg simulator implements it this way, and both this firmware and stock firmware run correctly on that model |
| **trace** | Copied from stock firmware's SFR writes; the effect is not understood |
| **comment** | Only a code comment or another doc says so |
| **guess** | Inferred, e.g. from the values written |

The sim traces in this page come from a full boot of this firmware and of three
stock images: unmanaged SWTG024AS (RTL8372), managed KeepLink 9XHPML and
MokerLink POE-2G040210GSM (RTL8373).

## Contents

- [Memory](#memory)
- [Code banking](#code-banking)
- [Standard 8051 SFRs](#standard-8051-sfrs)
- [Interrupts](#interrupts)
- [Switch register handshake](#switch-register-handshake)
- [SerDes](#serdes)
- [SMI / MDIO](#smi--mdio)
- [Flash controller](#flash-controller)
- [NIC DMA](#nic-dma)
- [Unknown](#unknown)
- [Known problems](#known-problems)

---

## Memory

| Space | Range | What | |
|---|---|---|---|
| code | 0x0000-0x3fff | Common area, in code RAM. The build uses 0x0000-0x3ffd: `BANK1` is written over the last two bytes (see #442) | confirmed |
| code | 0x4000-0xffff | Window into the flash, selected by `PSBANK` | confirmed |
| xdata | 0x0000-0xbfff | RAM; the build uses `--xram-size 49151` | confirmed |
| xdata | 0x4000-0xbfff | While `MAP_FLASH_PAGE_TO_XDATA` is 1, reads return the flash image and writes are lost. Which flash address it shows is not known | confirmed, address open |
| data | 0x00-0xff | 256 bytes internal RAM; 0x20-0x2f bit-addressable | confirmed |

Software can load the code RAM itself: with `HW_CONF` (0x6040) bit 1 set and a
4 KiB chunk in bits 3:2, a write to switch register `0x80b0 + 4 × i` stores the
low byte of its data at `chunk × 0x1000 + i`. The loader in #852 starts an
application this way (`app_start`). (**sim**)

## Code banking

| SFR | Name | Fields | |
|---|---|---|---|
| 0x96 | `PSBANK` | The flash bank in code 0x4000-0xffff. Bank n ≥ 1 maps code address a to flash `(n − 1) × 0xc000 + a`, so bank 1 is flash 0x04000-0x0ffff, bank 2 0x10000-0x1bfff and so on. 6 bits; bit 6 selects bank 1, and 0 wraps to the top of the flash | 1-5 confirmed, rest sim |
| 0xbb | `SFR_BANK_RET` | The stock firmware's bank trampolines keep the caller's bank only here (`mov 0xbb,0x96` … `mov 0x96,0xbb`), never on the stack. The managed images nest banked calls 6 deep and only run if it behaves as a stack: a write pushes, a read pops. This firmware's `crtbank.asm` pushes `PSBANK` on the CPU stack instead and never uses 0xbb | sim (stack depth unknown) |
| 0x97 | `MAP_FLASH_PAGE_TO_XDATA` | bit 0: 1 (reset) maps the flash into xdata 0x4000-0xbfff, see [Memory](#memory). `__sdcc_external_startup()` clears it before the C startup code fills xdata. Stock writes 0 early too | confirmed |

## Standard 8051 SFRs

Only the ones with chip-specific use:

| SFR | Name | Use | |
|---|---|---|---|
| 0x87 | `PCON` | bit 0 IDLE: the main loop idles until the next interrupt (`idle()`); unmanaged stock idles too. bit 7 `SMOD0`, doubles the UART baud rate | confirmed |
| 0x8e | `CKCON` | bit 4 Timer 1 at clk/4 (UART baud). bit 5 cleared for Timer 2 at clk/12. Bits 7:6 would set the watchdog interval | confirmed |
| 0x89, 0x8d | `TMOD`, `TH1` | Timer 1 mode 2 (8-bit auto-reload) as the UART baud generator | confirmed |
| 0x98, 0x99 | `SCON`, `SBUF` | UART 0, mode 1 8N1, 115200 | confirmed |
| 0xc8, 0xca-0xcd | `T2CON`, `RCAP2`, `TL2/TH2` | Timer 2, 16-bit auto-reload: the system tick (`isr_timer2`) | confirmed |
| 0x92 | `MPAGE` | Upper address byte for `movx @Ri` (SDCC's `_XPAGE`), instead of P2 | confirmed |
| 0xd8, 0xc7 | `WDCON`, `TA` | The DW8051 watchdog. Neither this firmware nor stock firmware touches them: stock has no watchdog | sim (stock scanned for every access) |

P0 (0x80) and P2 (0xa0) are not ports here: 0x80 is the flash controller and
0xa0 the register handshake.

## Interrupts

| Vector | Source | Handler | |
|---|---|---|---|
| 0x03 INT0 (`IT0` = 1, falling edge) | Link change: `ISR_INT_PORT_LINK_CHG` 0x5f88 with `IMR_INT_PORT_LINK_STS_CHG` 0x5f34 | `isr_ext0` masks EX0 and latches `link_irq`; `idle()` clears 0x5f88 and re-enables EX0 | confirmed |
| 0x0b Timer 0 | — | empty | — |
| 0x13 INT1 | NIC RX: `NIC_INT_STS` bit 1 with `NIC_INT_MSK` 0x7858 | `isr_ext1` masks EX1 and latches `rx_irq`. The main loop re-enables EX1 only after it has emptied the ring, so a lost `rx_irq` leaves receive dead until a reboot (#818) | confirmed |
| 0x23 serial | UART RX/TX | `isr_serial` | confirmed |
| 0x2b Timer 2 | System tick | `isr_timer2` | confirmed |
| 0x43 EX2 (`EIE` bit 0, flag `EXIF` bit 4) | Unknown | Enabled; the handler only clears the flag. Stock installs a handler too | trace |
| 0x4b EX3 (`EIE` bit 1, flag `EXIF` bit 5, `EIP` bit 1 high priority) | Unknown | As EX2 | trace |

`EIE` 0xe8, `EIP` 0xf8 and `EXIF` 0x91 (flags IE2-IE5 in bits 4-7, cleared by
software) are as on the DS80C320. Vectors 0x53 and 0x5b (EX4, EX5) are not
installed by either firmware.

## Switch register handshake

Reaches the 16-bit-addressed, 32-bit switch registers
([registers.md](registers.md)), and the SerDes and MDIO below.

| SFR | Name | Fields | |
|---|---|---|---|
| 0xa0 | `SFR_EXEC_GO` | Write a command: 1 register read, 3 register write, 5 SerDes read, 7 SerDes write, 9 MDIO read, 11 MDIO write | confirmed |
| 0xa1 | `SFR_EXEC_STATUS` | Polled until 0 after a command | confirmed |
| 0xa2, 0xa3 | `SFR_REG_ADDRH/L` | Register address | confirmed |
| 0xa4-0xa7 | `SFR_DATA_24/16/8/0` | Data, 0xa4 = bits 31:24 … 0xa7 = bits 7:0. 16-bit SerDes and MDIO data are in 0xa6/0xa7 | confirmed |

The 32-bit `__sfr32` at 0xa4 is not used because of SDCC bug 4070 (wrong
address for the 0xa4 byte), so the header splits it into two `__sfr16`.

## SerDes

The SoC's own SerDes 0 and 1, through the handshake with commands 5 and 7.

| SFR | Name | Fields | |
|---|---|---|---|
| 0x93 | `SFR_93` | SerDes register | confirmed |
| 0x94 | `SFR_94` | `page << 1 \| sds`: bit 0 SerDes 0/1, bits 7:1 page | confirmed |

Data in 0xa6/0xa7. `sds_read()` and `sds_write_v()` in `rtlplayground.c`.

## SMI / MDIO

PHY access, clause 45, through the handshake with commands 9 and 11.

| SFR | Name | Fields | |
|---|---|---|---|
| 0xc2, 0xc3 | `SFR_SMI_REG_H/L` | 16-bit register | confirmed |
| 0xc4 | `SFR_SMI_DEV` | 7:3 MMD (device). bit 0 is bit 8 of the PHY mask for writes. Bit 1 is always set (`phy_read/phy_write` OR in 2, and every stock access has it); its function is unknown. The code's comment says bit 2 "can also be set for some option" | 7:3, 0 confirmed, 1 trace |
| 0xc5 | `SFR_SMI_PHYMASK` | Writes go to every PHY in the mask (bits 7:0, bit 8 in 0xc4) | confirmed |

A read takes the PHY address in 0xa5 (`SFR_SMI_PHY`) and returns the data in
0xa6/0xa7.

## Flash controller

The SPI-NOR flash, both as the code/xdata window and for commands.

| SFR | Name | Fields | |
|---|---|---|---|
| 0x80 | `SFR_FLASH_EXEC` | bit 2 GO, bit 0 BUSY | confirmed |
| 0x9e | `SFR_FLASH_TCONF` | Transfer: 2:0 data bytes (0-4), bit 3 write (command from 0xb2, else 0xb1), bit 4 no address phase, bit 6 unknown (set on page-program chunks of 4) | 2:0, 3 confirmed; 4 guess |
| 0xb1 | `SFR_FLASH_CMD_R` | Command for a read transfer (status 0x05, JEDEC 0x9f, fast read 0x0b, dual read 0xbb, ...) | confirmed |
| 0xb2 | `SFR_FLASH_CMD` | Command for a write transfer (write enable, page program, sector erase, ...) | confirmed |
| 0xa9-0xab | `SFR_FLASH_ADDR0/8/16` | 24-bit address | confirmed |
| 0xac-0xaf | `SFR_FLASH_DATA0/8/16/24` | Data, first byte in 0xac | confirmed |
| 0x9a | `SFR_FLASH_MODEB` | Written 0x18 for dual-IO read transfers, 0 for single; the bits are not understood | values confirmed |
| 0x9d | `SFR_FLASH_DUMMYCYCLES` | Dummy cycles of a read transfer: 4 dual, 8 single. 24 "doesn't seem to work" (`rtl837x_flash.c`) | confirmed for 4, 8 |
| 0x9b | `SFR_FLASH_CONF_DIV` | Divider for memory-mapped reads (code fetches, `movc`): 4 with dual-IO, 8 single. `flash_init()`: the SPI speed "seems to be directly linked to the CPU frequency" | comment |
| 0x9c | `SFR_FLASH_CONF_RCMD` | Read command for memory-mapped reads: 0xbb dual-IO, 0x0b fast read | confirmed |
| 0xbc | `SFR_FLASH_CONFIG` | Written 9 by every firmware. "There may be a chip-select in here" | trace |

Sequences are in `rtl837x_flash.c`. The transfers this firmware uses, by
`TCONF`: 0x11 read status, 0x13 JEDEC ID, 0x18 write enable, 0x19 write
status, 0x04 a 4-byte read after an address phase (unique ID, security
registers), 0x08 sector erase, 0x08 + n and 0x4c page program.

## NIC DMA

Copies between xdata and the NIC's ring on the switch side, see
[CpuPort.md](CpuPort.md).

| SFR | Name | Fields | |
|---|---|---|---|
| 0xb3, 0xb4 | `SFR_NIC_DATA_L/H` | xdata address, little endian | confirmed |
| 0xb5, 0xb6 | `SFR_NIC_RING_L/H` | Ring offset in bytes, little endian. Bit 15 selects the TX ring | confirmed |
| 0xb7 | `SFR_NIC_CTRL` | Write the length in 8-byte units to start the copy | confirmed |

## Unknown

| SFR | Name | Use | |
|---|---|---|---|
| 0xb9 | `SFR_b9` | Written 0x00 once in `main()` ("Set in managed mode"), as the managed stock images do. Unmanaged stock never writes it | trace |
| 0xba | `SFR_ba` | Written 0x80 once, as 0xb9 | trace |

## Known problems

- `rtl837x_sfr.h` labels 0x93/0x94 "Clause 22 PHY access ???". They are the
  SerDes register and page/SerDes select (`sds_read()`, `sds_write_v()`).
- [hardware.md](hardware.md) says the RX interrupt is not used and RX is
  polled. RX runs on INT1.
