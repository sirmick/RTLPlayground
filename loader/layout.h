#ifndef LOADER_LAYOUT_H
#define LOADER_LAYOUT_H

/* The loader's flash addresses (data area, slot, application ranges, upload,
 * stage) are part of the one flash layout, in the top-level layout.h. This
 * adds only what is internal to the loader. */
#include "../layout.h"

#define LDR_ALIAS_FLASH		0x3800
#define LDR_ALIAS_LEN		0x800
#define LDR_ALIAS_RAM		(LDR_ALIAS_FLASH - 2)
/* start_app() runs app_start() from a bank window onto LDR_ALIAS_FLASH. Bank
 * b shows the flash at (b-1)*CODE_BANK_SIZE + the window address, which only
 * wraps onto the loader on a chip smaller than the banks reach. On 4 MiB and
 * up the loader writes a copy of the alias where bank 0x3f shows it at 0x7800. */
#define LDR_ALIAS_REACH		(0x3eUL * CODE_BANK_SIZE + 0x10000)
#define LDR_ALIAS_HIGH		(0x3eUL * CODE_BANK_SIZE + 0x4000 + LDR_ALIAS_FLASH)
#define LDR_SLOT_BUF		0x4000

#define LDR_RECOVERY_BOOTS	3
#define LDR_CONFIRM_S		15

#endif
