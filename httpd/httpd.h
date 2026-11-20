#ifndef __HTTPD_H__
#define __HTTPD_H__

/* Since this file will be included by uip.h, we cannot include uip.h
   here. But we might need to include uipopt.h if we need the u8_t and
   u16_t datatypes. */
#include "uipopt.h"

/* Next, we define the uip_tcp_appstate_t datatype. This is the state
   of our application, and the memory required for this state is
   allocated together with each TCP connection. One application state
   for each TCP connection. */
typedef struct httpd_state {
   uint8_t tstate;
   uint8_t fw_reset_pending;
} uip_tcp_appstate_t;

#define FW_RESET_PENDING 1
#define FW_UPDATE_PENDING 2

/* Finally we define the application function to be called by uIP. */
void httpd_appcall(void);
#ifndef UIP_APPCALL
#define UIP_APPCALL httpd_appcall
#endif /* UIP_APPCALL */

void httpd_init(void) __banked;

/* A web /cmd queued for cli (net.md item 2) */
extern __xdata uint8_t webcmd_pending, webcmd_done;
extern __xdata uint8_t webcmd_busy;	/* queued until its connection ends */
extern __xdata uint8_t webcmd_gen;	/* +1 a /cmd queued: which one the waiting connection is */
extern __xdata uint8_t webcmd_cap;	/* the capture between cli's steps: 1, 2 = full */
extern __xdata uint8_t webcmd_wait;	/* cli waits for room in outbuf: httpd sends, rx wakes cli */
extern __xdata uint8_t webcmd_cli;	/* a /cli: 200 always, its status lines in the body */
extern __xdata uint16_t slen;		/* the end of what is in outbuf */
/* outbuf's room cli needs for its next step (n bytes), CMD_TRUNCATED kept */
#define WEBCMD_ROOM(n)	(slen <= TCP_OUTBUF_SIZE - sizeof(CMD_TRUNCATED) - (n))
extern __xdata uint8_t cmd_capture;	/* rtlplayground.c: write_char's capture */
void httpd_webcmd_begin(void) __banked;	/* cli: the reply's header; its commands from config_buf */
void httpd_webcmd_end(void) __banked;	/* cli: complete the reply, webcmd_done */
#define WEBCMD_CAPTURE_ON()	(cmd_capture = webcmd_cap)
#define WEBCMD_CAPTURE_OFF()	(webcmd_cap = cmd_capture, cmd_capture = 0)

#endif
