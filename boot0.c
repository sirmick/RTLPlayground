/*
 * boot0.c - the first code to run after a reset, in sector 0 of the flash.
 *
 * A loaderless install (update_apply.c) verifies the staged image, arms the
 * progress record at UPDATE_PROGRESS and resets. This then copies the staged
 * image over the running one, highest sector first, marking each sector done
 * in the record, clears the staging area and the record, and resets into the
 * new image. A reset or power cut part way through just runs it again, and it
 * carries on where the marks say.
 *
 * It is self-contained in sector 0, which it copies last. It runs from
 * __sdcc_external_startup(), before the C start-up initialises any data and
 * before main(); it calls nothing outside this file, uses no initialised data
 * and drives the flash controller itself. So it works however sectors 1-3 of
 * the home bank and the code banks are torn between the old and the new
 * image. Only a cut while sector 0 itself is rewritten can still stop the
 * switch; the loader is the answer to that. tools/check_linked.py checks that
 * this stays in sector 0 and self-contained.
 *
 * Compiled into the HOME area (Makefile), after the interrupt vectors. On the
 * host test bench the flash primitives and the reset are the test's own.
 */
#include <stdint.h>
#include "layout.h"
#include "update_apply.h"
#ifndef RTLP_HOST_TEST
#include "rtl837x_sfr.h"
#endif

__xdata uint32_t b0_a;		/* flash address of the next operation */
__xdata uint8_t b0_d[4];	/* its data */
__xdata uint8_t b0_n;		/* bytes to program, 1-4 */
static __xdata uint8_t b0_s;	/* sector */
static __xdata uint16_t b0_o;	/* offset in the sector */

#ifdef RTLP_HOST_TEST
void b0_read(void);
void b0_erase(void);
void b0_program(void);
void b0_reset(void);
#else
#define CMD_PAGE_PROGRAM	0x02
#define CMD_READ_STATUS		0x05
#define CMD_WRITE_ENABLE	0x06
#define CMD_FREAD		0x0b
#define CMD_SECTOR_ERASE	0x20

/* The flash controller, as rtl837x_flash.c drives it (single I/O). */
static uint8_t b0_status(void)
{
	while (SFR_FLASH_EXEC_BUSY);
	SFR_FLASH_TCONF = 0x11;
	SFR_FLASH_CMD_R = CMD_READ_STATUS;
	SFR_FLASH_EXEC_GO = 1;
	while (SFR_FLASH_EXEC_BUSY);
	return SFR_FLASH_DATA0;
}

static void b0_addr(void)
{
	SFR_FLASH_ADDR16 = b0_a >> 16;
	SFR_FLASH_ADDR8 = b0_a >> 8;
	SFR_FLASH_ADDR0 = b0_a;
}

static void b0_write_enable(void)
{
	while (b0_status() & 0x01);
	SFR_FLASH_MODEB = 0;
	SFR_FLASH_DUMMYCYCLES = 0;
	SFR_FLASH_TCONF = 0x18;
	SFR_FLASH_CMD = CMD_WRITE_ENABLE;
	SFR_FLASH_EXEC_GO = 1;
	while (SFR_FLASH_EXEC_BUSY);
	while (!(b0_status() & 0x02));
}

/* 4 bytes at b0_a into b0_d */
static void b0_read(void)
{
	while (b0_status() & 0x01);
	SFR_FLASH_MODEB = 0;
	SFR_FLASH_CMD_R = CMD_FREAD;
	SFR_FLASH_DUMMYCYCLES = 8;
	b0_addr();
	SFR_FLASH_TCONF = 4;
	SFR_FLASH_EXEC_GO = 1;
	while (SFR_FLASH_EXEC_BUSY);
	b0_d[0] = SFR_FLASH_DATA0;
	b0_d[1] = SFR_FLASH_DATA8;
	b0_d[2] = SFR_FLASH_DATA16;
	b0_d[3] = SFR_FLASH_DATA24;
}

/* the 4 KiB sector at b0_a */
static void b0_erase(void)
{
	b0_write_enable();
	SFR_FLASH_TCONF = 8;
	SFR_FLASH_CMD = CMD_SECTOR_ERASE;
	b0_addr();
	SFR_FLASH_EXEC_GO = 1;
	while (b0_status() & 0x01);
}

/* b0_n bytes of b0_d at b0_a */
static void b0_program(void)
{
	b0_write_enable();
	SFR_FLASH_CMD = CMD_PAGE_PROGRAM;
	SFR_FLASH_TCONF = 8 | b0_n;
	b0_addr();
	SFR_FLASH_DATA0 = b0_d[0];
	SFR_FLASH_DATA8 = b0_d[1];
	SFR_FLASH_DATA16 = b0_d[2];
	SFR_FLASH_DATA24 = b0_d[3];
	SFR_FLASH_EXEC_GO = 1;
	while (b0_status() & 0x01);
}

#define b0_reset() CHIP_RESET_NOW()
#endif

void boot_resume(void)
{
	b0_a = UPDATE_PROGRESS;
	b0_read();
	if (b0_d[0] != 'U' || b0_d[1] != 'P' || b0_d[2] != 'D' || b0_d[3] != '1')
		return;
	b0_a = LDR_META;	/* the loader installs updates itself */
	b0_read();
	if (b0_d[0] == 'R' && b0_d[1] == 'T' && b0_d[2] == 'L' && b0_d[3] == 'B')
		return;

	b0_s = IMAGE_COPY_LEN / 0x1000;
	while (b0_s--) {
		b0_a = UPDATE_PROGRESS + 4 + b0_s;
		b0_read();
		if (!b0_d[0])
			continue;	/* copied before a reset */
		b0_a = (uint32_t)b0_s << 12;
		b0_erase();
		for (b0_o = 0; b0_o < 0x1000; b0_o += 4) {
			b0_a = FIRMWARE_UPLOAD_START + ((uint32_t)b0_s << 12) + b0_o;
			b0_read();
			if ((b0_d[0] & b0_d[1] & b0_d[2] & b0_d[3]) == 0xff)
				continue;	/* erased already */
			b0_a -= FIRMWARE_UPLOAD_START;
			b0_n = 4;
			b0_program();
		}
		b0_a = UPDATE_PROGRESS + 4 + b0_s;
		b0_d[0] = 0;
		b0_n = 1;
		b0_program();
	}
	/* The staging area, header first, so the install isn't found again;
	 * then the record. */
	for (b0_s = 0; b0_s < IMAGE_SIZE / 0x1000; b0_s++) {
		b0_a = FIRMWARE_UPLOAD_START + ((uint32_t)b0_s << 12);
		b0_erase();
	}
	b0_a = UPDATE_PROGRESS;
	b0_erase();
	b0_reset();
}

#ifndef RTLP_HOST_TEST
/* Called by the SDCC startup code before it copies XINIT to XISEG and clears
 * XSEG. Hardware reset starts us with MAP_FLASH_PAGE_TO_XDATA = 1, which maps
 * xdata 0x4000-0xbfff to the flash image (reads return flash, writes are
 * lost), so it must be 0 first. The flash controller is set up as
 * flash_init(0) does; main() sets it up again for normal running. */
unsigned char __sdcc_external_startup(void)
{
	MAP_FLASH_PAGE_TO_XDATA = 0;
	SFR_FLASH_CONFIG = 9;
	SFR_FLASH_CONF_RCMD = CMD_FREAD;
	SFR_FLASH_CONF_DIV = 8;
	boot_resume();
	return 0;
}
#endif
