/*
 * update_apply.c - apply a staged firmware image at boot. See update_apply.h.
 *
 * Moved verbatim out of rtlplayground.c so it can be exercised on the host test
 * bench (test/test_update_apply.c) against a modelled flash device, without a
 * programmer or a switch. No behaviour change.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_flash.h"
#include "rtl837x_regs.h"
#include "update_apply.h"
#include "loader/layout.h"

extern __xdata uint32_t flash_size;
extern __xdata uint16_t crc_value;
extern __xdata struct flash_region_t flash_region;
extern __xdata uint8_t flash_buf[FLASH_BUF_SIZE];
void crc16_bank1(__xdata uint8_t *v) __naked;

static void staging_erase(void)
{
	__xdata uint32_t dest = FIRMWARE_UPLOAD_START;
	__xdata uint16_t i;

	for (i = 0; i < IMAGE_SIZE/0x1000; i++) {
		flash_region.addr = dest;
		flash_sector_erase();
		dest += 0x1000;
		if (i%4 == 0) write_char('.');
	}
}

void check_and_flash_update_image(void)
{
	flash_read_jedecid(); // This initializes also __xdata flash_size variable

	print_string(get_flash_size_str()); print_string(" flash size detected.\n");
	if (flash_size < FLASH_NEEDED_FOR_UPDATE) {
		print_string("Flash too small for updating; skipping update check\n");
		return;
	}

	flash_region.addr = LDR_META;
	flash_region.len = 4;
	flash_read_bulk(flash_buf);
	if (flash_buf[0] == 'R' && flash_buf[1] == 'T' && flash_buf[2] == 'L' && flash_buf[3] == 'B') {
		print_string("Updates are installed by the loader\n");
		return;
	}

	print_string("Checking for update image in flash... ");
	// Check if an update image is in flash
	flash_region.addr = FIRMWARE_UPLOAD_START;
	flash_region.len = 0x100;
	flash_read_bulk(flash_buf);
	if (flash_buf[0] == 0x00 && flash_buf[1] == 0x40)
	{
		// Yes, flash the new image to the start of flash and reset
		__xdata uint32_t source = FIRMWARE_UPLOAD_START;
		__xdata uint16_t i = 0;
		__xdata uint16_t j = 0;
		__xdata uint8_t * __xdata bptr;
		print_string("found update image!\nChecking integrity");
		flash_init(0);
		set_sys_led_state(SYS_LED_FAST);
		crc_value = 0x0000;
		for (i = 0; i < IMAGE_SIZE/FLASH_BUF_SIZE; i++) {
			flash_region.addr = source;
			flash_region.len = FLASH_BUF_SIZE;
			flash_read_bulk(flash_buf);
			bptr = flash_buf;
			for (j = 0; j < FLASH_BUF_SIZE; j++) {
				crc16_bank1(bptr++);
			}
			source += FLASH_BUF_SIZE;
			if (i%16 == 0) write_char('.');
		}
		if (crc_value == 0xb001) {
			// Arm the progress record, then copy the image (but not a saved
			// config that sits inside it).
			// Arm the progress record and reset: the copy is done by
			// boot_resume() (boot0.c), from sector 0, before main().
			print_string("Checksum OK.\nInstalling at the next boot, resetting");
			flash_region.addr = UPDATE_PROGRESS;
			flash_sector_erase();
			flash_buf[0] = 'U'; flash_buf[1] = 'P'; flash_buf[2] = 'D'; flash_buf[3] = '1';
			flash_region.addr = UPDATE_PROGRESS;
			flash_region.len = 4;
			flash_write_bytes(flash_buf);
			delay(200);
			reset_chip();
		}
		print_string("Checksum incorrect, please upload the image again\n");
		print_string("Erasing bad uploaded flash image");
		staging_erase();
		write_char('\n');
	}
	else
	{
		print_string("no update image found.\n");
	}
}

__xdata uint32_t flash_crc16_from, flash_crc16_to;

uint16_t flash_crc16_range(void)
{
	__xdata uint32_t a = flash_crc16_from;
	__xdata uint16_t i, n;

	crc_value = 0;
	while (a < flash_crc16_to) {
		n = flash_crc16_to - a > FLASH_BUF_SIZE ? FLASH_BUF_SIZE : flash_crc16_to - a;
		flash_region.addr = a;
		flash_region.len = n;
		flash_read_bulk(flash_buf);
		for (i = 0; i < n; i++)
			crc16_bank1(flash_buf + i);
		a += n;
	}
	return crc_value;
}

#if CONFIG_START != LEGACY_CONFIG_START
/* Is the old config sector saved-config text: printable lines starting with
 * a letter and ending in NUL or erased flash? Staging leftovers (a gzipped web
 * UI, code) at the old address are binary, so they are not mistaken for one.
 * (No parameters and xdata locals: internal RAM is full.) */
