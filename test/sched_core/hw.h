/* The 8051 SFRs and bits sched.c, con_ring.c and crash.c touch; test_sched.c
 * holds them. Force-included for test_sched only (test_loader.c has its own). */
#include <stdbool.h>
#include <stdint.h>
extern volatile bool EA, ES, TI, RI;
extern volatile uint8_t SP, SBUF;
/* PCON through a call: sched_idle()'s `PCON |= 1` is the sleep, during which
 * the test can let time pass (test_sched.c's host_pcon()) */
volatile uint8_t *host_pcon(void);
#define PCON (*host_pcon())
/* the host's reset (a longjmp) and idata, behind crash.c's CRASH_RESET/CRASH_IDATA */
void host_reset(void);
extern uint8_t host_idata[256];
