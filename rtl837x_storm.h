#ifndef _RTL837X_STORM_H_
#define _RTL837X_STORM_H_

#include <stdint.h>

#define STORM_TYPES	4
#define STORM_METER_BASE	24
#define STORM_METER(port, type)	(STORM_METER_BASE + (((port) << 2) | (type)))


#endif
