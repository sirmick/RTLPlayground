
#include "httpd.h"
#include "page_impl.h"
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "cmd_parser.h"
#include "rtl837x_flash.h"
#include "uip.h"
#include "timer.h"
#include "sessions.h"
#include "html_data.h"
#include "boot.h"
#include "loader/layout.h"
#include "update_apply.h"
#include "flash_job.h"

// #define DEBUG
#include "debug.h"

#define SESSION_TIMEOUT_DEFAULT 3600

/* Seconds of inactivity after which a session cookie stops validating.
 * Configurable with the "session" command, see cmd_parser.c. */
__xdata uint16_t session_timeout = SESSION_TIMEOUT_DEFAULT;

#define CMARK_S 6


extern volatile __xdata uint8_t sfr_data[4];
extern __xdata uint8_t cmd_capture;	/* owned by rtlplayground.c, see write_char_no_syslog() */
extern __code const uint8_t * __code const hex;
extern __code const struct f_data f_data[];
extern __code const char * __code const mime_strings[];
extern __xdata struct flash_region_t flash_region;
extern __xdata uint32_t flash_size;

// Flash buffer to optimize flash writing speed, write_len is the current filling position
extern __xdata uint8_t flash_buf[FLASH_BUF_SIZE];
__xdata uint32_t uptr; // Current flash write position
__xdata uint16_t write_len;

__xdata uint8_t outbuf[TCP_OUTBUF_SIZE];
__xdata uint8_t entry;
__xdata uint16_t slen;
__xdata uint16_t o_idx;
__xdata uint16_t sent_len;
__xdata uint16_t len_left;
__xdata uint16_t cont_len;
__xdata uint32_t cont_addr;

// HTTP header properties
__xdata uint8_t boundary[72];

// a client may split the request anywhere, including inside a boundary or a
// part header, so a configuration upload is parsed only once it is complete;
// sized for a full config sector plus the multipart framing around it
#define CONFIG_UPLOAD_BUF (CONFIG_LEN + 384)
__xdata uint8_t config_upload;
__xdata uint8_t config_buf[CONFIG_UPLOAD_BUF];
// bytes buffered in config_buf so far (HTTP header, POST body, or a firmware
// part header); accumulates across TCP segments
__xdata uint16_t pre_acc;
__xdata uint8_t * __xdata content_type = 0;
__xdata uint8_t * __xdata session = 0;
__xdata uint16_t content_length;

// Global variables holding POST state
__xdata uint16_t bindex; // Current index into the boundary
__xdata uint8_t verify_crc;
__xdata uint32_t max_upload;
__xdata uint32_t upload_end;
__xdata uint8_t upload_loader;
__xdata uint8_t upload_mdata;	// a machine data region, written in place
__xdata uint16_t short_parsed;

#define POSTBODY_CMD	1
#define POSTBODY_LOGIN	2
#define POSTBODY_TIMEOUT (5 * SYS_TICK_HZ)
#define UPLOAD_TIMEOUT (30 * SYS_TICK_HZ)	/* an upload's body, from segment to segment */
__xdata uint8_t postbody_endpoint;
__xdata struct timer postbody_timer;

__xdata char passwd[21];
// Up to four logins at once, see sessions.c
__xdata uint8_t authenticated;
__xdata uint32_t now;

#define TSTATE_NONE		0
#define TSTATE_TX		1
#define TSTATE_ACKED 		2
#define TSTATE_CLOSED 		3
#define TSTATE_POST 		4
#define TSTATE_MULTIPART	5
#define TSTATE_POSTBODY		6
#define TSTATE_HEADER		7
#define TSTATE_WEBCMD		8	/* a /cmd queued for cli; the reply goes out on a poll */
#define TSTATE_UPLOAD		9	/* stopped: the flash job writes what was staged */

extern __xdata uint16_t crc_value;
__xdata uint16_t crc_final;
void crc16_bank1(__xdata uint8_t *v) __naked;



inline uint8_t is_separator(uint8_t c)
{
	return c == ' ' || c == '\t' || c == '?' || c == '=';
}


void httpd_init(void) __banked
{
	config_upload = 0; // xdata is not zeroed by the startup code
	webcmd_pending = 0;
	webcmd_done = 0;
	fl_kind = 0;		/* the flash job (flash_job.c) */
	fl_finished = 0;
	up_active = 0;
	uip_rcv_wnd = UIP_TCP_MSS;
	// Start listening to port 80
	uip_listen(HTONS(80));
	// Not through uip_conn: it only points at a connection while uIP is
	// handling one, and nothing has set it yet at init time.
	for (uint8_t i = 0; i < UIP_CONNS; i++)
		uip_conns[i].appstate.tstate = TSTATE_CLOSED;
	session_init();
}


uint8_t find_entry(__xdata uint8_t *e)
{
	uint8_t i, j;

	for (i = 0; f_data[i].len; i++) {
		j = 0;
		while (f_data[i].file[j] && (f_data[i].file[j] == e[j])) {
			j++;
		}
		if ((!f_data[i].file[j]) && (!e[j])) {
			return i;
		}
	}
	return 0xff;
}


bool is_word(__xdata uint8_t *xdata_str_p, __code const uint8_t * __xdata code_str_p)
{
	uint8_t u, c;

	while (1) {
		u = *xdata_str_p++;
		c = *code_str_p++;

		if (c == NUL) {
			if (u != NUL && u != ' ' && u != '\t' && u != ':' && u != '?' && u != '=' && u != '\n' && u != '\r')
				return false;
			return true;
		}

		if (c != u) {
			return false;
		}
	}
}


/* name must be lower-case, starting with the '\n' of the previous line's end */
__xdata uint8_t *header_value(__xdata uint8_t *p, __code const uint8_t *name)
{
	uint8_t u, c;

	while ((c = *name++)) {
		u = *p++;
		if (u >= 'A' && u <= 'Z')
			u += 'a' - 'A';
		if (u != c)
			return 0;
	}
	while (*p == ' ' || *p == '\t')
		p++;
	return p;
}


bool is_url_word_x(__xdata uint8_t *uri_str_p, __xdata uint8_t *src_str_p)
{
	uint8_t u, s;

	while(1) {
		u = *uri_str_p++;
		s = *src_str_p++;

		if (s == NUL) {
			if (u != NUL && u != ' ' && u != '\t' && u != ':' && u != '?' && u != '=' && u != '\n' && u != '\r')
				return false;
			return true;
		}

		if (u == '%') {
			bool again = true;
			u = 0;

			while(1) {
				// Swap instruction is fine for rotation
				u = (u << 4) | (u >> 4);

				uint8_t p = *uri_str_p++;
				u |= p - '0' < 10 ? (p - '0') : (p - 'A' + 10);

				// force `jbc`-instruction.
				if (again) {
					again = false;
				} else {
					break;
				}
			}
		} else if (u == '+') {
			u = ' ';
		}

		if (s != u) {
			return false;
		}
	}
}


