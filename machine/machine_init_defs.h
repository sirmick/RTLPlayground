#ifndef _MACHINE_INIT_DEFS_H_
#define _MACHINE_INIT_DEFS_H_


#include "machine_defs.h"

// Board init code (machine_custom_init(), called __banked). In bank 3, which has
// the most room: bank 2 overflowed on the POE-2G040210GSM, whose init is large.

#pragma codeseg BANK3
#pragma constseg BANK3

#endif
