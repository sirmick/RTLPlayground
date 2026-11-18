#ifndef _RTL837X_RLDP_H_
#define _RTL837X_RLDP_H_

#include <stdint.h>

void rldp_set(uint8_t on) __banked;
void rldp_tick(void) __banked;
void rldp_show(void) __banked;

extern __xdata uint8_t rldp_enabled;
extern __xdata uint16_t rldp_looped, rldp_blocked;

#endif