bool is_word_x(__xdata uint8_t * lhs_str_p, __xdata uint8_t * rhs_str_p)
{
	uint8_t u, c;

	while (1) {
		u = *lhs_str_p++;
		c = *rhs_str_p++;

		if (c == NUL) {
			/* ';' separates cookies in a Cookie header, so it ends a value too. */
			if (u != NUL && u != ' ' && u != '\t' && u != ':' && u != '?' && u != '=' && u != '\n' && u != '\r' && u != ';')
				return false;
			return true;
		}

		if (c != u) {
			return false;
		}
	}
}


uint8_t parse_short(__xdata uint8_t *p)
{
	uint8_t err = 1;
	uint8_t c = 0;

	short_parsed = 0;
	while(1) {
		c = *p++ - '0';
		if (c > 9) { break; }
		err = 0;
		if (short_parsed > 6552)
			short_parsed = 0xffff;
		else
			short_parsed = (short_parsed * 10) + c;
	}
	return err;
}


void send_not_found(void)
{
	slen = strtox(outbuf, "HTTP/1.1 404 Not found\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n" \
			      "<!DOCTYPE HTML PUBLIC>\n<title>404 Not Found</title>\n<h1>Not Found</h1>\n");
}


void send_bad_request(void)
{
	slen = strtox(outbuf, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n" \
			      "<!DOCTYPE HTML PUBLIC>\n<title>400 Bad Request</title>\n<h1>Bad Request</h1>\n");
}


void send_to_login(void)
{
	slen = strtox(outbuf, "HTTP/1.1 302 Found\r\nConnection: close\r\n" \
			      "Location: login.html\r\n\r\n");
}


void send_unauthorized(void)
{
	slen = strtox(outbuf, "HTTP/1.1 401 Unauthorized\r\nConnection: close\r\n\r\n");
}


void send_length_required(void)
{
	slen = strtox(outbuf, "HTTP/1.1 411 Length Required\r\nConnection: close\r\n\r\n");
}


void send_ok(void)
{
	slen = strtox(outbuf, "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
}


__xdata uint8_t *scan_header(__xdata uint8_t * __xdata p)
{
	__xdata uint8_t *v;

	content_type = 0;
	content_length = 0;
	session = 0;
	authenticated = 0;

	while (!strstart(p, "\r\n\r\n")) {
		dbg_char(*p);
		if (!*p)
			break;
		p++;
		if ((v = header_value(p, "\ncontent-type:")))
			content_type = v;
		else if ((v = header_value(p, "\ncontent-length:"))) {
			parse_short(v);
			content_length = short_parsed;
		} else if ((v = header_value(p, "\ncookie:"))) {
			/* Scan for the "session" key: the header may hold several
			 * cookies in any order. Match "session" not "session=" -
			 * is_word() requires a separator after the match and '=' is
			 * one, so this also rejects a longer key like "sessionx". */
			while (*v && *v != '\r' && *v != '\n') {
				if (is_word(v, "session")) {
					session = v + 8;	/* past "session=" */
					break;
				}
				v++;
			}
		}
	}
	if (content_type && is_word(content_type, "multipart/form-data; boundary")) {
		dbg_string("\nFound multipart\n");
		content_type += 30;
		uint8_t i = 0;
		while (i < (sizeof(boundary) - 5) &&
				content_type[i] != '\r' && content_type[i] != '\n') {
			boundary[i + 4] = content_type[i];
			i++;
		}
		// The boundary between parts is "\r\n--" + the boundary given in the header
		boundary[0] = '\r';
		boundary[1] = '\n';
		boundary[2] = '-';
		boundary[3] = '-';
		boundary[i + 4] = 0;
	}

	read_reg_timer(&now);

	if (session)
		session_check();
	return p;
}

/* 0: body incomplete, 1: configuration stored, 2: malformed */
static uint8_t config_take(void)
{
	// #386: needs static, otherwise it still lands in SRAM/DSEG
	static __xdata uint16_t cfg_pos, cfg_hdr, cfg_body, cfg_end, cfg_last, cfg_i;
	__xdata uint8_t cfg_bl, cfg_run;

	cfg_bl = strlen_x(boundary);

	// the body is complete once the closing boundary has arrived
	cfg_last = 0;
	while (1) {
		if (cfg_last + cfg_bl + 1 >= pre_acc)
			return 0;
		if (strstart_x(&config_buf[cfg_last], boundary)
		    && strstart(&config_buf[cfg_last + cfg_bl], "--"))
			break;
		cfg_last++;
	}

	// every part lies ahead of the closing boundary, so it bounds the walk
	cfg_pos = 0;
	while (cfg_pos < cfg_last) {
		if (!strstart_x(&config_buf[cfg_pos], boundary)) {
			cfg_pos++;
			continue;
		}
		cfg_hdr = cfg_pos + cfg_bl;
		cfg_body = cfg_hdr;
		while (1) {
			if (cfg_body + 3 >= cfg_last)
				return 2;
			if (strstart(&config_buf[cfg_body], "\r\n\r\n"))
				break;
			cfg_body++;
		}
		cfg_end = cfg_body;
		cfg_body += 4;
		// reaching cfg_last is a match: the last part ends at the closing boundary
		while (cfg_end < cfg_last && !strstart_x(&config_buf[cfg_end], boundary))
			cfg_end++;
		while (cfg_hdr + 8 < cfg_body) {
			// the part carrying a filename holds the configuration
			if (strstart(&config_buf[cfg_hdr], "filename")) {
				// the payload plus its terminator must fit the sector
				if (cfg_end - cfg_body + 1 > CONFIG_LEN)
					return 2;
				cfg_run = 0;
				for (cfg_i = cfg_body; cfg_i < cfg_end; cfg_i++) {
					if (config_buf[cfg_i] == '\n')
						cfg_run = 0;
					else if (++cfg_run >= CMD_BUF_SIZE - 1)
						return 2;
				}
				config_buf[cfg_end] = 0;
				fl_src = config_buf + cfg_body;	/* for the flash job */
				fl_len = cfg_end - cfg_body + 1;
				return 1;
			}
			cfg_hdr++;
		}
		cfg_pos = cfg_end;
	}
	return 2;
}


// unlike scan_header(), keeps no auth state, so it may run on every buffered segment
static uint16_t preamble_payload_start(uint16_t n)
{
	// Skip the HTTP header separator retained by handle_post().
	uint16_t pos = 4;

	while (pos + 3 < n && !strstart(&config_buf[pos], "\r\n\r\n"))
		pos++;
	if (pos + 3 >= n)
		return 0;
	return pos + 4;
}


// The staged segment upload_prog() reads; filled by the caller before the job
__xdata struct {
	__xdata uint8_t *p;
	uint16_t bptr;
	uint16_t plen;
} upload_settings;

/* The upload is written by the flash job (flash_job.c, net.md item 1):
 * httpd stages the body's segments in config_buf as they come, and uIP's
 * receive window is the room left there; the job's steps call upload_prog()
 * and upload_verify(), and once the job has written all of the stage it
 * starts again. config_buf is the stage because uIP has one connection
 * (the #error at webcmd_queue()): nothing else can reach it meanwhile, and
 * while the job writes for a connection that has ended a new one is reset.
 * When the job has ended, the connection's poll takes up_state and up_reset,
 * and sends a window update if the window was short. */
#ifndef UP_STAGE_LEN		/* the stage's room; smaller only to test (make UP_STAGE=n) */
#define UP_STAGE_LEN	CONFIG_UPLOAD_BUF
#elif UP_STAGE_LEN > CONFIG_UPLOAD_BUF || UP_STAGE_LEN < 1600
#error "UP_STAGE_LEN: the stage is config_buf; it must hold the upload's part headers and a segment"
#endif
#define UP_PAGES	2	/* pages an FL_PROG step parses and programs: ~10 ms */
#define VF_CHUNK	2048	/* bytes an FL_VERIFY step reads back */
#define UP_MORE		1	/* upload_fill(): the segment is used up */
#define UP_PAGE		2	/* a page is in flash_buf */
#define UP_END		3	/* the part has ended: write_len bytes left in flash_buf */
__xdata uint8_t up_state;	/* the connection's tstate once the job has ended */
__xdata uint8_t up_reset;	/* its fw_reset_pending, if not 0 */
static __xdata uint8_t up_r, up_erased, up_n;
static __xdata uint8_t up_dot;	/* a later segment came: a '.' at the next */
static __xdata uint8_t up_short;	/* the window advertised was short of an MSS */
static __xdata uint16_t st_len;	/* bytes in the stage (config_buf) */
__xdata uint8_t up_active;	/* an upload's body is coming: other jobs wait (flash_job.h) */
__xdata uint16_t uip_rcv_wnd;	/* uIP's receive window (uip-conf.h) */
#ifdef LAYOUT_COMPACT
static __xdata uint8_t up_install;
#endif
static __xdata uint32_t vf_at, vf_end;

/*
 * Reads post data from the staged segment into flash_buf, up to a page
 * Input: upload_settings, set by the caller
 * Returns UP_MORE, UP_PAGE or UP_END
 */
static uint8_t upload_fill(void)
{
	dbg_string("upload_fill called: ");
	dbg_short(upload_settings.bptr); dbg_char('\n');

	do {
		if (upload_settings.bptr >= upload_settings.plen)
			return UP_MORE;
		// Have we reached the end of the part?
		if (!boundary[bindex])
			return UP_END;
		if (upload_settings.p[upload_settings.bptr] == boundary[bindex]) {
			if (!bindex)
				crc_final = crc_value;
			crc16_bank1(upload_settings.p + upload_settings.bptr);
			upload_settings.bptr++;
			bindex++;
		} else {
			if (bindex) {
				memcpy(flash_buf + write_len, boundary, bindex);
				write_len += bindex;
				bindex = 0;
			} else {
				crc16_bank1(upload_settings.p + upload_settings.bptr);
				flash_buf[write_len++] = upload_settings.p[upload_settings.bptr++];
			}
		}
	} while (write_len < FLASH_PAGE_SIZE);
	return UP_PAGE;
}

/* The answer to a written part, its read-back CRC in crc_value */
static void upload_verdict(void)
{
	dbg_string("CRC16: "); dbg_short(crc_final); dbg_char('\n');
#ifdef LAYOUT_COMPACT
	/* An install image: the application, then the loader at
	 * LDR_STAGE, each with its own CRC. A board without the
	 * loader converts at the next boot, then the loader
	 * installs the application; one with it ignores the stage. */
	if (up_install)
		crc_final = crc_value == 0xb001 && loader_stage_fits() ? 0xb001 : 0;
	else
#endif
	/* the stream CRC alone doesn't catch a write that went wrong */
	if (crc_final == 0xb001 && crc_value != 0xb001) {
		print_string("Flash read-back does not match the upload\n");
		crc_final = 0;		/* refused below as a bad checksum */
	}
	// Both bodies are 33 bytes; Content-Length lets the
	// browser complete the response without waiting for
	// the connection close (which a reset would swallow)
	if (upload_mdata) {
		/* in place, not needed to boot: no reset, and a bad
		 * region only leaves the board's extra (PoE) off */
		if (crc_final == 0xb001 && mdata_len(MDATA_START) == uptr - MDATA_START) {
			print_string("Machine data written, applies at the next boot\n");
			slen = strtox(outbuf, "HTTP/1.1 200 OK\r\nContent-Length: 36\r\n"
				"Content-Type: text/plain\r\n\r\n"
				"OK: machine data written, next boot\n");
		} else {
			print_string("Machine data not valid\n");
			slen = strtox(outbuf, "HTTP/1.1 400 Bad Request\r\nContent-Length: 27\r\n"
				"Content-Type: text/plain\r\n\r\n"
				"NO: not valid machine data\n");
		}
	} else if (crc_final == 0xb001 && upload_loader && uptr == LDR_STAGE + LDR_LOADER_LEN && !loader_stage_fits()) {
		print_string("Not a loader for this layout\n");
		slen = strtox(outbuf, "HTTP/1.1 400 Bad Request\r\nContent-Length: 33\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"NO: not a loader for this layout\n");
#ifndef LAYOUT_COMPACT
	/* a compact board without one is converted at the next boot
	 * (boot.c loader_convert()); a standard image uses the
	 * loader's area for itself */
	} else if (crc_final == 0xb001 && upload_loader && uptr == LDR_STAGE + LDR_LOADER_LEN && !loader_present()) {
		print_string("No loader to update\n");
		slen = strtox(outbuf, "HTTP/1.1 400 Bad Request\r\nContent-Length: 29\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"NO: no loader on this switch\n");
#endif
	} else if (crc_final == 0xb001 && (!upload_loader || uptr == LDR_STAGE + LDR_LOADER_LEN)) {
		print_string("Checksum OK.\nUpload to flash done, will reset!\n");
		slen = strtox(outbuf, "HTTP/1.1 200 OK\r\nContent-Length: 33\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"OK: checksum verified, rebooting\n");
#ifdef LAYOUT_COMPACT
		if (upload_loader || up_install)
			loader_convert_arm();	/* a board without the loader */
#endif
		// Reset once the response is fully ACKed
		up_reset = FW_UPDATE_PENDING;
	} else {
		print_string("Checksum incorrect! Aborting.\n");
		slen = strtox(outbuf, "HTTP/1.1 400 Bad Request\r\nContent-Length: 33\r\n"
			"Content-Type: text/plain\r\n\r\n"
			"NO: checksum failed, not applied\n");
	}
	/* A refused /loader or install image may have left a whole
	 * loader at LDR_STAGE: the next boot would convert to it. */
	if (up_reset != FW_UPDATE_PENDING && !upload_mdata
	    && (upload_loader || (uptr > LDR_STAGE && uptr <= LDR_STAGE + LDR_LOADER_LEN)))
		loader_stage_drop();
}

/*
 * FL_PROG: up to UP_PAGES pages of the staged segment parsed and programmed
 * Returns UP_PROG (more), UP_ERASE (the sector at fl_addr first), UP_VERIFY
 * (the part has ended) or UP_DONE (the segment is used up, or the answer built)
 */
uint8_t upload_prog(void) __banked
{
	for (up_n = UP_PAGES; up_n; up_n--) {
		if (!up_r)		/* kept across an FL_ERASE */
			up_r = upload_fill();
		if (up_r == UP_MORE) {	/* all of the stage written: upload_done() */
			up_r = 0;
			up_state = TSTATE_POST;
			return UP_DONE;
		}
		/* The staged image spans FIRMWARE_UPLOAD_START to _END (or the
		 * loader stage), the span that is read back. Nothing
		 * else bounds uptr: a body whose closing boundary never arrives
		 * keeps writing, and on a flash exactly this size the address
		 * wraps onto the running image at zero. */
		if (up_r == UP_PAGE && uptr >= upload_end) {
			print_string("Upload runs past the image area! Aborting.\n");
			slen = strtox(outbuf, "HTTP/1.1 400 Bad Request\r\nContent-Length: 30\r\n"
				"Content-Type: text/plain\r\n\r\n"
				"NO: upload exceeds image area\n");
			up_r = 0;
			up_state = TSTATE_NONE;
			return UP_DONE;
		}
		/* a page opening a sector, or a last partial one */
		if (!up_erased && !(uptr % FLASH_SECTOR_SIZE) && (up_r == UP_PAGE || write_len)) {
			up_erased = 1;
			fl_addr = uptr;
			return UP_ERASE;
		}
		up_erased = 0;
		flash_region.addr = uptr;
		if (up_r == UP_PAGE) {
			dbg_string("len: "); dbg_short(write_len); dbg_char(' ');
			dbg_string("CRC16: "); dbg_short(crc_value); dbg_char('\n');
			flash_region.len = FLASH_PAGE_SIZE;
			flash_write_bytes(flash_buf);
			uptr += FLASH_PAGE_SIZE;
			write_len -= FLASH_PAGE_SIZE;
			up_r = 0;
			// Copy the remaining byte for the next page to the beginning of the buffer.
			if (write_len > 0)
				memcpy(flash_buf, flash_buf + FLASH_PAGE_SIZE, write_len);
			continue;
		}
		dbg_string("len 2: "); dbg_short(write_len); dbg_char(' ');
		flash_region.len = write_len;
		flash_write_bytes(flash_buf);
		uptr += write_len;
		write_len = 0;
		up_r = 0;
		up_state = TSTATE_NONE;
		uip_rcv_wnd = UIP_TCP_MSS;
		if (!verify_crc)
			return UP_DONE;
		/* the read-back: of an install image its application, else of
		 * what was written, if the stream CRC was good */
#ifdef LAYOUT_COMPACT
		up_install = !upload_mdata && !upload_loader && uptr == FIRMWARE_UPLOAD_END + LDR_LOADER_LEN;
		if (up_install) {
			vf_at = FIRMWARE_UPLOAD_START;
			vf_end = FIRMWARE_UPLOAD_END;
		} else
#endif
		{
			vf_at = upload_mdata ? MDATA_START : upload_loader ? LDR_STAGE : FIRMWARE_UPLOAD_START;
			vf_end = crc_final == 0xb001 ? uptr : vf_at;
		}
		crc_value = 0;
		return UP_VERIFY;
	}
	return UP_PROG;
}

/* FL_VERIFY: VF_CHUNK bytes more of the read-back; at its end the answer.
 * Returns UP_VERIFY (more) or UP_DONE */
uint8_t upload_verify(void) __banked
{
	flash_crc16_from = vf_at;
	vf_at += VF_CHUNK;
	if (vf_at > vf_end)
		vf_at = vf_end;
	flash_crc16_to = vf_at;
	flash_crc16_more();
	if (vf_at != vf_end)
		return UP_VERIFY;
	upload_verdict();
	return UP_DONE;
}

/* FL_DONE: 1 if more of the body has come since the job's last FL_PROG (rx
 * can run several times before this step): FL_PROG again. Else the stage
 * starts again, in the same step as the job ends, and the window opens. */
uint8_t upload_done(void) __banked
{
	if (up_state == TSTATE_POST && up_active) {
		if (upload_settings.bptr < upload_settings.plen)
			return 1;
		st_len = 0;
		upload_settings.bptr = 0;
		upload_settings.plen = 0;
	}
	uip_rcv_wnd = UIP_TCP_MSS;
	return 0;
}

/* The stage, config_buf, from st_len on: the job starts if it has ended, or
 * takes it when it gets there. The window is the room left (at most an
 * MSS), so what the client may send always fits: nothing in flight is
 * dropped, and uIP's window never moves left (net.md item 1). 0 if the job
 * can't start. */
static uint8_t upload_go(void)
{
	upload_settings.plen = st_len;
	up_active = 1;
	timer_set(&postbody_timer, UPLOAD_TIMEOUT);
	if (!fl_kind) {
		fl_req = FL_UPLOAD;
		if (!flash_job_start())
			return 0;
	}
	uip_rcv_wnd = st_len < UP_STAGE_LEN ? UP_STAGE_LEN - st_len : 0;
	if (uip_rcv_wnd >= UIP_TCP_MSS)
		uip_rcv_wnd = UIP_TCP_MSS;
	else
		up_short = 1;
	uip_conn->appstate.tstate = TSTATE_UPLOAD;
	return 1;
}

/* A later segment of the body. 0 if it doesn't fit (sent past the window)
 * or the job can't start: the answer built. */
static uint8_t upload_take(void)
{
	if (st_len + uip_len > UP_STAGE_LEN) {
		print_string("Upload sent past the window, aborting.\n");
		send_bad_request();
		return 0;
	}
	if (up_dot)
		write_char('.');	/* the segment before: not the last */
	up_dot = 1;
	memcpy(config_buf + st_len, uip_appdata, uip_len);
	st_len += uip_len;
	if (upload_go())
		return 1;
	send_bad_request();
	return 0;
}

static void handle_config_fragment(__xdata uint8_t *p)
{
	__xdata struct httpd_state * __xdata s = &(uip_conn->appstate);
	__xdata uint16_t frag_len;
	uint8_t taken;

	frag_len = uip_len - (p - (__xdata uint8_t *)uip_appdata);
	if (pre_acc + frag_len >= CONFIG_UPLOAD_BUF) {
		print_string("Configuration too large, aborting.\n");
		config_upload = 0;
		s->tstate = TSTATE_NONE;
		send_bad_request();
		return;
	}
	memcpy(config_buf + pre_acc, p, frag_len);
	pre_acc += frag_len;
	taken = config_take();
	if (!taken) {
		s->tstate = TSTATE_MULTIPART;
		return;
	}
	config_upload = 0;
	s->tstate = TSTATE_NONE;
	if (taken == 2) {
		send_bad_request();
		return;
	}
	/* the job writes it; the poll after sends the OK */
	send_ok();
	fl_req = FL_CONFIG;
	if (!flash_job_start()) {
		send_bad_request();
		return;
	}
	up_state = TSTATE_NONE;
	up_reset = 0;
	s->tstate = TSTATE_UPLOAD;	/* the request is whole: more data is dropped */
}


static void handle_firmware_fragment(__xdata uint8_t *p)
{
	__xdata struct httpd_state * __xdata s = &(uip_conn->appstate);
	__xdata uint16_t frag_len, payload_start;

	frag_len = uip_len - (p - (__xdata uint8_t *)uip_appdata);
	if (pre_acc + frag_len >= CONFIG_UPLOAD_BUF) {
		print_string("Firmware upload header too large, aborting.\n");
		config_upload = 0;
		s->tstate = TSTATE_NONE;
		send_bad_request();
		return;
	}
	memcpy(config_buf + pre_acc, p, frag_len);
	pre_acc += frag_len;
	payload_start = preamble_payload_start(pre_acc);
	if (!payload_start) {
		s->tstate = TSTATE_MULTIPART;
		return;
	}
	dbg_string("Have content octets\n");

	flash_init(0); // Re-initialize flash for non-DIO operation, otherwise flashing fails
	set_sys_led_state(SYS_LED_FAST);

	crc_value = 0;
	bindex = 0;
	write_len = 0;
	// A verdict is only built once the upload part completes;
	// clear any stale response so the completion check in the
	// appcall POST branch cannot send leftovers
	slen = 0;
	up_reset = 0;
	up_r = 0;
	up_erased = 0;
	up_dot = 0;
	up_short = 0;
	up_state = TSTATE_POST;
	upload_settings.p = config_buf;
	upload_settings.bptr = payload_start;
	st_len = pre_acc;
	if (!upload_go()) {
		s->tstate = TSTATE_NONE;
		send_bad_request();
	}

	dbg_string("Done reading first fragment\n");
}


/* /cmd (net.md item 2): the body is queued in the web command buffer
 * (config_buf), the rx task posts EV_WEBCMD, cli runs it (httpd_webcmd_run)
 * and sets webcmd_done, and the connection's next poll sends the reply (U4: a
 * web command no longer runs in the console's buffer).
 * config_buf and outbuf are safe to hold the command and its reply across
 * steps because uIP has one connection: while it waits in TSTATE_WEBCMD no
 * other request can arrive, and no command touches config_buf. If it ends
 * first, the queued command is cancelled (webcmd_pending cleared) before the
 * next connection can write config_buf (console.md). */
#if UIP_CONNS != 1
#error "/cmd queues its command in config_buf and its reply in outbuf: give it its own buffers for UIP_CONNS > 1"
#endif
__xdata uint8_t webcmd_pending, webcmd_done;

static void webcmd_queue(__xdata uint8_t *body)
{
	if (body != config_buf)
		memcpy(config_buf, body, content_length + 1);	/* with post_body_take's NUL */
	webcmd_done = 0;
	webcmd_pending = 1;
	uip_conn->appstate.tstate = TSTATE_WEBCMD;
}

static void run_cmd_body(__xdata uint8_t *body);

void httpd_webcmd_run(void) __banked
{
	run_cmd_body(config_buf);
	webcmd_done = 1;
}

static void run_cmd_body(__xdata uint8_t *body)
{
	uint16_t hdr_len = strtox(outbuf, HTTP_RESPONSE_TXT);

	slen = hdr_len;
	cmd_capture = 1;
	execute_commands(body);
	if (cmd_capture == 2)
		slen += strtox(outbuf + slen, CMD_TRUNCATED);
	cmd_capture = 0;
	/* Commands that configure something print nothing at all. Saying
	 * so beats an empty body, which reads the same as "nothing ran".
	 * Only on success: a silent failure must not answer with "OK". */
	if (err_status == ERR_OK && slen == hdr_len)
		slen += strtox(outbuf + slen, "OK\n");
	/* On a parse error keep what the parser printed, because that text
	 * is the explanation, and only restate the status. "400 NO" is as
	 * long as "200 OK", so the header does not have to be rebuilt. */
	if (err_status != ERR_OK) {
		outbuf[9] = '4';
		outbuf[10] = '0';
		outbuf[11] = '0';
		outbuf[13] = 'N';
		outbuf[14] = 'O';
	}
}


static void run_login_body(__xdata uint8_t *body)
{
	if (strstart(body, "pwd=") && is_url_word_x(body + 4, passwd)) {
		dbg_string("Password accepted!\n");
		read_reg_timer(&now);
		slen = strtox(outbuf, "HTTP/1.1 302 Found\r\nConnection: close\r\nLocation: index.html\r\n" \
				      "Set-Cookie: session=");
		session_new();
		slen += strtox(outbuf + slen, "; SameSite=Strict\r\n\r\n");
	} else {
		dbg_string("Password invalid!\n");
		slen = strtox(outbuf, "HTTP/1.1 302 Found\r\nConnection: close\r\nLocation: login.html\r\n\r\n");
	}
}


static uint8_t post_body_take(__xdata uint8_t *p)
{
	uint16_t have;

	if (!content_length) {
		send_length_required();
		return 0;
	}
	if (content_length >= CONFIG_UPLOAD_BUF) {
		send_bad_request();
		return 0;
	}
	have = uip_len - (p - (__xdata uint8_t *)uip_appdata);
	if (have >= content_length) {
		p[content_length] = NUL;
		return 1;
	}
	memcpy(config_buf, p, have);
	pre_acc = have;
	timer_set(&postbody_timer, POSTBODY_TIMEOUT);
	uip_conn->appstate.tstate = TSTATE_POSTBODY;
	return 0;
}


static void post_body_continue(void)
{
	uint16_t take;

	// no header scan runs while the body is pending: content_length is this request's
	take = content_length - pre_acc;
	if (take > uip_len)
		take = uip_len;
	memcpy(config_buf + pre_acc, uip_appdata, take);
	pre_acc += take;
	if (pre_acc < content_length) {
		timer_set(&postbody_timer, POSTBODY_TIMEOUT);
		return;
	}
	config_buf[pre_acc] = NUL;
	uip_conn->appstate.tstate = TSTATE_NONE;
	if (postbody_endpoint == POSTBODY_CMD)
		webcmd_queue(config_buf);
	else
		run_login_body(config_buf);
}



/*
 * Buffer an HTTP request header across TCP receive events. Stop copying at
 * CRLFCRLF so any body bytes in the final segment stay in uip_appdata.
 *
 * Return the body offset in the current receive buffer, 0 while incomplete,
 * or 0xffff after sending an error response.
 */
static uint16_t request_header_take(void)
{
	uint16_t i;

	for (i = 0; i < uip_len; i++) {
		if (pre_acc + 1 >= CONFIG_UPLOAD_BUF) {
			uip_conn->appstate.tstate = TSTATE_NONE;
			send_bad_request();
			return 0xffff;
		}
		config_buf[pre_acc++] = ((__xdata uint8_t *)uip_appdata)[i];
		config_buf[pre_acc] = NUL;
		if (pre_acc >= 4
		    && config_buf[pre_acc - 4] == '\r'
		    && config_buf[pre_acc - 3] == '\n'
		    && config_buf[pre_acc - 2] == '\r'
		    && config_buf[pre_acc - 1] == '\n') {
			uip_conn->appstate.tstate = TSTATE_NONE;
			return i + 1;
		}
	}

	timer_set(&postbody_timer, POSTBODY_TIMEOUT);
	uip_conn->appstate.tstate = TSTATE_HEADER;
	return 0;
}

void handle_post(uint16_t body_off)
{
	__xdata struct httpd_state * __xdata s = &(uip_conn->appstate);
	__xdata uint8_t *p = uip_appdata;
	__xdata uint8_t *request_path = p + 6;

	if (s->tstate == TSTATE_POSTBODY) {
		post_body_continue();
		return;
	}

	// Was the multipart header sent in multiple packets?
	if (s->tstate != TSTATE_MULTIPART) {
		p = config_buf;
		request_path = p + 6;

		dbg_string("Is POST\n");
		p += 5;  // Skip post
		// Find end of request path
		while (*p && !is_separator(*p))
			p++;
		*p++ = NUL;

		// Find end of request header
		boundary[0] =NUL;
		p = scan_header(p);
		dbg_string("Boundary: >"); dbg_string_x(boundary); dbg_string("<\n");
		if (!*p || !content_type) {
			dbg_string("Bad Request!\n");
			send_bad_request();
			return;
		}

		pre_acc = 0;
		p = (__xdata uint8_t *)uip_appdata + body_off;
		if (is_word(request_path, "upload")) {
			if (!authenticated) {
				send_unauthorized();
				return;
			}
			if (flash_size < FLASH_NEEDED_FOR_UPDATE)
			{
				print_string("Flash too small for firmware upload!\n");
				send_bad_request();
				return;
			}
			print_string("Firmware upload started.");
			config_upload = 0;
			upload_loader = 0;
			upload_mdata = 0;
			uptr = FIRMWARE_UPLOAD_START;
#ifdef LAYOUT_COMPACT
			upload_end = FIRMWARE_UPLOAD_END + LDR_LOADER_LEN;	/* an install image */
#else
			upload_end = FIRMWARE_UPLOAD_END;
#endif
			verify_crc = 1;
			max_upload = 1024576;
			pre_acc = 0;
		} else if (is_word(request_path, "loader")) {
			if (!authenticated) {
				send_unauthorized();
				return;
			}
			print_string("Loader upload started.");
			config_upload = 0;
			upload_loader = 1;
			upload_mdata = 0;
			uptr = LDR_STAGE;
			upload_end = LDR_STAGE + LDR_LOADER_LEN;
			verify_crc = 1;
			max_upload = LDR_LOADER_LEN + 0x1000;
			pre_acc = 0;
		} else if (is_word(request_path, "mdata")) {
			if (!authenticated) {
				send_unauthorized();
				return;
			}
			/* only the compact layout has the region, on a chip big enough */
#ifdef LAYOUT_COMPACT
			if (!flash_size)
				flash_read_jedecid();
			if (flash_size < MDATA_START + MDATA_LEN) {
#endif
				print_string("No machine data region on this switch\n");
				send_bad_request();
				return;
#ifdef LAYOUT_COMPACT
			}
			print_string("Machine data upload started.");
			config_upload = 0;
			upload_loader = 0;
			upload_mdata = 1;
			uptr = MDATA_START;
			upload_end = MDATA_START + MDATA_LEN;
			verify_crc = 1;
			max_upload = MDATA_LEN + 0x1000;
			pre_acc = 0;
#endif
		} else if (is_word(request_path, "config")) {
			if (!authenticated) {
				send_unauthorized();
				return;
			}
			dbg_string("Configuration upload\n");
			verify_crc = 0;
			config_upload = 1;
			// config_take() matches CRLF-prefixed delimiters, including the first.
			config_buf[0] = '\r';
			config_buf[1] = '\n';
			pre_acc = 2;
		}
		// Check for other POST requests, which are not multipart, below
	} else {
		dbg_string("Multipart request\n");
	}

	if (s->tstate == TSTATE_MULTIPART || is_word(request_path, "upload") || is_word(request_path, "loader")
	    || is_word(request_path, "mdata")
	    || is_word(request_path, "config")) {
		dbg_string("POST upload/config request\n");
		if (!authenticated) {
			send_unauthorized();
			return;
		}
		if (!boundary[0]) {
			dbg_string("Bad request, no boundary!\n");
			send_bad_request();
			return;
		}
		if (config_upload)
			handle_config_fragment(p);
		else
			handle_firmware_fragment(p);
		return;
	} else if (is_word(request_path, "cmd")) {
		if (!authenticated) {
			send_unauthorized();
			return;
		}
		postbody_endpoint = POSTBODY_CMD;
		if (!post_body_take(p))
			return;
		webcmd_queue(p);
		return;
	} else if (is_word(request_path, "login")) {
		dbg_string("POST login\n");

		if (!content_type || !is_word(content_type, "application/x-www-form-urlencoded")) {
			dbg_string("Bad request!\n");
			send_bad_request();
			return;
		}

		postbody_endpoint = POSTBODY_LOGIN;
		if (!post_body_take(p))
			return;
		run_login_body(p);
		return;
	} else {
		send_not_found();
		return;
	}
}


static void tx_send(uint16_t len)
{
	sent_len = len;
	uip_send(outbuf + o_idx, len);
}


/* The response in outbuf: its first segment */
static void send_outbuf(void)
{
	dbg_string("slen: "); dbg_short(slen); dbg_char('\n');
	o_idx = 0;
	if (slen > uip_mss()) {
		dbg_string("Sending a: "); dbg_short(slen); dbg_char('\n');
		tx_send(uip_mss());
		dbg_string("Sending a done\n");
	} else {
		dbg_string("Sending b: "); dbg_short(slen); dbg_char('\n');
		tx_send(slen);
		dbg_string("Sending b done\n");
	}
	uip_conn->appstate.tstate = TSTATE_TX;
}

void httpd_appcall(void)
{
	__xdata struct httpd_state * __xdata s = &(uip_conn->appstate);

	dbg_char('P');
#ifdef DEBUG
	if (uip_newdata())
		write_char('N');
	print_byte(s->tstate);
	write_char(' ');
#endif
	if(uip_connected()) {
		dbg_string("Connected...\n");
		s->tstate = TSTATE_NONE;
		uip_rcv_wnd = UIP_TCP_MSS;
		s->fw_reset_pending = 0; // explicitly initialize reused XDATA appstate
		up_active = 0;
		/* the job still writes from config_buf for a connection that has
		 * ended: no request may land there until it is done */
		if (fl_kind) {
			uip_abort();
			s->tstate = TSTATE_CLOSED;
		}
	} else if (uip_closed()) {
		dbg_string("Connection closed\n");
		if (s->tstate == TSTATE_WEBCMD)
			webcmd_pending = webcmd_done = 0;	/* nobody waits for it */
		up_active = 0;
		s->tstate = TSTATE_CLOSED;
		if (s->fw_reset_pending && uip_acked())
			reset_chip();
	} else if (uip_aborted() || uip_timedout()) {
		dbg_string("Connection aborted\n");
		if (s->tstate == TSTATE_WEBCMD)
			webcmd_pending = webcmd_done = 0;
		up_active = 0;
		uip_close();
		s->tstate = TSTATE_CLOSED;
	} else if (uip_poll()) {
		uip_len = 0;
		if (s->tstate == TSTATE_UPLOAD && fl_finished) {
			/* the flash job has written all of the stage */
			fl_finished = 0;
			s->tstate = up_state;
			if (up_reset)
				s->fw_reset_pending = up_reset;
			if (up_short)		/* the window was short: an update */
				uip_flags |= UIP_NEWDATA;
			up_short = 0;
			if (up_state != TSTATE_POST)
				up_active = 0;	/* the part has ended */
			if (up_state != TSTATE_POST && slen)
				send_outbuf();
		} else if (s->tstate == TSTATE_WEBCMD && webcmd_done) {
			webcmd_done = 0;
			send_outbuf();
		} else if (s->tstate == TSTATE_ACKED) {
			dbg_string("Closing because everything has been transmitted\n");
			uip_close();
			s->tstate = TSTATE_CLOSED;
		} else if ((s->tstate == TSTATE_POSTBODY || s->tstate == TSTATE_HEADER
			    || s->tstate == TSTATE_POST)
			   && timer_expired(&postbody_timer)) {
			/* POST: an upload whose body stopped coming (UPLOAD_TIMEOUT),
			 * which would hold other flash jobs off for ever */
			dbg_string("HTTP request never completed\n");
			uip_abort();
			up_active = 0;
			s->tstate = TSTATE_CLOSED;
		}
	} else if (uip_acked() && s->tstate == TSTATE_TX) {
		dbg_string("ACK\n");
		if (slen > sent_len) {
			slen -= sent_len;
			o_idx += sent_len;
		} else {
			slen = 0;
		}

		s->tstate = TSTATE_ACKED;

		if (slen > uip_mss()) {
			dbg_string("Sending A: "); dbg_short(slen); dbg_char('\n');
			tx_send(uip_mss());
			s->tstate = TSTATE_TX;
		} else if (slen > 0) {
			dbg_string("Sending B: "); dbg_short(slen); dbg_char('\n');
			tx_send(slen);
			s->tstate = TSTATE_TX;
		} else if (cont_len) {
			dbg_string("CONT cont_len: "); dbg_short(cont_len);
			slen = cont_len > uip_mss() ? uip_mss() : cont_len;
			if (slen > TCP_OUTBUF_SIZE)
				slen = TCP_OUTBUF_SIZE;
			flash_region.addr = cont_addr;
			flash_region.len = slen;
			flash_read_bulk(outbuf);
			o_idx = 0;
			tx_send(slen);
			cont_len -= slen;
			cont_addr += slen;
			s->tstate = TSTATE_TX;
		} else if (s->fw_reset_pending) {
			// The upload verdict has been fully ACKed by the client;
			// now it is safe to reset and apply the staged image
			if (s->fw_reset_pending != FW_UPDATE_PENDING || !upload_loader || loader_update()) {
				print_string("Resetting to apply update\n");
				reset_chip();
			}
			s->fw_reset_pending = 0;
		}
	} else if (uip_newdata() && s->tstate == TSTATE_WEBCMD) {
		/* A trailing CRLF or a pipelined request while the command waits
		 * in config_buf: dropped, as today's code drops it while the reply
		 * is sent (TSTATE_TX). Taking it would overwrite the command. */
		uip_len = 0;
	} else if (uip_newdata() && s->tstate == TSTATE_UPLOAD
		   && (fl_kind == FL_CONFIG || (!fl_kind && up_state != TSTATE_POST))) {
		/* a config save being written, or a part that has ended, its
		 * answer not sent yet: dropped, as behind a queued /cmd */
		uip_len = 0;
	} else if (uip_newdata() && (s->tstate == TSTATE_POST || s->tstate == TSTATE_UPLOAD)) {
		/* into the stage, behind what the job still has to write */
		if (config_upload || uip_len <= max_upload) {
			if (!config_upload)
				max_upload -= uip_len;
			if (upload_take()) {
				uip_len = 0;
				return;
			}
			s->tstate = TSTATE_NONE;
			goto do_send;
		} else {
			send_bad_request();
			goto do_send;
		}
	} else if (uip_newdata() && s->tstate != TSTATE_TX) {
		cont_len = 0;
		dbg_char('<'); dbg_short(uip_len); dbg_char('\n');
		__xdata uint8_t *p = uip_appdata;
		uint16_t body_off = 0;
		// Mark end of request header with \0
		p[uip_len] = 0;
#ifdef DEBUG
		while (*p)
			dbg_char(*p++);
		dbg_char('\n');
#endif
		p = uip_appdata;
		if (s->tstate != TSTATE_MULTIPART && s->tstate != TSTATE_POSTBODY) {
			if (s->tstate != TSTATE_HEADER)
				pre_acc = 0;
			body_off = request_header_take();
			if (!body_off) {
				uip_len = 0;
				return;
			}
			if (body_off == 0xffff)
				goto do_send;
		}

		if (s->tstate == TSTATE_MULTIPART || s->tstate == TSTATE_POSTBODY
		    || is_word(config_buf, "POST")) {
			handle_post(body_off);
			// If this is an ongoing post stream, then wait for the next packet
			if (s->tstate == TSTATE_POST || s->tstate == TSTATE_MULTIPART
			    || s->tstate == TSTATE_POSTBODY || s->tstate == TSTATE_WEBCMD
			    || s->tstate == TSTATE_UPLOAD) {
				uip_len = 0;
				return;
			}
			goto do_send;
		}

		p = config_buf;
		if (!is_word(p, "GET")) {
			send_bad_request();
			goto do_send;
		}
		dbg_string("GET request ");
		p += 4;
		scan_header(p);
		__xdata uint8_t *q = p;
		while (*p && !is_separator(*p))
			p++;
		*p = NUL;
		dbg_string_x(q);
		dbg_char('\n');

		s->tstate = TSTATE_NONE;
		entry = find_entry(q);
		dbg_string("Entry is: "); dbg_byte(entry); dbg_char('\n');
		if (entry == 0xff) {
			if (!authenticated) {
				dbg_string("Not authorized!\n");
				send_unauthorized();
				goto do_send;
			}
			dbg_string("Not file entry\n");
			if (!strcmp(q, "/ports.json")) {
				send_ports();
			} else if (!strcmp(q, "/traffic.json")) {
				send_traffic();
			} else if (!strcmp(q, "/information.json")) {
				send_basic_info();
			} else if (!strcmp(q, "/vlan.json")) {
				parse_short(q + 15);
				send_vlan(short_parsed);
			} else if (is_word(q, "/counters.json")) {
				uint8_t cport = q[20] - '0';
				if (send_counters(cport))
					send_bad_request();
			} else if (is_word(q, "/eee.json")) {
				send_eee();
			} else if (is_word(q, "/bandwidth.json")) {
				send_bandwidth();
			} else if (is_word(q, "/storm.json")) {
				send_storm();
			} else if (is_word(q, "/l2.json")) {
				parse_short(q + 13); // e.g.: /l2.json?idx=10
				send_l2(short_parsed);
			} else if (is_word(q, "/l2_del.json")) {
				parse_short(q + 17);
				l2_delete(short_parsed);
			} else if (is_word(q, "/mirror.json")) {
				send_mirror();
			} else if (is_word(q, "/isolation.json")) {
				send_isolation();
			} else if (is_word(q, "/lag.json")) {
				send_lag();
			} else if (is_word(q, "/stp.json")) {
				send_stp();
			} else if (is_word(q, "/lldp.json")) {
				send_lldp();
			} else if (is_word(q, "/loop.json")) {
				send_loop();
			} else if (is_word(q, "/stpcnt.json")) {
				send_stp_counters();
			} else if (is_word(q, "/vlanlist.json")) {
				send_vlanlist();
			} else if (is_word(q, "/config")) {
				send_config();
			} else if (is_word(q, "/cmd_log")) {
				send_cmd_log();
			} else if (is_word(q, "/cmd_log_clear")) {
				clear_command_history();
				slen = strtox(outbuf, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
			} else if (is_word(q, "/reset")) {
				slen = strtox(outbuf, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
				s->fw_reset_pending = FW_RESET_PENDING;
			} else {
				send_not_found();
			}
		} else {
			dbg_string("Have entry, authenticated: "); dbg_byte(authenticated); dbg_char('\n');
			if (!authenticated && !(f_data[entry].start == FDATA_START_login_html
						|| f_data[entry].start == FDATA_START_favicon_ico)) {
				send_to_login();
				goto do_send;
			}
			slen = strtox(outbuf, "HTTP/1.1 200 OK\r\nContent-Type: ");
			slen += strtox(outbuf + slen, mime_strings[f_data[entry].mime]);
			/* 'unsafe-inline' is needed for the inline onclick handlers
			 * and the inline <script> on login.html. Connection: close is
			 * required because this httpd closes the connection after every
			 * response; without advertising it a browser reuses the socket
			 * from its keep-alive pool and the next request hits the already
			 * closed connection (a POST is then dropped without a retry). */
			slen += strtox(outbuf + slen, "; charset=UTF-8\r\n");
			if (f_data[entry].gzip)
				slen += strtox(outbuf + slen, "Content-Encoding: gzip\r\n");
			slen += strtox(outbuf + slen, "Cache-Control: max-age=60, must-revalidate\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\nContent-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; form-action 'self'\r\n\r\n");

			len_left = f_data[entry].len;
			if (len_left > (TCP_OUTBUF_SIZE - slen)) {
				cont_len = len_left - (TCP_OUTBUF_SIZE - slen);
				len_left = TCP_OUTBUF_SIZE - slen;
				cont_addr = f_data[entry].start + len_left;
			}
			dbg_string("MIME: "); dbg_string(mime_strings[f_data[entry].mime]); dbg_char('\n');
			flash_region.addr = f_data[entry].start;
			flash_region.len = len_left;
			flash_read_bulk(outbuf + slen);
			slen += len_left;
		}
do_send:
		send_outbuf();
	} else if (uip_rexmit()) { // Connection established, need to rexmit?
		dbg_string("RETRANSMIT requested\n");
		if (sent_len) {
			dbg_string("Sending C: "); dbg_short(sent_len); dbg_char('\n');
			tx_send(sent_len);
			dbg_string("Sending C done\n");
		}
		s->tstate = TSTATE_TX;
		uip_len = 0;
	} else {
		uip_len = 0;
	}
}
