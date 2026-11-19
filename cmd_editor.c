#include "cmd_parser.h"
#include "con_ring.h"
#include "machine.h"


/* The console's own line buffer: the editor edits it, cli copies a complete
 * line into cmd_buffer (console.md: two input buffers, one parser) */
__xdata uint8_t con_line[CMD_BUF_SIZE];
__xdata uint8_t con_line_ready;	/* a complete line waits for cli */
// Properties of currently edited command line in con_line[CMD_BUF_SIZE]
__xdata uint8_t cursor;
__xdata uint8_t cmd_line_len;
__xdata uint8_t current_cmdline[CMD_BUF_SIZE];
__xdata uint16_t history_editptr;


extern __xdata uint8_t cmd_history[CMD_HISTORY_SIZE];
extern __xdata uint16_t cmd_history_ptr;

void cmd_editor_init(void) __banked
{
	con_rx_tail = con_rx_head; // Drop what was typed during the boot
	cursor = 0;
	cmd_line_len = 0;
	con_line_ready = 0;
	history_editptr = 0xffff;
}

/*
 * Allows editing the current command line held in con_line[CMD_BUF_SIZE] by
 * identifying new characters typed or up to 4-byte escape sequences in the
 * console RX ring con_rx_buf[CON_RX_SIZE] (con_ring.h), which the serial ISR
 * fills; con_rx_tail is the editor's read position.
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
	while (!con_line_ready && con_rx_tail != con_rx_head) {
		if (con_rx_buf[con_rx_tail] >= ' ' && con_rx_buf[con_rx_tail] < 127) { // A printable character, copy to command line
			// Reserve one byte for the terminating NUL written on Enter. When the
			// line is full, drop the character but still fall through to advance the
			// serial-ring read pointer below; a 'continue' here would spin forever.
			if (cmd_line_len < CMD_BUF_SIZE - 1) {
				write_char(con_rx_buf[con_rx_tail]);
				// Shift buffer to right
				for (uint8_t i = cmd_line_len; i > cursor; i--)
					con_line[i] = con_line[i-1];
				// Insert char in comand buffer
				con_line[cursor++] = con_rx_buf[con_rx_tail];
				cmd_line_len++;
				// Print rest of line
				for (uint8_t i = cursor; i < cmd_line_len; i++)
					write_char(con_line[i]);
				// Move backwards
				for (uint8_t i = cursor; i < cmd_line_len; i++)
					write_char('\010'); // BS works like cursor-left
			}
		} else if (con_rx_buf[con_rx_tail] == '\033') { // ESC-Sequence
			// Wait until we have at least 3 characters including the ESC character in the serial buffer
			if (((con_rx_head + CON_RX_SIZE - con_rx_tail) & CON_RX_MASK) < 3)
				continue;
			if (((con_rx_head > con_rx_tail ? con_rx_head - con_rx_tail : CON_RX_SIZE + con_rx_head - con_rx_tail) >= 4)
				   && con_rx_buf[con_rx_tail] == '\033' && con_rx_buf[(con_rx_tail + 1) & CON_RX_MASK] == '[' && con_rx_buf[(con_rx_tail + 2) & CON_RX_MASK] == '3' && con_rx_buf[(con_rx_tail + 3) & CON_RX_MASK] == '~') { // DEL
				if (cursor < cmd_line_len) {
					write_char('\033'); write_char('['); write_char('1'); write_char('P'); // Delete to end of line
					cmd_line_len--;
					for (uint8_t i = cursor; i < cmd_line_len; i++) {
						con_line[i] = con_line[i+1];
						write_char(con_line[i]);
					}
					for (uint8_t i = cursor; i < cmd_line_len; i++)
						write_char('\010');
				}
				con_rx_tail += 4;
				con_rx_tail &= CON_RX_MASK;
				continue;
			} else if (con_rx_buf[con_rx_tail] == '\033' && con_rx_buf[(con_rx_tail + 1) & CON_RX_MASK] == '[' && con_rx_buf[(con_rx_tail + 2) & CON_RX_MASK] == 'D') { // <CURSOR-LEFT>
				if (cursor) {
					write_char('\010'); // BS works like cursor-left
					cursor--;
				}
				con_rx_tail += 3;
				con_rx_tail &= CON_RX_MASK;
				continue;
			} else if (con_rx_buf[con_rx_tail] == '\033' && con_rx_buf[(con_rx_tail + 1) & CON_RX_MASK] == '[' && con_rx_buf[(con_rx_tail + 2) & CON_RX_MASK] == 'C') { // <CURSOR-RIGHT>
				if (cursor < cmd_line_len) {
					write_char('\033'); write_char('['); write_char('C');
					cursor++;
				}
				con_rx_tail += 3;
				con_rx_tail &= CON_RX_MASK;
				continue;
			} else if (con_rx_buf[con_rx_tail] == '\033' && con_rx_buf[(con_rx_tail + 1) & CON_RX_MASK] == '[' && con_rx_buf[(con_rx_tail + 2) & CON_RX_MASK] == 'A') { // <CURSOR-UP>
				for (uint8_t i = 0; i < cmd_line_len; i++)
					current_cmdline[i] = con_line[i];
				current_cmdline[cmd_line_len] = 0;
				__xdata uint16_t p;
				if (history_editptr == 0xffff)
					p = (cmd_history_ptr - 2) & CMD_HISTORY_MASK;
				else
					p = history_editptr;
				// Move cursor to beginning of line
				write_char('\033'); write_char('['); itoa(cursor + 2); write_char('D');
				cursor = 0;
				while (cmd_history[p] && cmd_history[p] != '\n') {
					cursor++;
					p--;
					p &= CMD_HISTORY_MASK;
				}
				history_editptr = (p - 1) & CMD_HISTORY_MASK;
				p = (p + 1) & CMD_HISTORY_MASK;
				if (cursor) {
					print_string("\033[2K> "); // Clear entire line: ^[[2K and print new prompt
					for (uint8_t i = 0; i < cursor; i++) {
						con_line[i] = cmd_history[p];
						write_char(con_line[i]);
						p = (p+1) & CMD_HISTORY_MASK;
					}
					cmd_line_len = cursor;
				} else {
					print_string("\033[2C"); // Move 2 right to start of editing space
				}
				con_rx_tail += 3;
				con_rx_tail &= CON_RX_MASK;
				continue;
			} else if (con_rx_buf[con_rx_tail] == '\033' && con_rx_buf[(con_rx_tail + 1) & CON_RX_MASK] == '[' && con_rx_buf[(con_rx_tail + 2) & CON_RX_MASK] == 'B') { // <CURSOR-DOWN>
				if (history_editptr != 0xffff) {
					__xdata uint16_t p = (history_editptr + 2) & CMD_HISTORY_MASK;
					// Move cursor to beginning of line
					write_char('\033'); write_char('['); itoa(cursor + 2); write_char('D');
					print_string("\033[2K> "); // Clear entire line: ^[[2K and print new prompt
					uint8_t i = 0;
					while (cmd_history[p] && cmd_history[p] != '\n') {
						p = (p + 1) & CMD_HISTORY_MASK;
					}
					p = (p + 1) & CMD_HISTORY_MASK;
					while (cmd_history[p] && cmd_history[p] != '\n') {
						con_line[i] = cmd_history[p];
						write_char(con_line[i++]);
						p = (p + 1) & CMD_HISTORY_MASK;
					}
					history_editptr = (p - 1) & CMD_HISTORY_MASK;
					if (!i) {
						if (current_cmdline[i]) {
							while (current_cmdline[i]) {
								con_line[i] = current_cmdline[i];
								write_char(con_line[i++]);
							}
						} else {
							write_char('\033'); write_char('['); write_char('C');
						}
						history_editptr = 0xffff;
						// Move cursor right
					}
					cmd_line_len = i;
					cursor = 0;
					// Move cursor again to beginning of line
					write_char('\033'); write_char('['); itoa(i); write_char('D');
				} else {
					// If we are at the last entry of the history, just move the cursor to the end of the line
					if (cursor < cmd_line_len) {
						write_char('\033'); write_char('['); itoa(cmd_line_len - cursor); write_char('C');
					}
					cursor = cmd_line_len;
				}
				con_rx_tail += 3;
				con_rx_tail &= CON_RX_MASK;
				continue;
			} else { // An unknown or not yet complete Escape sequence: wait
				continue;
			}
		} else if (con_rx_buf[con_rx_tail] == 127 || con_rx_buf[con_rx_tail] == 8) {  // Backspace DEL or BS/^H
			if (cursor > 0) {
				write_char('\010');
				for (uint8_t i = cursor; i < cmd_line_len; i++)
					write_char(con_line[i]);
				write_char(' '); // Overwrite end of line
				// Move backwards n steps:
				for (uint8_t i = cursor; i <= cmd_line_len; i++)
					write_char('\010');
				cursor--;
				for (uint8_t i = cursor; i <= cmd_line_len; i++)
					con_line[i] = con_line[i+1];
				cmd_line_len--;
			}
		}
		// Check whether return was pressed:
		if (con_rx_buf[con_rx_tail] == '\n' || con_rx_buf[con_rx_tail] == '\r') {
			write_char('\n');
			con_line[cmd_line_len] = NUL;
			// The line, even an empty one, goes to cli, which prints the prompt
			con_line_ready = 1;
			cursor = 0;
			cmd_line_len = 0;
			history_editptr = 0xffff;
		}
		con_rx_tail++;
		con_rx_tail &= CON_RX_MASK;
	}
}
