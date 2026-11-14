#ifndef UPDATE_SHIM_H
#define UPDATE_SHIM_H

#include <stdint.h>
#include "flash_dev.h"

/* The modelled flash the firmware's flash interface is bound to. */
extern flashdev_t g_flash;

/* Set non-zero to let the firmware's console output through to stdout. */
extern int shim_verbose;

/* Run check_and_flash_update_image() against g_flash.
 * Returns 1 if it reset the chip (an install armed, copied by boot_copy() and
 * finished), 0 if it returned normally (too small / no image / bad CRC) or a
 * modelled power cut stopped it (g_flash.powercut set). */
int update_run(void);

/* Run boot0.c's boot_resume() against g_flash, as the start of a boot: 1 if
 * it finished an install and reset, 0 if there was nothing to install or a
 * modelled power cut stopped it. */
int resume_run(void);

/* Run config_migrate() against g_flash. Returns its result, or -1 if a
 * modelled power cut stopped it part-way (g_flash.powercut set). */
int migrate_run(void);
/* The same for mdata_migrate(). */
int migrate_mdata_run(void);

/* crc16_host.c */
uint16_t crc16_host_step(uint16_t crc, uint8_t b);
uint16_t crc16_host_buf(const uint8_t *d, uint32_t n);

#endif
