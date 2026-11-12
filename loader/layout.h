#ifndef LOADER_LAYOUT_H
#define LOADER_LAYOUT_H

/* The loader's flash addresses (data area, slot, application ranges, upload,
 * stage) are part of the one flash layout, in the top-level layout.h. This
 * adds only what is internal to the loader. */
#include "../layout.h"

#define LDR_ALIAS_FLASH		0x3800
#define LDR_ALIAS_LEN		0x800
#define LDR_ALIAS_RAM		(LDR_ALIAS_FLASH - 2)
#define LDR_SLOT_BUF		0x4000

#define LDR_RECOVERY_BOOTS	3
#define LDR_CONFIRM_S		15

#endif
