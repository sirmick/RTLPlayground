/*
 * config_migrate.c - the one-time move of the saved config from the standard
 * layout's sector, at boot. See update_apply.h. Banked: it runs only from
 * execute_config, and only the update itself has to stay resident.
 */
#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_flash.h"
#include "update_apply.h"
#include "loader/layout.h"

extern __xdata struct flash_region_t flash_region;
extern __xdata uint8_t flash_buf[FLASH_BUF_SIZE];

#if CONFIG_START != LEGACY_CONFIG_START
/* Is the old config sector a saved config: binary records ("\0RC1"), or
 * text, printable lines starting with a letter and ending in NUL or erased
 * flash? Staging leftovers (a gzipped web UI, code) at the old address are
 * neither, so they are not mistaken for one.
 * (No parameters and xdata locals: internal RAM is full.) */
static uint8_t legacy_is_config_text(void)
{
	__xdata uint16_t off, i;
	__xdata uint8_t c;

	for (off = 0; off < CONFIG_LEN; off += FLASH_BUF_SIZE) {
		flash_region.addr = LEGACY_CONFIG_START + off;
		flash_region.len = FLASH_BUF_SIZE;
		flash_read_bulk(flash_buf);
		/* or binary records (om.c, DESIGN.md 15.3): "\0RC1" */
		if (!off && !flash_buf[0] && flash_buf[1] == 'R' && flash_buf[2] == 'C' && flash_buf[3] == '1')
			return 1;
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

uint8_t config_migrate(void) __banked
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
uint8_t config_migrate(void) __banked
{
	return CONFIG_STAYED;
}
#endif
