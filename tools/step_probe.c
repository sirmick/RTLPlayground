/*
 * step_probe.c - sched.h's STEP_SLEEP/STEP_WAIT/STEP_ARG with the states
 * SDCC 4.6.0 mishandles: `return (f(), K);` drops f() when K is a literal or
 * an explicit enum (STEP_STAY, STEP_END). sched.mk compiles this with the
 * build's flags and tools/step_probe.py fails the build if a call or the
 * store is missing from a function below. Not linked.
 */
#include <stdint.h>
#include "sched.h"

uint8_t probe_sleep_stay(void)	{ return STEP_SLEEP(STEP_STAY, 1000); }	/* _task_sleep */
uint8_t probe_sleep_end(void)	{ return STEP_SLEEP(STEP_END, 1000); }	/* _task_sleep */
uint8_t probe_wait_stay(void)	{ return STEP_WAIT(STEP_STAY, 1, 1000); }	/* _task_mask _task_sleep */
uint8_t probe_arg_stay(void)	{ return STEP_ARG(STEP_STAY, 0x1234); }	/* #0x34 */