static uint8_t legacy_is_config_text(void)
{
	__xdata uint16_t off, i;
	__xdata uint8_t c;

	for (off = 0; off < CONFIG_LEN; off += FLASH_BUF_SIZE) {
		flash_region.addr = LEGACY_CONFIG_START + off;
		flash_region.len = FLASH_BUF_SIZE;
		flash_read_bulk(flash_buf);
		for (i = 0; i < FLASH_BUF_SIZE; i++) {
			c = flash_buf[i];
			if (!off && !i && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
				return 0;
			if (c == 0x00 || c == 0xff)
				return 1;
			if ((c < ' ' || c > '~') && c != '\n' && c != '\r' && c != '\t')
				return 0;
		}
	}
	return 0;		/* no end within the sector */
}

uint8_t config_migrate(void)
{
	__xdata uint32_t off;
	__xdata uint16_t c;	/* one CRC at a time: flash_crc16() passes its range in globals */

	/* The move erases the old sector last, so config text still there means
	 * the move isn't finished (or a standard firmware ran again since): the
	 * old sector is the truth. Copy it over, unless that already happened. */
	if (!legacy_is_config_text())
		return CONFIG_STAYED;
	c = flash_crc16(LEGACY_CONFIG_START, LEGACY_CONFIG_START + CONFIG_LEN);
	if (c != flash_crc16(CONFIG_START, CONFIG_START + CONFIG_LEN)) {
		print_string("Moving the saved config to its new place... ");
		flash_region.addr = CONFIG_START;
		flash_sector_erase();
		for (off = 0; off < CONFIG_LEN; off += FLASH_BUF_SIZE) {
			flash_region.addr = LEGACY_CONFIG_START + off;
			flash_region.len = FLASH_BUF_SIZE;
			flash_read_bulk(flash_buf);
			flash_region.addr = CONFIG_START + off;
			flash_region.len = FLASH_BUF_SIZE;
			flash_write_bytes(flash_buf);
		}
		c = flash_crc16(LEGACY_CONFIG_START, LEGACY_CONFIG_START + CONFIG_LEN);
		if (c != flash_crc16(CONFIG_START, CONFIG_START + CONFIG_LEN)) {
			flash_region.addr = CONFIG_START;	/* blank again: retried next boot */
			flash_sector_erase();
			print_string("failed\n");
			return CONFIG_MOVE_FAILED;
		}
		print_string("done\n");
	}
	flash_region.addr = LEGACY_CONFIG_START;
	flash_sector_erase();
	return CONFIG_MOVED;
}
#else
uint8_t config_migrate(void)
{
	return CONFIG_STAYED;
}
#endif

#ifdef LAYOUT_COMPACT
static uint8_t mdata_chip_ok(void)
{
	if (!flash_size)
		flash_read_jedecid();
	return flash_size >= MDATA_START + MDATA_LEN;
}

__xdata uint32_t mdata_at;

uint32_t mdata_len_at(void)
{
	__xdata uint32_t a = mdata_at;
	__xdata uint32_t len;

	if (!mdata_chip_ok())
		return 0;
	flash_region.addr = a;
	flash_region.len = MDATA_HDR_LEN;
	flash_read_bulk(flash_buf);
	if (flash_buf[0] != 'M' || flash_buf[1] != 'D' || flash_buf[2] != 'T' || flash_buf[3] != '1')
		return 0;
	len = flash_buf[4] | ((uint32_t)flash_buf[5] << 8) | ((uint32_t)flash_buf[6] << 16) | ((uint32_t)flash_buf[7] << 24);
	if (len > MDATA_LEN - MDATA_HDR_LEN - 2)
		return 0;
	len += MDATA_HDR_LEN + 2;
	return flash_crc16(a, a + len) == 0xb001 ? len : 0;
}

uint8_t mdata_migrate(void)
{
	__xdata uint32_t len, off;
	__xdata uint16_t c;	/* one CRC at a time: flash_crc16() passes its range in globals */
	__xdata uint8_t same = 0;

	len = mdata_len(MDATA_XFER_START);
	if (!len)
		return CONFIG_STAYED;
	if (mdata_len(MDATA_START) == len) {
		c = flash_crc16(MDATA_START, MDATA_START + len);
		same = c == flash_crc16(MDATA_XFER_START, MDATA_XFER_START + len);
	}
	if (!same) {
		print_string("Moving the machine data to its own region... ");
		for (off = 0; off < len; off += 0x1000) {
			flash_region.addr = MDATA_START + off;
			flash_sector_erase();
		}
		for (off = 0; off < len; off += FLASH_BUF_SIZE) {
			flash_region.addr = MDATA_XFER_START + off;
			flash_region.len = FLASH_BUF_SIZE;
			flash_read_bulk(flash_buf);
			flash_region.addr = MDATA_START + off;
			flash_region.len = FLASH_BUF_SIZE;
			flash_write_bytes(flash_buf);
		}
		if (mdata_len(MDATA_START) != len) {
			print_string("failed\n");
			return CONFIG_MOVE_FAILED;	/* transfer copy kept: retried next boot */
		}
		print_string("done\n");
	}
	flash_region.addr = MDATA_XFER_START;	/* its header gone: done, for good */
	flash_sector_erase();
	return CONFIG_MOVED;
}
#else
__xdata uint32_t mdata_at;

uint32_t mdata_len_at(void)
{
	return 0;
}

uint8_t mdata_migrate(void)
{
	return CONFIG_STAYED;
}
#endif
