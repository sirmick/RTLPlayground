#ifndef _CMD_PARSER_H_
#define _CMD_PARSER_H_

#include <stdint.h>

#include "rtl837x_common.h"

extern __xdata uint8_t cmd_buffer[CMD_BUF_SIZE];
extern __xdata uint8_t err_status;

/* Health instrumentation, built with HEALTH=1. Without it the hooks below
 * compile to nothing and the "health" command does not exist. */
#ifdef HEALTH
/* Phase ids, in main-loop order */
#define HEALTH_PH_LINK	0
#define HEALTH_PH_SFP	1
#define HEALTH_PH_RX	2
#define HEALTH_PH_TX	3
#define HEALTH_PH_STP	4
#define HEALTH_PH_CMD	5
#define HEALTH_PH_LLDP	6
#define HEALTH_PHASES	(HEALTH_PH_LLDP + 1)	/* last id + 1: sizes the arrays in cmd_parser.c */

extern __xdata uint16_t health_rx_frames;
#define health_rx_frame()	health_rx_frames++

void health_loop_start(void) __banked;	/* once per idle() pass, resets the phase baseline */
void health_phase(uint8_t id) __banked;	/* after each phase; accounts ticks since last checkpoint */
void health_stack_paint(void) __banked;	/* paint the unused stack area once at boot */
void health_show(void) __banked;	/* "health" CLI dump */
#else
#define health_rx_frame()
#define health_loop_start()
#define health_phase(id)
#define health_stack_paint()
#endif

void cmd_tokenize(void) __banked;
void cmd_parser(void) __banked;
void execute_config(void) __banked;
void execute_commands(__xdata uint8_t *p) __banked;

/* Dumps as jobs (E1): cli_job set by the command, a line per cli_job_more() */
#define CLI_JOB_HISTORY	1
#define CLI_JOB_L2	2
#define CLI_JOB_HEALTH	3
#define CLI_JOB_STAT	4
#define CLI_JOB_LACP	5
#define CLI_JOB_STPFDB	6
extern __xdata uint8_t cli_job;
void cli_job_more(void) __banked;	/* the job's next line; clears cli_job after its last */
void cmd_finish(void) __banked;		/* the job to its end, at once */
void exec_begin(__xdata uint8_t *p) __banked;	/* several commands, from p */
uint8_t exec_line(void) __banked;	/* the next one; 0 when done (err_status) */
uint8_t port_l2_line(void) __banked;	/* rtl837x_port.c: the l2 job */
uint8_t port_stats_line(void) __banked;	/* rtl837x_port.c: the stat job */
uint8_t lacp_show_line(void) __banked;	/* rtl837x_lacp.c: the lacp show job */
uint8_t stp_fdb_line(void) __banked;	/* rtl837x_stp.c: stp off's BPDU entries, a VLAN walk */
void print_ip(__xdata uint8_t *ptr) __banked;
void print_mac(__xdata uint8_t *ptr) __banked;
void print_sw_version(void) __banked;
void clear_command_history(void) __banked;

#endif
