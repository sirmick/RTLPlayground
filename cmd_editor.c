#include "cmd_parser.h"
#include "con_ring.h"
#include "machine.h"
#include "om.h"


/* The console's own line buffer: the editor edits it, cli copies a complete
 * line into cmd_buffer (console.md: two input buffers, one parser) */
__xdata uint8_t con_line[CMD_BUF_SIZE];
__xdata uint8_t con_line_ready;	/* a complete line waits for cli */
// Properties of currently edited command line in con_line[CMD_BUF_SIZE]
__xdata uint8_t cursor;
__xdata uint8_t cmd_line_len;
static __xdata uint8_t current_cmdline[CMD_BUF_SIZE];	/* the line being typed, while one is recalled */
static __xdata uint8_t cur_len;
static __xdata uint8_t h_on, h_at;	/* recalling: the entry shown starts at h_at */
__xdata uint8_t con_tab;		/* om_tab()'s: 1 the Tab before completed nothing; OM_TAB_MORE its walk goes on */
static __xdata uint8_t c, i;

/* cmd_parser.c's cmd_save(): entries end in '\n', the next one goes at
 * cmd_history_ptr; CMD_HISTORY_SIZE is 256, so the 8-bit indexes wrap */
extern __xdata uint8_t cmd_history[CMD_HISTORY_SIZE];
extern __xdata uint8_t cmd_history_ptr;

/* the keys, as the escape sequences decode: ESC [ A, B, C, D and ESC [ 3 ~ */
#define K_UP	0x80
#define K_DOWN	0x81
#define K_RIGHT	0x82
#define K_LEFT	0x83
#define K_DEL	0x84

void cmd_editor_init(void) __banked
{
	con_rx_tail = con_rx_head; // Drop what was typed during the boot
	cursor = 0;
	cmd_line_len = 0;
	con_line_ready = 0;
	h_on = 0;
	con_tab = 0;
}

/* the line from the cursor on, and a space over a deleted character; the
 * terminal's cursor back where it was (BS works like cursor-left) */
static void redraw(void)
{
	for (i = cursor; i < cmd_line_len; i++)
		write_char(con_line[i]);
	write_char(' ');
	for (i = cursor; i <= cmd_line_len; i++)
		write_char('\010');
}

static void del(void)			/* the character at the cursor */
{
	if (cursor < cmd_line_len) {
		cmd_line_len--;
		for (i = cursor; i < cmd_line_len; i++)
			con_line[i] = con_line[i + 1];
		redraw();
	}
}

static void show(void)			/* a recalled line: the prompt and it again, the cursor at its end */
{
	print_string("\r\033[2K");
	om_prompt();
	print_string("> ");
	for (cursor = 0; cursor < cmd_line_len; cursor++)
		write_char(con_line[cursor]);
}

static void recall(void)		/* the entry at h_at */
{
	cmd_line_len = 0;
	for (i = h_at; cmd_history[i] != '\n' && cmd_line_len < CMD_BUF_SIZE - 1; i++)
		con_line[cmd_line_len++] = cmd_history[i];
	show();
}

/*
 * Allows editing the current command line held in con_line[CMD_BUF_SIZE] by
 * identifying new characters typed or up to 4-byte escape sequences in the
 * console RX ring con_rx_buf[CON_RX_SIZE] (con_ring.h), which the serial ISR
 * fills; con_rx_tail is the editor's read position (8 bits: it wraps by
 * itself). An escape sequence not complete yet waits for the next step.
 * Upon detecting new characters or escape sequences, the con_line and the
 * representation of the command line in the terminal are updated.
 * To debug, the easist is to interpose a tty-interceptor between the physical
 * serial device and a logical one created by interceptty:
 * sudo interceptty -s 'ispeed 115200 ospeed 115200' /dev/ttyUSB0 /dev/tmpS
 * picocom -b 115200 /dev/tmpS
 */
void cmd_edit(void) __banked
{
	/* While a line waits for cli, nothing more is read: typing or a paste
	 * stays in the RX ring instead of overwriting the pending line (U2) */
	while (!con_line_ready) {
		if (con_tab >= OM_TAB_MORE) {	/* a Tab's walk, a step each: the keys after it wait */
			if ((con_tab = om_tab(con_tab)) == OM_TAB_MORE)
				return;
			continue;
		}
		if (con_rx_tail == con_rx_head)
			return;
		c = con_rx_buf[con_rx_tail];
		i = 1;
		if (c == '\033') {
			if ((uint8_t)(con_rx_head - con_rx_tail) < 3)
				return;
			c = con_rx_buf[(uint8_t)(con_rx_tail + 2)] - 'A';
			i = 3;
			if ((uint8_t)(c + 'A' - '0') < 10) {	/* ESC [ n ~ */
				if ((uint8_t)(con_rx_head - con_rx_tail) < 4)
					return;
				i = 4;
				c = c == (uint8_t)('3' - 'A') ? K_DEL - K_UP : 0xff;
			}
			if (con_rx_buf[(uint8_t)(con_rx_tail + 1)] != '[' || c > K_DEL - K_UP)
				c = 0;		/* unknown: dropped */
			else
				c += K_UP;
		}
		con_rx_tail += i;
		if (c == '\t') {		/* 0, 1: OM_TAB_NEW, + 1 the next lists */
			con_tab += OM_TAB_NEW;
			continue;
		}
		con_tab = 0;
		switch (c) {
		case K_LEFT:
			if (cursor) {
				write_char('\010');
				cursor--;
			}
			break;
		case K_RIGHT:
			if (cursor < cmd_line_len)
				write_char(con_line[cursor++]);
			break;
		case K_UP:
			if (!h_on) {
				memcpy(current_cmdline, con_line, cmd_line_len);
				cur_len = cmd_line_len;
				h_at = cmd_history_ptr;
				h_on = 1;
			}
			i = h_at - 1;		/* the '\n' that ends the entry before */
			if (cmd_history[i] == '\n') {
				do
					i--;
				while (cmd_history[i] && cmd_history[i] != '\n');
				h_at = i + 1;
				recall();
			}
			break;
		case K_DOWN:
			if (!h_on)
				break;
			if (h_at != cmd_history_ptr) {
				for (i = h_at; cmd_history[i] != '\n'; i++);
				h_at = i + 1;
			}
			if (h_at != cmd_history_ptr) {
				recall();
				break;
			}
			h_on = 0;		/* past the newest: the line being typed */
			cmd_line_len = cur_len;
			memcpy(con_line, current_cmdline, cur_len);
			show();
			break;
		case 127:			/* Backspace: DEL or BS/^H */
		case 8:
			if (!cursor)
				break;
			write_char('\010');
			cursor--;
			/* fall through */
		case K_DEL:
			del();
			break;
		case '\n':
		case '\r':
			write_char('\n');
			con_line[cmd_line_len] = NUL;
			// The line, even an empty one, goes to cli, which prints the prompt
			con_line_ready = 1;
			cursor = 0;
			cmd_line_len = 0;
			h_on = 0;
			break;
		default:
			// A printable character, inserted at the cursor. One byte is kept
			// for the terminating NUL written on Enter: a full line drops it.
			if (c >= ' ' && c < 127 && cmd_line_len < CMD_BUF_SIZE - 1) {
				for (i = cmd_line_len; i > cursor; i--)
					con_line[i] = con_line[i - 1];
				con_line[cursor] = c;
				cmd_line_len++;
				write_char(con_line[cursor++]);
				if (cursor < cmd_line_len)
					redraw();
			}
		}
	}
}
