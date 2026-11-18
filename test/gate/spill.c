#include <stdint.h>
void step_helper(void);
/* A non-leaf function with 32-bit locals live across a call and a second
 * parameter: SDCC gives them fixed internal RAM (DSEG: a sloc and a PARM),
 * which the scheduler's rules forbid (doc/sched/memory.md). */
uint16_t spilling_step(uint16_t a, uint16_t b)
{
	uint32_t x = (uint32_t)a * 300;
	uint32_t y = (uint32_t)b * 7;
	step_helper();
	x += y;
	step_helper();
	return (uint16_t)(x >> 4) + (uint16_t)y + a;
}
