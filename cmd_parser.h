#ifndef _CMD_PARSER_H_
#define _CMD_PARSER_H_

#include <stdint.h>

#include "rtl837x_common.h"

extern __xdata uint8_t cmd_buffer[CMD_BUF_SIZE];
extern __xdata uint8_t err_status;

/* the stack painted at boot, for state.health.stack_headroom (om_health.c) */
void health_stack_paint(void) __banked;

void cmd_tokenize(void) __banked;
void cmd_parser(void) __banked;
void execute_config(void) __banked;

/* Dumps as jobs (E1): cli_job set by the command, a line per cli_job_more() */
#define CLI_JOB_OM	4	/* om.c: get, diff */
#define CLI_JOB_OMAPPLY	7	/* om.c: apply, a port a step */
/* A job with side effects runs to its end even if nobody reads it */
#define CLI_JOB_KEEPS(j)	((j) >= CLI_JOB_OMAPPLY)
extern __xdata uint8_t cli_job;
/* set by a job's line: its next one at least CLI_JOB_SLEEP_MS later */
#define CLI_JOB_SLEEP_MS	10
extern __xdata uint8_t cli_job_sleep;
/* set by a job's line: no more of its lines in this step (a web step's several) */
extern __xdata uint8_t cli_job_one;
void cli_job_more(void) __banked;	/* the job's next line; clears cli_job after its last */
void exec_begin(__xdata uint8_t *p) __banked;	/* several commands, from p */
uint8_t exec_line(void) __banked;	/* the next one; 0 when done (err_status) */
void print_ip(__xdata uint8_t *ptr) __banked;
void print_sw_version(void) __banked;
void clear_command_history(void) __banked;

#endif
