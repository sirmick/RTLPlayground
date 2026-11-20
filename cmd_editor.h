#ifndef _CMD_EDITOR_H_
#define _CMD_EDITOR_H_

#include <stdint.h>

void cmd_editor_init(void) __banked;
void cmd_edit(void) __banked;

/* The console's line (console.md): complete when con_line_ready; cli copies it */
extern __xdata uint8_t con_line[];
extern __xdata uint8_t con_line_ready;
extern __xdata uint8_t cursor, cmd_line_len;	/* the editor's: om_tab() types into the line */
extern __xdata uint8_t con_tab;		/* OM_TAB_MORE: a Tab's walk goes on (the con task yields) */

#endif
