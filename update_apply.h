#ifndef _UPDATE_APPLY_H_
#define _UPDATE_APPLY_H_

#include <stdint.h>

/*
 * At boot, check for an image staged in flash by the web upload and, if one is
 * present and its CRC is good, copy it over the running image and reset.
 *
 * Talks to the flash only through the flash interface (rtl837x_flash.h), so it
 * builds and runs on the host test bench against a modelled flash device.
 * Must stay resident (no codeseg pragma): it erases and writes the code banks,
 * so it cannot run from one.
 */
void check_and_flash_update_image(void);

/* CRC-16 (the firmware's, as crc16_bank1) of the flash range [a, end). An
 * image with its CRC appended comes to 0xb001. Resident; uses flash_buf.
 * The range is passed in xdata (the macro): internal RAM is full, and 32-bit
 * parameters would take it. */
extern __xdata uint32_t flash_crc16_from, flash_crc16_to;
uint16_t flash_crc16_range(void);
#define flash_crc16(a, end) (flash_crc16_from = (a), flash_crc16_to = (end), flash_crc16_range())

/*
 * Move a saved config from the standard layout's sector (LEGACY_CONFIG_START)
 * to CONFIG_START, once, at boot, before the config is read. Acts only while
 * the old sector holds config text, and erases it last - so a factory reset
 * later can't bring it back, and an unfinished move (power cut) is simply
 * redone at the next boot. Returns CONFIG_MOVED, CONFIG_STAYED (nothing to
 * do) or CONFIG_MOVE_FAILED (the copy didn't read back: the old config is
 * kept, the new sector left blank; boot on the default for now).
 */
#define CONFIG_STAYED      0
#define CONFIG_MOVED       1
#define CONFIG_MOVE_FAILED 2
uint8_t config_migrate(void);

/* Machine data (layout.h, MDATA_*). mdata_len(a): the total length (header,
 * payload, CRC) of a valid region at a, else 0. mdata_migrate(): at boot,
 * before the machine init reads it, move a transfer copy left by a bridge
 * image to MDATA_START, once (same rules as config_migrate). Both do nothing
 * on a chip too small to have the region. Returns as config_migrate. */
extern __xdata uint32_t mdata_at;
uint32_t mdata_len_at(void);
#define mdata_len(a) (mdata_at = (a), mdata_len_at())
uint8_t mdata_migrate(void);

#endif
