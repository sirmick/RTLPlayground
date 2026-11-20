/*
 * Host-side bench for the httpd transmit and firmware upload paths.
 *
 * Compiles the UNMODIFIED httpd/httpd.c and uip/uip.c and drives them through a
 * real handshake, GETs with moving receive windows, and multipart POSTs.
 * Everything below the two modules - flash, console, JSON pages - is mocked
 * here, so the bench observes the wire response and the bytes staged to flash.
 *
 * The file served out of the simulated flash carries a position-dependent
 * pattern, so a duplicated or skipped range shows up as a mismatch at a known
 * offset instead of an anonymous "content differs".
 *
 * Run: make -C test    (exit code 0 = all scenarios pass)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Some hosts ship these in <sys/_endian.h>, and they assign to their argument;
 * uip.h refuses to be included next to them. The firmware definitions are the
 * ones this bench needs, so the host macros go first. */
#undef HTONS
#undef NTOHS

#include "httpd.h"
#include "uip.h"
#include "rtl837x_common.h"
#include "rtl837x_regs.h"
#include "rtl837x_flash.h"
#include "page_impl.h"
#include "html_data.h"
#include "../loader/layout.h"
#include "flash_job.h"
#include "crash.h"

/* Private to uip.c, and the client needs them to build segments. */
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define TCPH		((struct uip_tcpip_hdr *)&uip_buf[UIP_LLH_LEN])

#define MSS_FULL	1460
#define SMALL_WINDOW	600

/* httpd.c serves this one file without a session, which keeps the bench clear
 * of the login machinery: what is under test is the transmit path. */
#define FILE_START	FDATA_START_login_html
#define FILE_NAME	"/login.html"
#define FILE_LEN	6000
#define PROTECTED_FILE_NAME	"/index.html"
#define PROTECTED_FILE_START	(FILE_START + FILE_LEN + 64)
#define PROTECTED_FILE_LEN	64
#define STREAM_MAX	16384

static int failures;
static int verbose;

#define CHECK(cond, name) do { \
	if (cond) printf("PASS  %s\n", name); \
	else { printf("FAIL  %s\n", name); failures++; } \
} while (0)

static uint8_t pattern(uint32_t addr)
{
	return (uint8_t)((addr * 7u) + (addr >> 8));
}

/* ---- firmware environment below httpd.c --------------------------------- */

volatile uint8_t sfr_data[4];
volatile uint32_t ticks;
uint8_t cmd_capture;
uint8_t err_status;
const uint8_t * const hex = (const uint8_t *)"0123456789abcdef";
uint16_t crc_value;
const uint8_t * const HTTP_RESPONSE_TXT = (const uint8_t *)"HTTP/1.1 200 OK\r\n\r\n";
uint32_t flash_size = 0x80000;
uint8_t flash_buf[FLASH_BUF_SIZE];
uint8_t rx_headers[16];
struct flash_region_t flash_region;

static uint8_t written_config[CONFIG_LEN];
static uint16_t written_config_len;
static uint32_t config_erase_addr, config_write_addr;
static int config_erases, config_writes;
static uint8_t flash_written[8192];
static size_t flash_written_len;
static int flash_write_bad;

const char * const mime_strings[] = { "text/html", "image/svg+xml", "image/x-icon",
			 "image/png", "text/javascript", "text/css", "text/plain" };

const struct f_data f_data[] = {
	{ FILE_NAME, FILE_START, FILE_LEN, mime_HTML, 0 },
	{ PROTECTED_FILE_NAME, PROTECTED_FILE_START, PROTECTED_FILE_LEN, mime_HTML, 0 },
	{ 0, 0, 0, mime_HTML, 0 },
};

void flash_read_bulk(uint8_t *dst)
{
	for (uint16_t i = 0; i < flash_region.len; i++)
		dst[i] = pattern(flash_region.addr + i);
}

void flash_init(uint8_t enable_dio) { (void)enable_dio; }
static uint32_t erase_log[8];
static int erase_n;

void flash_sector_erase(void)
{
	config_erases++;
	config_erase_addr = flash_region.addr;
	if (erase_n < 8)
		erase_log[erase_n++] = flash_region.addr;
}

void flash_write_bytes(uint8_t *ptr)
{
	if (flash_region.addr >= CONFIG_START && flash_region.addr < CONFIG_START + CONFIG_LEN) {
		config_writes++;
		config_write_addr = flash_region.addr;
		written_config_len = flash_region.len;
		if (written_config_len <= sizeof(written_config))
			memcpy(written_config, ptr, written_config_len);
		return;
	}
	if (flash_region.addr != FIRMWARE_UPLOAD_START + flash_written_len ||
	    flash_region.len > sizeof(flash_written) - flash_written_len) {
		flash_write_bad = 1;
		return;
	}
	memcpy(flash_written + flash_written_len, ptr, flash_region.len);
	flash_written_len += flash_region.len;
}
const char *get_flash_size_str(void) { return "512 kB"; }
void flash_read_jedecid(void) { }
void crc16_bank1(uint8_t *v) { (void)v; }
/* The flash read-back of a finished upload comes to readback_crc. */
static uint16_t readback_crc = 0xb001;
uint32_t flash_crc16_from, flash_crc16_to;
uint16_t flash_crc16_range(void) { return readback_crc; }
uint16_t flash_crc16_more(void) { crc_value = readback_crc; return crc_value; }
/* machine data: none in this (standard-layout) build */
uint32_t mdata_at;
uint32_t mdata_len_at(void) { return 0; }
static unsigned reset_calls;
static uint8_t reset_cause;
static int stream_len, reset_len;	/* the bytes the client had at the reset */
void reset_chip(uint8_t cause) { reset_calls++; reset_cause = cause; reset_len = stream_len; }
uint8_t crash_pending;

/* The flash job (flash_job.c), the bench's: it runs at once, through the
 * same calls (httpd.c's upload_prog() and upload_verify() for an upload; the
 * sector erase and the copy for a config save). The connection's next poll
 * finds it done and reopens the window. */
uint8_t fl_req, fl_kind, fl_finished;
uint8_t *fl_src;
uint16_t fl_len;
uint32_t fl_addr;
static int job_busy;		/* a scenario's: the job refuses */
static int job_defer;		/* a scenario's: the job is claimed but runs at job_run() */

static void job_upload(void)
{
	uint8_t r;

	do {
		do {
			r = upload_prog();
			if (r == UP_ERASE) {
				flash_region.addr = fl_addr;
				flash_sector_erase();
			}
		} while (r == UP_PROG || r == UP_ERASE);
		while (r == UP_VERIFY)
			r = upload_verify();
	} while (upload_done());
}

uint8_t flash_job_start(void)
{
	if (job_busy || fl_kind || (fl_req != FL_UPLOAD && up_active))
		return 0;
	fl_finished = 0;
	if (job_defer) {
		fl_kind = fl_req;
		return 1;
	}
	if (fl_req == FL_UPLOAD) {
		job_upload();
	} else if (fl_req == FL_CONFIG) {
		flash_region.addr = CONFIG_START;
		flash_sector_erase();
		flash_region.addr = CONFIG_START;
		flash_region.len = fl_len;
		flash_write_bytes(fl_src);
	}
	fl_finished = 1;
	return 1;
}

/* one step of a deferred job, as the scheduler would run it: FL_PROG (or
 * FL_ERASE), FL_VERIFY, then FL_DONE a step of its own, so a segment can
 * come between the job's last write and its end */
static uint8_t job_vf;		/* 0 FL_PROG, 1 FL_VERIFY, 2 FL_DONE */

static void job_step(void)
{
	uint8_t r;

	if (!fl_kind)
		return;
	if (job_vf == 2) {
		job_vf = 0;
		if (fl_kind < FL_FACTORY && upload_done()) {	/* as flash_job.c fl_end() */
			fl_kind = FL_UPLOAD;
			return;
		}
		fl_kind = 0;
		fl_finished = 1;
		return;
	}
	r = job_vf ? upload_verify() : upload_prog();
	if (r == UP_ERASE) {
		flash_region.addr = fl_addr;
		flash_sector_erase();
	} else if (r == UP_VERIFY) {
		job_vf = 1;
	} else if (r == UP_DONE) {
		job_vf = 2;
	}
}

/* a deferred job runs now, to its end */
static void job_run(void)
{
	while (fl_kind)
		job_step();
}
static bool loader_update_ok = true;
static int loader_update_calls;
bool loader_update(void) { loader_update_calls++; return loader_update_ok; }
static bool loader_present_ok = true;
bool loader_present(void) { return loader_present_ok; }
static bool loader_fits_ok = true;
bool loader_stage_fits(void) { return loader_fits_ok; }
static int stage_drops, convert_arms;
void loader_stage_drop(void) { stage_drops++; }
void loader_convert_arm(void) { convert_arms++; }
void delay(uint16_t t) { (void)t; }
void write_char(char c) { (void)c; }
void write_char_no_syslog(char c) { (void)c; }
void itoa_no_syslog(uint8_t v) { (void)v; }
void print_string(const char *p) { (void)p; }
void print_string_no_syslog(const char *p) { (void)p; }
void print_string_newline_no_syslog(const char *p) { (void)p; }
void set_sys_led_state(uint8_t state) { (void)state; }
void cmd_parser(void) { }
void execute_config(void) { }
void exec_begin(uint8_t *p) { (void)p; }
static int history_clears;
void clear_command_history(void) { history_clears++; }
void udp_callbacks(void) { }
void tcpip_output(void) { }
void get_random_32(void) { }
static uint32_t fake_now;
void read_reg_timer(uint32_t *tmr) { *tmr = fake_now; }

/* uip-conf.h asks uIP not to define the packet buffer: on the switch it lives
 * at a fixed XDATA address, so the bench provides the storage itself. */
u8_t uip_buf[UIP_CONF_BUFFER_ALLOC_SIZE];

uint16_t strlen_x(const char *s) { return (uint16_t)strlen(s); }

uint16_t strtox(uint8_t *dst, const char *s)
{
	uint16_t n = 0;

	while (s[n]) { dst[n] = (uint8_t)s[n]; n++; }
	return n;
}

void memcpyc(uint8_t *dst, const uint8_t *src, uint16_t len) { memcpy(dst, src, len); }

bool strstart(const uint8_t *a, const uint8_t *b)
{
	while (*b) { if (*a++ != *b++) return false; }
	return true;
}

bool strstart_x(const uint8_t *a, const uint8_t *b) { return strstart(a, b); }

bool send_counters(uint8_t phys_port) { (void)phys_port; return false; }
void send_ports(void) { }
void send_traffic(void) { }
void send_vlan(uint16_t vlan) { (void)vlan; }
void send_bandwidth(void) { }
void send_storm(void) { }
void send_isolation(void) { }
void send_eee(void) { }
void send_l2(uint16_t idx) { (void)idx; }
static int l2_deleted = -1;
void l2_delete(uint16_t idx) { l2_deleted = idx; }
void send_mirror(void) { }
void send_config(void) { }
void send_cmd_log(void) { }
void send_lag(void) { }
void send_stp(void) { }
void send_stp_counters(void) { }
void send_vlanlist(void) { }
void send_lldp(void) {}
void send_loop(void) { }

extern uint8_t authenticated;
extern char session_id[];
extern uint32_t last_session_use[];
extern char passwd[21];
extern uint16_t session_timeout;

/* uip.c defines the listen table but no header declares it; the bench reads it
 * to confirm the port httpd_init() asked for is the one uIP is watching. */
extern u16_t uip_listenports[UIP_LISTENPORTS];

/* ---- the simulated client ----------------------------------------------- */

static uint32_t cli_seq;	/* next sequence number we send */
static uint32_t cli_rcv_nxt;	/* next sequence number we expect */
static uint16_t cli_window;	/* what we advertise */

static uint8_t stream[STREAM_MAX];	/* bytes accepted from the server */
static int stream_len;

static uint8_t last_tx[MSS_FULL + 64];	/* payload of the last server segment */
static uint16_t srv_wnd;		/* the window the last server segment advertised */
static uint8_t srv_flags;		/* and its flags */
static uint32_t srv_ack;		/* and its ACK */
static int edge_watch, edge_bad;	/* each ACK's right edge (ack + window) checked */
static uint32_t edge_last;
static int last_tx_len;

static uint16_t hton16(uint16_t v)
{
	return (uint16_t)((v << 8) | (v >> 8));
}

static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static uint32_t rd32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | p[3];
}

static void trace(const char *tag)
{
	if (!verbose)
		return;
	printf("      [%-8s] uip_len=%-5u flags=0x%02x seq=%-6u ack=%-6u | conn: state=0x%02x len=%-5u mss=%-5u tstate=%u\n",
	       tag, uip_len,
	       uip_len ? TCPH->flags : 0,
	       uip_len ? rd32(TCPH->seqno) : 0,
	       uip_len ? rd32(TCPH->ackno) : 0,
	       uip_conns[0].tcpstateflags, uip_conns[0].len, uip_conns[0].mss,
	       uip_conns[0].appstate.tstate);
}

/* Takes in one outgoing segment, if uIP produced one. */
static int harvest(void)
{
	uint8_t *payload;
	uint32_t seq;
	int plen, hlen;

	if (uip_len == 0)
		return -1;

	srv_wnd = (uint16_t)(TCPH->wnd[0] << 8 | TCPH->wnd[1]);
	srv_ack = rd32(TCPH->ackno);
	if (edge_watch && (TCPH->flags & TCP_ACK)) {
		uint32_t e = srv_ack + srv_wnd;

		if (edge_last && e < edge_last)
			edge_bad++;
		edge_last = e;
	}
	srv_flags = TCPH->flags;
	seq = rd32(TCPH->seqno);
	/* The SYNACK carries the MSS option, so the header is not always 20 B. */
	hlen = (TCPH->tcpoffset >> 4) * 4;
	payload = &uip_buf[UIP_LLH_LEN + 20 + hlen];
	plen = (int)uip_len - 20 - hlen;
	if (plen < 0)
		plen = 0;

	if (plen > 0) {
		last_tx_len = plen > (int)sizeof(last_tx) ? (int)sizeof(last_tx) : plen;
		memcpy(last_tx, payload, last_tx_len);

		/* A real client keeps what continues the stream and drops the
		 * rest, so bytes sent twice under new sequence numbers land in
		 * the file just as they would in a browser. */
		if (seq == cli_rcv_nxt) {
			if (stream_len + plen <= STREAM_MAX) {
				memcpy(stream + stream_len, payload, plen);
				stream_len += plen;
			}
			cli_rcv_nxt += plen;
		}
		if (verbose)
			printf("      server -> %d B, seq %u\n", plen, seq);
	}
	if (TCPH->flags & TCP_SYN)
		cli_rcv_nxt = seq + 1;

	uip_len = 0;
	return plen;
}

static void client_send(uint8_t flags, const char *payload, int plen)
{
	int hlen = 20;

	/* A SYN carries the MSS option, as every real client does: uIP takes the
	 * connection MSS from it, and without one the server has nothing to send
	 * with. */
	if (flags & TCP_SYN)
		hlen = 24;

	memset(uip_buf, 0, UIP_LLH_LEN + 20 + hlen + (plen > 0 ? plen : 0));
	TCPH->vhl = 0x45;
	TCPH->tos = 0;
	TCPH->len[0] = (uint8_t)((20 + hlen + plen) >> 8);
	TCPH->len[1] = (uint8_t)((20 + hlen + plen) & 0xff);
	TCPH->ttl = 64;
	TCPH->proto = UIP_PROTO_TCP;
	TCPH->ipchksum = 0;
	TCPH->srcipaddr[0] = hton16(0x0a00); TCPH->srcipaddr[1] = hton16(0x0002);
	TCPH->destipaddr[0] = hton16(0x0a00); TCPH->destipaddr[1] = hton16(0x0001);
	TCPH->srcport = hton16(40000);
	TCPH->destport = hton16(80);
	wr32(TCPH->seqno, cli_seq);
	wr32(TCPH->ackno, cli_rcv_nxt);
	TCPH->tcpoffset = (uint8_t)((hlen / 4) << 4);
	TCPH->flags = flags;
	TCPH->wnd[0] = (uint8_t)(cli_window >> 8);
	TCPH->wnd[1] = (uint8_t)(cli_window & 0xff);
	TCPH->tcpchksum = 0;

	if (hlen == 24) {
		uint8_t *opt = &uip_buf[UIP_LLH_LEN + 40];

		opt[0] = 2; opt[1] = 4;			/* kind = MSS, length 4 */
		opt[2] = MSS_FULL >> 8; opt[3] = MSS_FULL & 0xff;
	}
	if (plen > 0)
		memcpy(&uip_buf[UIP_LLH_LEN + 20 + hlen], payload, plen);

	uip_len = 20 + hlen + plen;
	uip_input();
	trace("input");
	cli_seq += plen;
	if (flags & TCP_SYN)
		cli_seq++;
	harvest();
}

static void client_ack(uint16_t window)
{
	cli_window = window;
	client_send(TCP_ACK, NULL, 0);
}

static void run_periodic(int rounds)
{
	for (int i = 0; i < rounds; i++) {
		uip_periodic(0);
		trace("timer");
		harvest();
	}
}

static void session_start(uint16_t window)
{
	uip_init();
	httpd_init();
	authenticated = 1;

	cli_seq = 1000;
	cli_rcv_nxt = 0;
	cli_window = window;
	stream_len = 0;
	last_tx_len = 0;

	client_send(TCP_SYN, NULL, 0);
	client_ack(window);
}

static void request_file(uint16_t window)
{
	static const char get[] = "GET " FILE_NAME " HTTP/1.1\r\nHost: sw\r\n\r\n";

	cli_window = window;
	client_send(TCP_ACK | TCP_PSH, get, (int)strlen(get));
}

/* Drains the response, acknowledging every segment with the given window. */
static void drain(uint16_t window)
{
	for (int i = 0; i < 20 && stream_len < STREAM_MAX; i++) {
		client_ack(window);
		run_periodic(1);
	}
}

static int body_offset(void)
{
	for (int i = 0; i + 4 <= stream_len; i++)
		if (!memcmp(stream + i, "\r\n\r\n", 4))
			return i + 4;
	return -1;
}

/* Offset of the first body byte that is not the one the file holds there, or
 * -1 when the body matches, or -2 when no header ever arrived. */
static int first_body_mismatch(void)
{
	int off = body_offset();

	if (off < 0)
		return -2;
	for (int i = 0; i < FILE_LEN && off + i < stream_len; i++)
		if (stream[off + i] != pattern(FILE_START + i))
			return i;
	return -1;
}

static int stream_contains(const char *needle)
{
	size_t n = strlen(needle);

	for (int i = 0; i + (int)n <= stream_len; i++)
		if (!memcmp(stream + i, needle, n))
			return 1;
	return 0;
}

static void login_session_start(void)
{
	session_start(MSS_FULL);
	strcpy(passwd, "1234");
}

static void report(const char *tag)
{
	if (verbose)
		printf("      %s: header %d B, stream %d B, first mismatch %d\n",
		       tag, body_offset(), stream_len, first_body_mismatch());
}

/* ---- scenarios ---------------------------------------------------------- */

/* Instrument check: with a window that never moves, the MSS at ACK time and the
 * length sent earlier are the same number, so the file must arrive intact. If
 * this one fails, the bench is wrong - not the firmware. */
static void scenario_steady_window(void)
{
	session_start(MSS_FULL);
	if (verbose)
		printf("      listen port=0x%04x, initial mss=%u\n",
		       uip_listenports[0], uip_conns[0].initialmss);
	request_file(MSS_FULL);
	drain(MSS_FULL);
	report("steady");

	CHECK(body_offset() >= 0, "control: response carries an HTTP header");
	CHECK(stream_len >= body_offset() + FILE_LEN,
	      "control: the whole file arrives");
	CHECK(first_body_mismatch() == -1,
	      "control: file content without duplicates or gaps");
}

/* The client stops draining, so the window in the ACK is smaller than the
 * segment that ACK covers. */
static void scenario_shrinking_window(void)
{
	session_start(MSS_FULL);
	request_file(MSS_FULL);
	drain(SMALL_WINDOW);
	report("shrinking");

	CHECK(body_offset() >= 0,
	      "shrinking window: response carries an HTTP header");
	CHECK(stream_len >= body_offset() + FILE_LEN,
	      "shrinking window: the whole file arrives");
	CHECK(first_body_mismatch() == -1,
	      "shrinking window: file content without duplicates or gaps");
}

/* The client catches up, so the window in the ACK is larger than the segment
 * that ACK covers. */
static void scenario_growing_window(void)
{
	session_start(SMALL_WINDOW);
	request_file(SMALL_WINDOW);
	drain(MSS_FULL);
	report("growing");

	CHECK(stream_len >= body_offset() + FILE_LEN,
	      "growing window: the whole file arrives");
	CHECK(first_body_mismatch() == -1,
	      "growing window: file content without duplicates or gaps");
}

/* A segment is lost, a window update shrinks the window while it is still
 * unacknowledged, and the retransmission timer fires. */
static void scenario_rexmit_after_shrink(void)
{
	uint8_t original[MSS_FULL + 64];
	int original_len, same = 1;

	session_start(MSS_FULL);
	request_file(MSS_FULL);

	original_len = last_tx_len;
	memcpy(original, last_tx, original_len);

	/* The segment never arrived: take it back out of the client's stream and
	 * send a pure window update, which acknowledges nothing. */
	if (original_len > 0 && stream_len >= original_len) {
		stream_len -= original_len;
		cli_rcv_nxt -= original_len;
	}
	cli_window = SMALL_WINDOW;
	client_send(TCP_ACK, NULL, 0);

	memset(&uip_buf[UIP_LLH_LEN + 40], 0xaa, MSS_FULL);

	last_tx_len = 0;
	run_periodic(UIP_RTO + 2);

	if (last_tx_len != original_len || memcmp(last_tx, original, original_len))
		same = 0;

	if (verbose)
		printf("      first %d B, retransmitted %d B\n",
		       original_len, last_tx_len);

	CHECK(original_len > 0, "retransmission: the first segment went out");
	CHECK(same, "retransmission: the repeat carries the same bytes");
}

static void scenario_bad_l4_checksum(void)
{
	uip_stats_t chkerr_before;
	uint32_t seq_before;
	int len_before;

	session_start(MSS_FULL);

	chkerr_before = uip_stat.tcp.chkerr;
	len_before = stream_len;
	seq_before = cli_seq;

	rx_headers[1] = RX_TAG_L4_CSUM_BAD;
	request_file(MSS_FULL);
	rx_headers[1] = 0;
	cli_seq = seq_before;

	CHECK(uip_stat.tcp.chkerr == chkerr_before + 1,
	      "bad checksum: the segment is counted as a checksum error");
	CHECK(stream_len == len_before,
	      "bad checksum: the request draws no reply");

	request_file(MSS_FULL);
	drain(MSS_FULL);
	report("bad checksum");

	CHECK(body_offset() >= 0,
	      "bad checksum control: the same request with a good flag is served");
	CHECK(first_body_mismatch() == -1,
	      "bad checksum control: the file that follows is intact");
}

/* Lay a bare IPv4/TCP header into uip_buf with a chosen data offset and option
 * bytes, so a malformed header can be fed to uip_input() directly. */
static void put_tcp(uint8_t flags, uint8_t offset_words, const uint8_t *opt, int optlen,
		    uint32_t seq, uint16_t total_len)
{
	memset(uip_buf, 0, UIP_LLH_LEN + 20 + 20 + (optlen > 0 ? optlen : 0));
	TCPH->vhl = 0x45;
	TCPH->len[0] = (uint8_t)(total_len >> 8);
	TCPH->len[1] = (uint8_t)(total_len & 0xff);
	TCPH->ttl = 64;
	TCPH->proto = UIP_PROTO_TCP;
	TCPH->srcipaddr[0] = hton16(0x0a00); TCPH->srcipaddr[1] = hton16(0x0002);
	TCPH->destipaddr[0] = hton16(0x0a00); TCPH->destipaddr[1] = hton16(0x0001);
	TCPH->srcport = hton16(40000);
	TCPH->destport = hton16(80);
	wr32(TCPH->seqno, seq);
	wr32(TCPH->ackno, cli_rcv_nxt);
	TCPH->tcpoffset = (uint8_t)(offset_words << 4);
	TCPH->flags = flags;
	TCPH->wnd[0] = (uint8_t)(cli_window >> 8);
	TCPH->wnd[1] = (uint8_t)(cli_window & 0xff);
	if (optlen > 0)
		memcpy(&uip_buf[UIP_LLH_LEN + 40], opt, optlen);
}

/* A SYN whose option walk, left unbounded, never terminates: NOOP, then an
 * unknown option of length 255 that wraps an 8-bit cursor from 1 back to 0.
 * With the walk bounded, the SYN is parsed and a SYN-ACK goes out. If this
 * ever regresses the walk spins and the bench hangs - that is the symptom. */
static void scenario_malformed_tcp_options(void)
{
	static const uint8_t opts[4] = { 0x01, 0x03, 0xff, 0x00 };

	uip_init();
	httpd_init();
	cli_window = MSS_FULL;
	cli_rcv_nxt = 0;
	put_tcp(TCP_SYN, 6 /* 24-byte header */, opts, sizeof(opts), 1000, 44);
	uip_len = 44;
	uip_input();
	CHECK(uip_len && (TCPH->flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK),
	      "malformed SYN options: the walk terminates and a SYN-ACK is sent");
}

/* A segment claiming a 60-byte TCP header (data offset 15) while only 40 bytes
 * arrived. Unguarded, uip_len underflows and uip_appdata is carried past
 * uip_buf (ASan catches the read). Guarded, the segment is dropped and the
 * connection keeps working. */
static void scenario_oversized_data_offset(void)
{
	session_start(MSS_FULL);
	put_tcp(TCP_ACK, 15 /* 60-byte header */, NULL, 0, cli_seq, 40);
	uip_len = 40;
	uip_input();
	CHECK(uip_conn_active(0),
	      "oversized data offset: the segment is dropped, the connection survives");

	stream_len = 0;
	request_file(MSS_FULL);
	drain(MSS_FULL);
	CHECK(first_body_mismatch() == -1,
	      "oversized data offset: a normal request after it still serves the file");
}

/* A SYN advertising MSS 0 (option 02 04 00 00). uip_send clamps to conn->mss,
 * so storing 0 would leave a connection that can never send and holds its slot
 * until timeout. The option must be floored to a usable MSS instead. */
static void scenario_mss_zero(void)
{
	static const uint8_t mss0[4] = { 0x02, 0x04, 0x00, 0x00 };

	uip_init();
	httpd_init();
	cli_window = MSS_FULL;
	cli_rcv_nxt = 0;
	put_tcp(TCP_SYN, 6 /* 24-byte header */, mss0, sizeof(mss0), 1000, 44);
	uip_len = 44;
	uip_input();
	CHECK(uip_conns[0].mss != 0,
	      "an advertised MSS of 0 is floored, so the connection can send");
}

#define TCP_URG 0x20

/* A segment with URG set and an urgent pointer past the payload. Advancing
 * uip_appdata by the urgent pointer ran httpd off the end of uip_buf
 * (CVE-2020-17437, ASan SEGVs without the fix); delivered inline, the byte is
 * just data and the connection carries on. */
static void scenario_urg_inline(void)
{
	session_start(MSS_FULL);

	memset(uip_buf, 0, UIP_LLH_LEN + 41);
	TCPH->vhl = 0x45;
	TCPH->len[0] = 0; TCPH->len[1] = 41;	/* 20 IP + 20 TCP + 1 data */
	TCPH->ttl = 64;
	TCPH->proto = UIP_PROTO_TCP;
	TCPH->srcipaddr[0] = hton16(0x0a00); TCPH->srcipaddr[1] = hton16(0x0002);
	TCPH->destipaddr[0] = hton16(0x0a00); TCPH->destipaddr[1] = hton16(0x0001);
	TCPH->srcport = hton16(40000);
	TCPH->destport = hton16(80);
	wr32(TCPH->seqno, cli_seq);
	wr32(TCPH->ackno, cli_rcv_nxt);
	TCPH->tcpoffset = 5 << 4;
	TCPH->flags = TCP_ACK | TCP_URG;
	TCPH->wnd[0] = MSS_FULL >> 8; TCPH->wnd[1] = MSS_FULL & 0xff;
	TCPH->urgp[0] = 0; TCPH->urgp[1] = 2;	/* urgent pointer one past the byte */
	uip_buf[UIP_LLH_LEN + 40] = 'x';
	uip_len = 41;
	uip_input();
	CHECK(uip_conn_active(0),
	      "URG segment: delivered inline, no out-of-bounds, connection survives");
}


/* Private HTTP lifecycle values; drive the real callback, not a model. */
enum { TSTATE_NONE = 0, TSTATE_TX = 1, TSTATE_POST = 4 };
extern uint16_t slen, cont_len, sent_len, bindex, crc_final, write_len;
extern uint8_t boundary[], verify_crc, outbuf[];
extern struct { uint8_t *p; uint16_t bptr, plen; } upload_settings;
extern uint8_t upload_loader;
extern uint32_t uptr;
extern uint8_t up_state, up_reset;

/* A staged segment through the job and the poll after it, at once: what the
 * firmware did in one call before the flash job (E3). */
static void stream_upload(void)
{
	up_reset = 0;
	job_upload();
	uip_conn->appstate.tstate = up_state;
	if (up_reset)
		uip_conn->appstate.fw_reset_pending = up_reset;
}

static void scenario_upload_reset_owner(void)
{
	uint8_t byte = 0;
	uint8_t get[] = "GET " FILE_NAME " HTTP/1.1\r\nHost: sw\r\n\r\n";
	int initialized = 1;

	/* The firmware has one TCP slot. Its appstate can contain stale bytes. */
	uip_conn = &uip_conns[0];
	reset_calls = 0;
	for (unsigned state = 0; state <= 0xff; state++) {
		uip_conn->appstate.tstate = state;
		uip_conn->appstate.fw_reset_pending = 0xaa;
		uip_flags = UIP_CONNECTED;
		httpd_appcall();
		if (uip_conn->appstate.tstate != TSTATE_NONE
		    || uip_conn->appstate.fw_reset_pending != 0)
			initialized = 0;
	}
	CHECK(initialized, "connect initializes both fields for every stale state");
	CHECK(reset_calls == 0, "connection initialization does not reset");

	/* End-of-part verdict with flash/CRC hardware edges mocked. */
	boundary[0] = 0;
	bindex = 0;
	verify_crc = 1;
	crc_final = 0xb001;
	write_len = 0;
	upload_settings.p = &byte;
	upload_settings.bptr = 0;
	upload_settings.plen = 1;
	stream_upload();
	CHECK(uip_conn->appstate.fw_reset_pending == FW_UPDATE_PENDING,
	      "verified upload records reset intent in its slot");

	uip_conn->appstate.tstate = TSTATE_TX;
	uip_conn->mss = MSS_FULL;
	slen = 10;
	sent_len = 5;
	cont_len = 0;
	uip_flags = UIP_ACKDATA;
	httpd_appcall();
	CHECK(reset_calls == 0 && slen == 5 && sent_len == 5,
	      "partial verdict ACK sends the remainder without resetting");
	uip_flags = UIP_ACKDATA;
	httpd_appcall();
	CHECK(reset_calls == 1 && slen == 0 && reset_cause == CRASH_UPDATE,
	      "final verdict ACK resets after all response bytes are acknowledged, cause update");

	reset_calls = 0;
	loader_update_calls = 0;
	loader_update_ok = false;
	uip_conn->appstate.tstate = TSTATE_TX;
	uip_conn->appstate.fw_reset_pending = FW_UPDATE_PENDING;
	slen = 10;
	sent_len = 5;
	cont_len = 0;
	uip_flags = UIP_ACKDATA;
	httpd_appcall();
	uip_flags = UIP_ACKDATA;
	httpd_appcall();
	CHECK(reset_calls == 1 && loader_update_calls == 0,
	      "a firmware upload does not rewrite the loader");

	reset_calls = 0;
	upload_loader = 1;
	uip_conn->appstate.tstate = TSTATE_TX;
	uip_conn->appstate.fw_reset_pending = FW_UPDATE_PENDING;
	slen = 10;
	sent_len = 5;
	cont_len = 0;
	uip_flags = UIP_ACKDATA;
	httpd_appcall();
	uip_flags = UIP_ACKDATA;
	httpd_appcall();
	CHECK(reset_calls == 0 && !uip_conn->appstate.fw_reset_pending,
	      "a failed loader update keeps the switch running");
	upload_loader = 0;
	loader_update_calls = 0;
	uip_conn->appstate.tstate = TSTATE_TX;
	uip_conn->appstate.fw_reset_pending = FW_RESET_PENDING;
	slen = 10;
	sent_len = 5;
	cont_len = 0;
	uip_flags = UIP_ACKDATA;
	httpd_appcall();
	uip_flags = UIP_ACKDATA;
	httpd_appcall();
	CHECK(reset_calls == 1 && loader_update_calls == 0,
	      "a plain reset does not touch the loader");
	loader_update_ok = true;

	upload_loader = 1;
	boundary[0] = 0;
	bindex = 0;
	crc_final = 0xb001;
	write_len = 0;
	upload_settings.p = &byte;
	upload_settings.bptr = 0;
	upload_settings.plen = 1;
	uptr = LDR_STAGE + 0x100;
	uip_conn->appstate.fw_reset_pending = 0;
	stream_upload();
	CHECK(!uip_conn->appstate.fw_reset_pending,
	      "a loader image of the wrong length is refused");
	bindex = 0;
	upload_settings.bptr = 0;
	uptr = LDR_STAGE + LDR_LOADER_LEN;
	loader_fits_ok = false;
	stage_drops = 0;
	stream_upload();
	CHECK(!uip_conn->appstate.fw_reset_pending && strstr((char *)outbuf, "not a loader for this layout") && stage_drops == 1,
	      "a loader built for another layout is refused, the stage erased");
	loader_fits_ok = true;
	bindex = 0;
	upload_settings.bptr = 0;
	uptr = LDR_STAGE + LDR_LOADER_LEN;
	loader_present_ok = false;
	convert_arms = 0;
	stream_upload();
#ifdef LAYOUT_COMPACT
	CHECK(uip_conn->appstate.fw_reset_pending == FW_UPDATE_PENDING && convert_arms == 1,
	      "compact: a loader image on a switch without one arms the conversion");
	uip_conn->appstate.fw_reset_pending = 0;
#else
	CHECK(!uip_conn->appstate.fw_reset_pending && strstr((char *)outbuf, "no loader"),
	      "a loader image is refused on a switch without a loader");
#endif
	loader_present_ok = true;
#ifdef LAYOUT_COMPACT
	/* an install image: the application, then the loader; its stream CRC
	 * covers both, so only the two read-back CRCs count. A refused one must
	 * not leave its loader staged: the next boot would convert to it. */
	upload_loader = 0;
	crc_final = 0x1234;
	bindex = 0;
	upload_settings.bptr = 0;
	uptr = FIRMWARE_UPLOAD_END + LDR_LOADER_LEN;
	uip_conn->appstate.fw_reset_pending = 0;
	stage_drops = convert_arms = 0;
	stream_upload();
	CHECK(uip_conn->appstate.fw_reset_pending == FW_UPDATE_PENDING && !stage_drops && convert_arms == 1,
	      "compact: an accepted install image records reset intent and arms the conversion");
	loader_fits_ok = false;
	bindex = 0;
	upload_settings.bptr = 0;
	uptr = FIRMWARE_UPLOAD_END + LDR_LOADER_LEN;
	uip_conn->appstate.fw_reset_pending = 0;
	stage_drops = convert_arms = 0;
	stream_upload();
	CHECK(!uip_conn->appstate.fw_reset_pending && strstr((char *)outbuf, "checksum failed") && stage_drops == 1 && !convert_arms,
	      "compact: an install image whose loader doesn't fit is refused, the stage erased, nothing armed");
	loader_fits_ok = true;
	readback_crc = 0x1234;
	bindex = 0;
	upload_settings.bptr = 0;
	uptr = FIRMWARE_UPLOAD_END + LDR_LOADER_LEN;
	stage_drops = convert_arms = 0;
	stream_upload();
	CHECK(!uip_conn->appstate.fw_reset_pending && strstr((char *)outbuf, "checksum failed") && stage_drops == 1 && !convert_arms,
	      "compact: an install image whose application reads back wrong is refused, the stage erased, nothing armed");
	readback_crc = 0xb001;
	crc_final = 0;
	bindex = 0;
	upload_settings.bptr = 0;
	uptr = FIRMWARE_UPLOAD_END + 0x800;
	stage_drops = 0;
	stream_upload();
	CHECK(!uip_conn->appstate.fw_reset_pending && stage_drops == 1,
	      "compact: an install image cut off inside its loader is refused, the stage erased");
	crc_final = 0;
	bindex = 0;
	upload_settings.bptr = 0;
	uptr = FIRMWARE_UPLOAD_END;
	stage_drops = 0;
	stream_upload();
	CHECK(!uip_conn->appstate.fw_reset_pending && !stage_drops,
	      "compact: a refused plain image leaves the stage alone");
	upload_loader = 1;
	crc_final = 0xb001;
#endif
	bindex = 0;
	upload_settings.bptr = 0;
	uptr = LDR_STAGE + LDR_LOADER_LEN;
	stream_upload();
	CHECK(uip_conn->appstate.fw_reset_pending == FW_UPDATE_PENDING,
	      "a loader image of the right length records reset intent");
	reset_calls = 0;
	loader_update_calls = 0;
	uip_conn->appstate.tstate = TSTATE_TX;
	slen = 10;
	sent_len = 5;
	cont_len = 0;
	uip_flags = UIP_ACKDATA;
	httpd_appcall();
	uip_flags = UIP_ACKDATA;
	httpd_appcall();
	CHECK(reset_calls == 1 && loader_update_calls == 1,
	      "a loader image is installed from the staging area before the reset");
	upload_loader = 0;

	/* No verdict ACK: a later connection reuses the same slot. */
	reset_calls = 0;
	uip_conn->appstate.tstate = TSTATE_TX;
	uip_conn->appstate.fw_reset_pending = 1;
	uip_flags = UIP_CONNECTED;
	httpd_appcall();
	CHECK(uip_conn->appstate.tstate == TSTATE_NONE
	      && uip_conn->appstate.fw_reset_pending == 0,
	      "slot reuse clears an unacknowledged upload's reset intent");
	uip_appdata = get;
	uip_len = strlen((char *)get);
	uip_flags = UIP_NEWDATA;
	httpd_appcall();
	CHECK(uip_conn->appstate.tstate == TSTATE_TX && slen > 0,
	      "reused slot serves a subsequent plain GET");
	for (unsigned i = 0; i < 20 && uip_conn->appstate.tstate == TSTATE_TX; i++) {
		uip_flags = UIP_ACKDATA;
		httpd_appcall();
	}
	CHECK(reset_calls == 0 && slen == 0 && cont_len == 0,
	      "subsequent GET response ACKs cannot inherit reset intent");

	for (unsigned i = 0; i < 2; i++) {
		uip_conn->appstate.tstate = TSTATE_TX;
		uip_conn->appstate.fw_reset_pending = 1;
		reset_calls = 0;
		uip_flags = i ? UIP_TIMEDOUT : UIP_ABORT;
		httpd_appcall();
		CHECK(reset_calls == 0, "abort/timeout does not reset");
		uip_flags = UIP_CONNECTED;
		httpd_appcall();
		CHECK(uip_conn->appstate.tstate == TSTATE_NONE
		      && uip_conn->appstate.fw_reset_pending == 0,
		      "connect after abort/timeout clears old upload intent");
		uip_conn->appstate.tstate = TSTATE_TX;
		slen = 0;
		cont_len = 0;
		sent_len = 0;
		uip_flags = UIP_ACKDATA;
		httpd_appcall();
		CHECK(reset_calls == 0, "new connection ACK cannot inherit reset");
	}

	crc_final = 0;
	bindex = 0;
	upload_loader = 0;
	uptr = FIRMWARE_UPLOAD_START + 0x100;	/* a firmware image, not the size of an install image */
	stream_upload();
	CHECK(uip_conn->appstate.fw_reset_pending == 0,
	      "failed CRC does not request reset");
	CHECK(strstr((char *)outbuf, "HTTP/1.1 400 Bad Request") != NULL,
	      "failed CRC returns an error verdict");
	uip_conn->appstate.tstate = TSTATE_TX;
	sent_len = slen;
	cont_len = 0;
	uip_flags = UIP_ACKDATA;
	httpd_appcall();
	CHECK(reset_calls == 0, "failed CRC verdict ACK does not reset");

	/* a good stream whose flash read-back doesn't match is refused too */
	crc_final = 0xb001;
	readback_crc = 0x1234;
	bindex = 0;
	stream_upload();
	readback_crc = 0xb001;
	CHECK(uip_conn->appstate.fw_reset_pending == 0
	      && strstr((char *)outbuf, "HTTP/1.1 400 Bad Request") != NULL,
	      "an upload whose flash read-back fails is refused, no reset");

}

static void scenario_fragmented_post_header(void)
{
	static const char first[] =
		"POST /login HTTP/1.1\r\n"
		"Host: sw\r\n"
		"Content-Len";
	static const char second[] =
		"gth: 8\r\n"
		"Content-Type: application/x-www-form-urlencoded\r\n"
		"\r\n"
		"pwd=1234";

	login_session_start();
	client_send(TCP_ACK | TCP_PSH, first, (int)strlen(first));
	CHECK(stream_len == 0,
	      "fragmented header: incomplete first fragment has no response");

	client_send(TCP_ACK | TCP_PSH, second, (int)strlen(second));
	CHECK(stream_contains("HTTP/1.1 302 Found\r\n"),
	      "fragmented header: split Content-Length is accepted");
	CHECK(stream_contains("Location: index.html\r\n"),
	      "fragmented header: login body is parsed after reassembly");
}

static void scenario_split_content_type(void)
{
	static const char first[] =
		"POST /login HTTP/1.1\r\n"
		"Content-Length: 8\r\n"
		"Content-Ty";
	static const char second[] =
		"pe: application/x-www-form-urlencoded\r\n"
		"\r\n"
		"pwd=1234";

	login_session_start();
	client_send(TCP_ACK | TCP_PSH, first, (int)strlen(first));
	CHECK(stream_len == 0,
	      "split Content-Type: incomplete header has no response");
	client_send(TCP_ACK | TCP_PSH, second, (int)strlen(second));
	CHECK(stream_contains("Location: index.html\r\n"),
	      "split Content-Type: field name can span receive events");
}

static void scenario_split_header_terminator(void)
{
	static const char first[] =
		"POST /login HTTP/1.1\r\n"
		"Content-Length: 8\r\n"
		"Content-Type: application/x-www-form-urlencoded\r\n"
		"\r";
	static const char second[] = "\npwd=1234";

	login_session_start();
	client_send(TCP_ACK | TCP_PSH, first, (int)strlen(first));
	CHECK(stream_len == 0,
	      "split terminator: CRLFCR fragment has no premature response");
	client_send(TCP_ACK | TCP_PSH, second, (int)strlen(second));
	CHECK(stream_contains("Location: index.html\r\n"),
	      "split terminator: CRLFCRLF can span receive events");
}

static void scenario_header_then_body(void)
{
	static const char header[] =
		"POST /login HTTP/1.1\r\n"
		"Content-Length: 8\r\n"
		"Content-Type: application/x-www-form-urlencoded\r\n"
		"\r\n";

	login_session_start();
	client_send(TCP_ACK | TCP_PSH, header, (int)strlen(header));
	CHECK(stream_len == 0,
	      "header-only segment: waits for the declared body");
	client_send(TCP_ACK | TCP_PSH, "pwd=1234", 8);
	CHECK(stream_contains("Location: index.html\r\n"),
	      "header-only segment: following body completes login");
}

static void scenario_header_with_partial_body(void)
{
	static const char first[] =
		"POST /login HTTP/1.1\r\n"
		"Content-Length: 8\r\n"
		"Content-Type: application/x-www-form-urlencoded\r\n"
		"\r\n"
		"pwd=";

	login_session_start();
	client_send(TCP_ACK | TCP_PSH, first, (int)strlen(first));
	CHECK(stream_len == 0,
	      "partial body: waits when final header segment has only part of body");
	client_send(TCP_ACK | TCP_PSH, "1234", 4);
	CHECK(stream_contains("Location: index.html\r\n"),
	      "partial body: existing body reassembly takes over correctly");
}

static void scenario_post_header_timeout(void)
{
	static const char partial[] =
		"POST /login HTTP/1.1\r\n"
		"Content-Length: 8\r\n"
		"Content-Type: application/x-www-form-urlencoded\r\n";

	login_session_start();
	ticks = 0;
	client_send(TCP_ACK | TCP_PSH, partial, (int)strlen(partial));
	CHECK(stream_len == 0,
	      "header timeout: incomplete header has no immediate response");

	/* The firmware timeout is 5 seconds; 2000 host ticks is safely beyond it. */
	ticks = 2000;
	run_periodic(1);
	CHECK(!uip_conn_active(0),
	      "header timeout: connection is aborted when the header never completes");
}

static void scenario_oversized_post_header(void)
{
	static const char start[] =
		"POST /login HTTP/1.1\r\n"
		"X-Fill: ";
	char fill[1200];

	login_session_start();
	memset(fill, 'a', sizeof(fill));
	client_send(TCP_ACK | TCP_PSH, start, (int)strlen(start));

	/* Keep extending a header with no CRLFCRLF until the fixed buffer refuses it. */
	for (int i = 0; i < 16 && !stream_contains("400 Bad Request"); i++)
		client_send(TCP_ACK | TCP_PSH, fill, (int)sizeof(fill));

	CHECK(stream_contains("HTTP/1.1 400 Bad Request\r\n"),
	      "oversized header: rejected before the header buffer overflows");
}

static void scenario_fragmented_missing_content_length(void)
{
	static const char first[] =
		"POST /login HTTP/1.1\r\n"
		"Content-Type: application/x-www-form-urlencoded\r\n";
	static const char second[] = "\r\npwd=1234";

	login_session_start();
	client_send(TCP_ACK | TCP_PSH, first, (int)strlen(first));
	client_send(TCP_ACK | TCP_PSH, second, (int)strlen(second));
	CHECK(stream_contains("HTTP/1.1 411 Length Required\r\n"),
	      "fragmented header: missing Content-Length still returns 411");
}


static void scenario_reset_after_ack(void)
{
	static const char get[] =
		"POST /reset HTTP/1.1\r\n"
		"Host: sw\r\n"
		"Cookie: session=0123456789ab\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: 0\r\n"
		"\r\n";

	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");
	reset_calls = 0;

	client_send(TCP_ACK | TCP_PSH, get, (int)strlen(get));
	CHECK(reset_calls == 0 && stream_contains("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n"),
	      "reset: the answer goes out before the switch resets");
	client_ack(MSS_FULL);
	CHECK(reset_calls == 1 && reset_cause == CRASH_WEB,
	      "reset: the switch resets once the answer is acknowledged, cause web");

	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");
	reset_calls = 0;
	client_send(TCP_ACK | TCP_PSH, get, (int)strlen(get));
	CHECK(reset_calls == 0 && stream_contains("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n"),
	      "reset: the answer goes out before a closing client resets");
	client_send(TCP_FIN | TCP_ACK, NULL, 0);
	CHECK(reset_calls == 1, "reset: a FIN carrying the acknowledgement resets as well");
	crash_pending = 0;
}

static void scenario_fragmented_get_cookie(void)
{
	static const char first[] =
		"GET " PROTECTED_FILE_NAME " HTTP/1.1\r\n"
		"Host: sw\r\n"
		"Cook";
	static const char second[] =
		"ie: session=0123456789ab\r\n"
		"\r\n";

	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");

	client_send(TCP_ACK | TCP_PSH, first, (int)strlen(first));
	CHECK(stream_len == 0,
	      "fragmented GET cookie: no redirect before the header completes");

	client_send(TCP_ACK | TCP_PSH, second, (int)strlen(second));
	CHECK(stream_contains("HTTP/1.1 200 OK\r\n"),
	      "fragmented GET cookie: protected index is served");
	CHECK(!stream_contains("Location: login.html\r\n"),
	      "fragmented GET cookie: session is not lost on the redirected GET");
}

static void scenario_fragmented_get_cookie_value(void)
{
	static const char first[] =
		"GET " PROTECTED_FILE_NAME " HTTP/1.1\r\n"
		"Host: sw\r\n"
		"Cookie: session=012345";
	static const char second[] =
		"6789ab\r\n"
		"\r\n";

	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");

	client_send(TCP_ACK | TCP_PSH, first, (int)strlen(first));
	CHECK(stream_len == 0,
	      "fragmented GET cookie value: waits for the rest of the header");
	client_send(TCP_ACK | TCP_PSH, second, (int)strlen(second));
	CHECK(stream_contains("HTTP/1.1 200 OK\r\n"),
	      "fragmented GET cookie value: split session value authenticates");
}

static void scenario_split_get_header_terminator(void)
{
	static const char first[] =
		"GET " PROTECTED_FILE_NAME " HTTP/1.1\r\n"
		"Cookie: session=0123456789ab\r\n"
		"\r";
	static const char second[] = "\n";

	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");

	client_send(TCP_ACK | TCP_PSH, first, (int)strlen(first));
	CHECK(stream_len == 0,
	      "fragmented GET terminator: CRLFCR does not trigger parsing");
	client_send(TCP_ACK | TCP_PSH, second, 1);
	CHECK(stream_contains("HTTP/1.1 200 OK\r\n"),
	      "fragmented GET terminator: CRLFCRLF may span receive events");
}

/* The sessions of the login tests below: four in use, slot 1 the least recently used */
#define ID_STRIDE	13	/* SESSION_ID_LENGTH + 1 */

static const char *slot_id(int i)
{
	return session_id + i * ID_STRIDE;
}

static void fill_sessions(void)
{
	static const char *const ids[4] = { "0123456789ab", "aaaaaaaaaaaa", "bbbbbbbbbbbb", "cccccccccccc" };
	static const uint32_t used[4] = { 50, 10, 60, 70 };

	fake_now = 100;
	for (int i = 0; i < 4; i++) {
		strcpy(session_id + i * ID_STRIDE, ids[i]);
		last_session_use[i] = used[i];
	}
}

static int others_kept(int first, int second, int third)
{
	static const char *const ids[4] = { "0123456789ab", "aaaaaaaaaaaa", "bbbbbbbbbbbb", "cccccccccccc" };

	return !strcmp(slot_id(first), ids[first]) && !strcmp(slot_id(second), ids[second]) &&
	       !strcmp(slot_id(third), ids[third]);
}

static void scenario_login_with_valid_cookie_reuses_its_slot(void)
{
	static const char post[] =
		"POST /login HTTP/1.1\r\n"
		"Cookie: session=0123456789ab\r\n"
		"Content-Length: 8\r\n"
		"Content-Type: application/x-www-form-urlencoded\r\n"
		"\r\n"
		"pwd=1234";

	login_session_start();
	fill_sessions();
	client_send(TCP_ACK | TCP_PSH, post, (int)strlen(post));
	CHECK(stream_contains("Location: index.html\r\n"),
	      "login with a valid cookie: accepted");
	CHECK(stream_contains("; SameSite=Strict; HttpOnly\r\n"),
	      "login: the session cookie is HttpOnly");
	CHECK(strcmp(slot_id(0), "0123456789ab") != 0,
	      "login with a valid cookie: its own slot gets the new id");
	CHECK(others_kept(1, 2, 3),
	      "login with a valid cookie: nobody else is pushed out");
	fake_now = 0;
}

static void scenario_login_without_cookie_replaces_least_recently_used(void)
{
	static const char post[] =
		"POST /login HTTP/1.1\r\n"
		"Content-Length: 8\r\n"
		"Content-Type: application/x-www-form-urlencoded\r\n"
		"\r\n"
		"pwd=1234";

	login_session_start();
	fill_sessions();
	client_send(TCP_ACK | TCP_PSH, post, (int)strlen(post));
	CHECK(stream_contains("Location: index.html\r\n"),
	      "login without a cookie: accepted");
	CHECK(strcmp(slot_id(1), "aaaaaaaaaaaa") != 0,
	      "login without a cookie: the least recently used slot is taken");
	CHECK(others_kept(0, 2, 3),
	      "login without a cookie: the other three stay");
	fake_now = 0;
}

static void scenario_fragmented_get_without_cookie(void)
{
	static const char first[] =
		"GET " PROTECTED_FILE_NAME " HTTP/1.1\r\n"
		"Host: sw\r\n";
	static const char second[] = "\r\n";

	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");

	client_send(TCP_ACK | TCP_PSH, first, (int)strlen(first));
	client_send(TCP_ACK | TCP_PSH, second, (int)strlen(second));
	CHECK(stream_contains("HTTP/1.1 302 Found\r\n"),
	      "fragmented GET without cookie: protected file still redirects");
	CHECK(stream_contains("Location: login.html\r\n"),
	      "fragmented GET without cookie: authentication is not carried over");
}

static void scenario_get_header_timeout(void)
{
	static const char partial[] =
		"GET " PROTECTED_FILE_NAME " HTTP/1.1\r\n"
		"Host: sw\r\n"
		"Cookie: session=012345";

	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");
	ticks = 0;

	client_send(TCP_ACK | TCP_PSH, partial, (int)strlen(partial));
	CHECK(stream_len == 0,
	      "fragmented GET timeout: incomplete header has no immediate response");

	ticks = 2000;
	run_periodic(1);
	CHECK(!uip_conn_active(0),
	      "fragmented GET timeout: incomplete request is eventually aborted");
}

static void scenario_oversized_get_header(void)
{
	static const char start[] =
		"GET " PROTECTED_FILE_NAME " HTTP/1.1\r\n"
		"X-Fill: ";
	char fill[1200];

	session_start(MSS_FULL);
	memset(fill, 'a', sizeof(fill));

	client_send(TCP_ACK | TCP_PSH, start, (int)strlen(start));
	for (int i = 0; i < 16 && !stream_contains("400 Bad Request"); i++)
		client_send(TCP_ACK | TCP_PSH, fill, (int)sizeof(fill));

	CHECK(stream_contains("HTTP/1.1 400 Bad Request\r\n"),
	      "oversized GET header: fixed header buffer is bounds checked");
}

static void scenario_split_method_names(void)
{
	static const char get[] =
		"GET " PROTECTED_FILE_NAME " HTTP/1.1\r\n"
		"Cookie: session=0123456789ab\r\n\r\n";
	static const char post[] =
		"POST /login HTTP/1.1\r\n"
		"Content-Length: 8\r\n"
		"Content-Type: application/x-www-form-urlencoded\r\n\r\n"
		"pwd=1234";
	int get_waits = 1, get_ok = 1, post_waits = 1, post_ok = 1;

	for (int split = 1; split < 3; split++) {
		session_start(MSS_FULL);
		strcpy(session_id, "0123456789ab");
		client_send(TCP_ACK | TCP_PSH, get, split);
		if (stream_len)
			get_waits = 0;
		client_send(TCP_ACK | TCP_PSH, get + split, (int)strlen(get) - split);
		if (!stream_contains("HTTP/1.1 200 OK\r\n"))
			get_ok = 0;
	}
	CHECK(get_waits, "split GET method: incomplete verb waits for the header");
	CHECK(get_ok, "split GET method: every split inside GET serves the file");

	for (int split = 1; split < 4; split++) {
		login_session_start();
		client_send(TCP_ACK | TCP_PSH, post, split);
		if (stream_len)
			post_waits = 0;
		client_send(TCP_ACK | TCP_PSH, post + split, (int)strlen(post) - split);
		if (!stream_contains("Location: index.html\r\n"))
			post_ok = 0;
	}
	CHECK(post_waits, "split POST method: incomplete verb waits for the header");
	CHECK(post_ok, "split POST method: every split inside POST completes login");
}

static void scenario_config_upload(void)
{
	static const char expected[] = "hostname upload-test\nip 10.0.0.1\n";
	static const char body[] =
		"--upload-boundary\r\n"
		"Content-Disposition: form-data; name=\"file\"; filename=\"config.txt\"\r\n"
		"Content-Type: text/plain\r\n\r\n"
		"hostname upload-test\nip 10.0.0.1\n"
		"\r\n--upload-boundary--\r\n";
	char request[768];
	int request_len, first_bad_response = -1, first_bad_flash = -1;

	request_len = snprintf(request, sizeof(request),
		"POST /config HTTP/1.1\r\n"
		"Host: sw\r\n"
		"Cookie: session=0123456789ab\r\n"
		"Content-Type: multipart/form-data; boundary=upload-boundary\r\n"
		"Content-Length: %zu\r\n\r\n%s", sizeof(body) - 1, body);
	if (request_len < 0 || request_len >= (int)sizeof(request)) {
		CHECK(0, "multipart/config: test request fits the client buffer");
		return;
	}

	/* The same well-formed request must work in one event and with a split
	 * anywhere in its HTTP header, first multipart delimiter, part headers,
	 * configuration, or closing delimiter. No extra CRLF is added to the body. */
	for (int split = 0; split <= request_len; split++) {
		session_start(MSS_FULL);
		strcpy(session_id, "0123456789ab");
		config_erases = config_writes = 0;
		written_config_len = 0;
		memset(written_config, 0xa5, sizeof(written_config));
		if (split)
			client_send(TCP_ACK | TCP_PSH, request, split);
		if (split < request_len)
			client_send(TCP_ACK | TCP_PSH, request + split, request_len - split);
		run_periodic(1);	/* the poll after the flash job sends the answer */
		if (!stream_contains("HTTP/1.1 200 OK\r\n") && first_bad_response < 0)
			first_bad_response = split;
		if ((config_erases != 1 || config_writes != 1
		     || config_erase_addr != CONFIG_START || config_write_addr != CONFIG_START
		     || written_config_len != sizeof(expected)
		     || memcmp(written_config, expected, sizeof(expected))) && first_bad_flash < 0)
			first_bad_flash = split;
	}
	if (first_bad_response >= 0)
		printf("      multipart/config: first rejected request split at byte %d\n", first_bad_response);
	if (first_bad_flash >= 0)
		printf("      multipart/config: first incorrect flash write at split %d\n", first_bad_flash);
	CHECK(first_bad_response < 0,
	      "multipart/config: complete request and every two-event split return 200");
	CHECK(first_bad_flash < 0,
	      "multipart/config: every split writes the exact configuration and NUL to its sector");
}

/* Requiring octet-stream would leave alternate or absent MIME types buffered
 * as headers forever instead of staging the binary and checking its CRC. */
static void scenario_upload(const char *name, const char *part_type, int split)
{
	static const uint8_t payload[] = { 0x00, 0xff, 'R', '\r', '\n', '-', 0x80, 0x00 };
	static const uint8_t staged[] = { 0x00, 0xff, 'R', '\r', '\n', '-', 0x80, 0x00 };
	static const char closing[] = "\r\n--upload-boundary--\r\n";
	char preamble[256], request[512];
	uint32_t saved_flash_size = flash_size;
	int preamble_len, header_len, request_len;

	printf("\n-- upload: %s --\n", name);
	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");
	session_timeout = 200;
	last_session_use[0] = 0; /* read_reg_timer() returns zero in this bench. */
	flash_size = 0x100000;
	flash_written_len = 0;
	flash_write_bad = 0;
	reset_calls = 0;

	preamble_len = snprintf(preamble, sizeof(preamble),
		"--upload-boundary\r\n"
		"Content-Disposition: form-data; name=\"file\"; filename=\"firmware.bin\"\r\n"
		"%s\r\n", part_type);
	header_len = snprintf(request, sizeof(request),
		"POST /upload HTTP/1.1\r\nHost: sw\r\n"
		"Cookie: session=0123456789ab\r\n"
		"Content-Type: multipart/form-data; boundary=upload-boundary\r\n"
		"Content-Length: %zu\r\n\r\n%s",
		(size_t)preamble_len + sizeof(payload) + sizeof(closing) - 1, preamble);
	memcpy(request + header_len, payload, sizeof(payload));
	memcpy(request + header_len + sizeof(payload), closing, sizeof(closing) - 1);
	request_len = header_len + sizeof(payload) + sizeof(closing) - 1;
	if (split) {
		/* Cut the part's CRLFCRLF between its final CR and LF. */
		client_send(TCP_ACK | TCP_PSH, request, header_len - 1);
		CHECK(stream_len == 0 && flash_written_len == 0,
		      "upload: incomplete part headers stage nothing and await more data");
		client_send(TCP_ACK | TCP_PSH, request + header_len - 1,
			    request_len - header_len + 1);
	} else {
		client_send(TCP_ACK | TCP_PSH, request, request_len);
	}
	drain(MSS_FULL);

	CHECK(!flash_write_bad && flash_written_len == sizeof(staged) &&
	      !memcmp(flash_written, staged, sizeof(staged)),
	      "upload: exactly the binary payload staged at image address, nothing past it");
	CHECK(stream_len >= 12 && !memcmp(stream, "HTTP/1.1 400", 12),
	      "upload: invalid firmware is rejected with HTTP 400");
	CHECK(!uip_conn->appstate.fw_reset_pending && reset_calls == 0,
	      "upload: deliberately invalid CRC never schedules or triggers reset");
	flash_size = saved_flash_size;
}


#define UPLOAD_LEN	600
#define UPLOAD_BOUNDARY	"\r\n--XyZ"
#define UPLOAD_TRAILER	UPLOAD_BOUNDARY "--\r\n"

static int upload_in_two(uint8_t *body, uint16_t body_len, uint16_t split)
{
	uptr = FIRMWARE_UPLOAD_START;
	write_len = 0;
	bindex = 0;
	verify_crc = 0;
	flash_written_len = 0;
	flash_write_bad = 0;
	strcpy((char *)boundary, UPLOAD_BOUNDARY);
	uip_conn = &uip_conns[0];
	uip_conn->appstate.tstate = TSTATE_POST;

	upload_settings.p = body;
	upload_settings.bptr = 0;
	upload_settings.plen = split;
	stream_upload();
	if (uip_conn->appstate.tstate == TSTATE_POST) {
		upload_settings.p = body + split;
		upload_settings.bptr = 0;
		upload_settings.plen = body_len - split;
		stream_upload();
	}
	return uip_conn->appstate.tstate == TSTATE_NONE
		&& uptr == FIRMWARE_UPLOAD_START + UPLOAD_LEN && !flash_write_bad
		&& flash_written_len == UPLOAD_LEN;
}

static void scenario_upload_cr_before_boundary(void)
{
	uint8_t body[UPLOAD_LEN + sizeof(UPLOAD_TRAILER)];
	uint16_t body_len, split;
	int completed = 0, intact = 0, splits = 0;

	for (uint16_t i = 0; i < UPLOAD_LEN; i++)
		body[i] = pattern(i);
	body[UPLOAD_LEN - 2] = 'x';
	body[UPLOAD_LEN - 1] = '\r';
	body_len = UPLOAD_LEN + sizeof(UPLOAD_TRAILER) - 1;
	memcpy(body + UPLOAD_LEN, UPLOAD_TRAILER, sizeof(UPLOAD_TRAILER) - 1);

	for (split = 0; split <= body_len; split++) {
		splits++;
		if (upload_in_two(body, body_len, split)) {
			completed++;
			if (!memcmp(flash_written, body, UPLOAD_LEN))
				intact++;
		}
	}
	if (verbose)
		printf("      %d of %d splits complete, %d intact\n", completed, splits, intact);

	CHECK(completed == splits,
	      "upload: an image ending in CR still finds the closing boundary");
	CHECK(intact == splits,
	      "upload: the CR and the bytes before it reach the flash unchanged");
}

static int upload_whole_and_split(uint8_t *body, uint16_t body_len)
{
	return upload_in_two(body, body_len, body_len) && !memcmp(flash_written, body, UPLOAD_LEN)
		&& upload_in_two(body, body_len, UPLOAD_LEN) && !memcmp(flash_written, body, UPLOAD_LEN);
}

static void scenario_upload_last_byte(void)
{
	uint8_t body[UPLOAD_LEN + sizeof(UPLOAD_TRAILER)];
	uint16_t body_len = UPLOAD_LEN + sizeof(UPLOAD_TRAILER) - 1;
	int bad = -1;

	for (uint16_t i = 0; i < UPLOAD_LEN; i++)
		body[i] = pattern(i);
	memcpy(body + UPLOAD_LEN, UPLOAD_TRAILER, sizeof(UPLOAD_TRAILER) - 1);
	for (int last = 0; last < 256 && bad < 0; last++) {
		body[UPLOAD_LEN - 1] = last;
		if (!upload_whole_and_split(body, body_len))
			bad = last;
	}
	if (bad >= 0)
		printf("      last byte 0x%02x loses the closing boundary\n", bad);
	CHECK(bad < 0, "upload: the part ends after the payload whatever its last byte");

	body[UPLOAD_LEN - 1] = '\r';
	body[UPLOAD_LEN - 2] = '\r';
	CHECK(upload_whole_and_split(body, body_len), "upload: an even run of CR before the boundary");
	body[UPLOAD_LEN - 3] = '\r';
	CHECK(upload_whole_and_split(body, body_len), "upload: an odd run of CR before the boundary");
}

static void scenario_upload_broken_matches(void)
{
	uint8_t body[UPLOAD_LEN + sizeof(UPLOAD_TRAILER)];
	uint16_t body_len = UPLOAD_LEN + sizeof(UPLOAD_TRAILER) - 1;

	memcpy(body + UPLOAD_LEN, UPLOAD_TRAILER, sizeof(UPLOAD_TRAILER) - 1);
	memset(body, '\r', UPLOAD_LEN);
	CHECK(upload_whole_and_split(body, body_len),
	      "upload: a run of CR longer than a flash page is staged intact");
	for (uint16_t i = 0; i < UPLOAD_LEN; i++)
		body[i] = "\r\n-"[i % 3];
	CHECK(upload_whole_and_split(body, body_len),
	      "upload: repeated partial delimiters are staged as payload");
}

/* E3: the stage (config_buf) takes segments behind what the flash job still
 * has to write, and the window is its room. A client that keeps to it, with
 * the job held, then run a step at a time between arrivals: every segment
 * is taken, the window closes as the stage fills, the poll after the job
 * has written it all sends a window update (no zero-window deadlock), and no
 * ACK moves the right edge (ack + window) left. A 5000 B image crosses a
 * sector: both sectors erased, every byte staged in order. A segment past
 * the window is refused. */
#define WIN_LEN 5000

/* A zero-window probe as Linux sends one: empty, one below what we have
 * acknowledged. 1 if answered with an ACK whose window is open (want_open)
 * or shut (!want_open). */
static int probe(int want_open)
{
	uint32_t seq = cli_seq;

	srv_flags = 0;
	cli_seq--;
	client_send(TCP_ACK, NULL, 0);
	cli_seq = seq;
	return (srv_flags & TCP_ACK) && (want_open ? srv_wnd > 0 : srv_wnd == 0);
}

static void scenario_upload_window(void)
{
	static uint8_t request[WIN_LEN + 1024];	/* the last case sends 4 MSS */
	static const char closing[] = "\r\n--upload-boundary--\r\n";
	static const char part[] = "--upload-boundary\r\n"
		"Content-Disposition: form-data; name=\"file\"; filename=\"firmware.bin\"\r\n"
		"Content-Type: application/octet-stream\r\n\r\n";
	int header_len, request_len, off, n, taken = 1, zero = 0, stuck = 0, probe_bad = 0;
	uint32_t saved_flash_size = flash_size;

	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");
	session_timeout = 200;
	last_session_use[0] = 0;
	flash_size = 0x100000;
	flash_written_len = 0;
	flash_write_bad = 0;
	erase_n = 0;
	header_len = snprintf((char *)request, sizeof(request),
		"POST /upload HTTP/1.1\r\nHost: sw\r\n"
		"Cookie: session=0123456789ab\r\n"
		"Content-Type: multipart/form-data; boundary=upload-boundary\r\n"
		"Content-Length: %zu\r\n\r\n%s",
		sizeof(part) - 1 + WIN_LEN + sizeof(closing) - 1, part);
	for (int i = 0; i < WIN_LEN; i++)
		request[header_len + i] = pattern(i);
	memcpy(request + header_len + WIN_LEN, closing, sizeof(closing) - 1);
	request_len = header_len + WIN_LEN + sizeof(closing) - 1;

	/* The job held, then run a step at a time between arrivals, then to its
	 * end when the window has closed: every ACK's right edge is checked. */
	job_defer = 1;
	job_vf = 0;
	edge_watch = 1;
	edge_last = 0;
	edge_bad = 0;
	off = 0;
	srv_wnd = MSS_FULL;
	for (int k = 0; off < request_len && k < 200; k++) {
		n = request_len - off;
		if (n > MSS_FULL / 2)		/* as Linux: half the largest window seen */
			n = MSS_FULL / 2;
		if (n > srv_wnd)
			n = srv_wnd;
		if (!n) {			/* closed: the job writes the stage, its poll */
			zero++;
			if (!probe(0))		/* a zero-window probe: answered, still 0 */
				probe_bad++;
			job_run();
			if (zero == 1 && !probe(1))	/* the update lost: a probe repairs it */
				probe_bad++;
			run_periodic(1);
			if (!srv_wnd)
				stuck++;
			if (!srv_wnd)
				break;
			continue;
		}
		client_send(TCP_ACK | TCP_PSH, (const char *)request + off, n);
		if (srv_ack != cli_seq || uip_conn->appstate.tstate != 9)
			taken = 0;
		off += n;
		if (k % 3 == 2) {		/* a step of the job between arrivals */
			job_step();
			if (fl_finished && !fl_kind)
				run_periodic(1);
		}
	}
	job_defer = 0;
	job_run();
	run_periodic(1);
	edge_watch = 0;
	CHECK(taken && off == request_len, "upload window: every segment inside the window is taken");
	CHECK(zero > 0 && !stuck, "upload window: the window closes as the stage fills, and the poll after the job reopens it");
	CHECK(!edge_bad, "upload window: no ACK moves the window's right edge left");
	CHECK(!probe_bad, "upload window: a zero-window probe gets an ACK with the window as it is (0, or open once the job has written the stage)");
	drain(MSS_FULL);
	CHECK(!flash_write_bad && flash_written_len == WIN_LEN && !memcmp(flash_written, request + header_len, WIN_LEN),
	      "upload window: the image is staged whole, in order, across the segments");
	CHECK(erase_n == 2 && erase_log[0] == FIRMWARE_UPLOAD_START && erase_log[1] == FIRMWARE_UPLOAD_START + 0x1000,
	      "upload window: each sector erased once, before its first page");
	CHECK(stream_len >= 12 && !memcmp(stream, "HTTP/1.1 400", 12) && strstr((char *)stream, "checksum failed"),
	      "upload window: the answer goes out once the part is written");

	/* a client that ignores the window: uIP doesn't take the segment past
	 * it, and ACKs with the window as it is; once the job has written the
	 * stage the retransmission is taken */
	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");
	last_session_use[0] = 0;
	flash_written_len = 0;
	job_defer = 1;
	job_vf = 0;
	for (off = 0; off < 3 * MSS_FULL; off += MSS_FULL)
		client_send(TCP_ACK | TCP_PSH, (const char *)request + off, MSS_FULL);
	{
		uint32_t seq = cli_seq, ack = srv_ack;
		uint16_t wnd = srv_wnd;

		srv_flags = 0;
		client_send(TCP_ACK | TCP_PSH, (const char *)request + off, MSS_FULL);
		CHECK(wnd < MSS_FULL && (srv_flags & TCP_ACK) && srv_ack == ack && srv_wnd == wnd,
		      "upload window: a segment past the window isn't taken; an ACK with the window as it is");
		cli_seq = seq;
		job_run();
		run_periodic(1);
		client_send(TCP_ACK | TCP_PSH, (const char *)request + off, MSS_FULL);
		CHECK(srv_ack == cli_seq && flash_written_len > 0,
		      "upload window: its retransmission, once the stage has room, is taken");
	}
	job_defer = 0;
	flash_size = saved_flash_size;
}

/* E3: segments that come after the job has written all of the stage but
 * before its FL_DONE step (a pass can run rx several times in between): they
 * fill the window to zero, and the job must not end with them unwritten (the
 * window would stay closed: its update says zero, and uIP doesn't answer
 * Linux's zero-window probes). Measured with curl, 2026-11-19. */
static void scenario_upload_window_race(void)
{
	static uint8_t request[WIN_LEN + 1024];
	static const char closing[] = "\r\n--upload-boundary--\r\n";
	static const char part[] = "--upload-boundary\r\n"
		"Content-Disposition: form-data; name=\"file\"; filename=\"firmware.bin\"\r\n"
		"Content-Type: application/octet-stream\r\n\r\n";
	int header_len, request_len, off, n, filled = 0, sends;
	uint32_t saved_flash_size = flash_size;

	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");
	session_timeout = 200;
	last_session_use[0] = 0;
	flash_size = 0x100000;
	flash_written_len = 0;
	flash_write_bad = 0;
	header_len = snprintf((char *)request, sizeof(request),
		"POST /upload HTTP/1.1\r\nHost: sw\r\n"
		"Cookie: session=0123456789ab\r\n"
		"Content-Type: multipart/form-data; boundary=upload-boundary\r\n"
		"Content-Length: %zu\r\n\r\n%s",
		sizeof(part) - 1 + WIN_LEN + sizeof(closing) - 1, part);
	for (int i = 0; i < WIN_LEN; i++)
		request[header_len + i] = pattern(i);
	memcpy(request + header_len + WIN_LEN, closing, sizeof(closing) - 1);
	request_len = header_len + WIN_LEN + sizeof(closing) - 1;

	job_defer = 1;
	job_vf = 0;
	off = header_len + 100;			/* the headers and a little of the image */
	client_send(TCP_ACK | TCP_PSH, (const char *)request, off);
	for (int i = 0; i < 50 && job_vf != 2; i++)
		job_step();			/* to its last write, FL_DONE to come */
	for (sends = 0; sends < 12 && srv_wnd && off < request_len; sends++) {
		n = request_len - off > MSS_FULL / 2 ? MSS_FULL / 2 : request_len - off;
		if (n > srv_wnd)
			n = srv_wnd;
		client_send(TCP_ACK | TCP_PSH, (const char *)request + off, n);
		off += n;
	}
	filled = srv_wnd == 0;
	job_run();				/* FL_DONE, and what it does then */
	run_periodic(1);
	CHECK(filled && srv_wnd > 0,
	      "upload window: segments before the job's FL_DONE are written, the window reopens");
	job_defer = 0;
	for (int i = 0; i < 40 && off < request_len && srv_wnd; i++) {
		n = request_len - off > MSS_FULL / 2 ? MSS_FULL / 2 : request_len - off;
		if (n > srv_wnd)
			n = srv_wnd;
		client_send(TCP_ACK | TCP_PSH, (const char *)request + off, n);
		off += n;
		run_periodic(1);
	}
	drain(MSS_FULL);
	CHECK(off == request_len && !flash_write_bad && flash_written_len == WIN_LEN
	      && !memcmp(flash_written, request + header_len, WIN_LEN),
	      "upload window: and the image is staged whole, in order");
	flash_size = saved_flash_size;
}

/* E3 review: the upload's job has ended between two segments and another
 * kind (the loader's boot-count erase, 15 s after a boot) runs when the
 * last segment comes. upload_go() leaves it to the running job; that job's
 * FL_DONE must go on with the upload (flash_job.c fl_end()): the segment is
 * written and the answer sent. */
static void scenario_upload_during_other_job(void)
{
	static uint8_t request[WIN_LEN + 1024];
	static const char closing[] = "\r\n--upload-boundary--\r\n";
	static const char part[] = "--upload-boundary\r\n"
		"Content-Disposition: form-data; name=\"file\"; filename=\"firmware.bin\"\r\n"
		"Content-Type: application/octet-stream\r\n\r\n";
	int header_len, request_len, off, n, before_ok;
	uint32_t saved_flash_size = flash_size;

	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");
	session_timeout = 200;
	last_session_use[0] = 0;
	flash_size = 0x100000;
	flash_written_len = 0;
	flash_write_bad = 0;
	header_len = snprintf((char *)request, sizeof(request),
		"POST /upload HTTP/1.1\r\nHost: sw\r\n"
		"Cookie: session=0123456789ab\r\n"
		"Content-Type: multipart/form-data; boundary=upload-boundary\r\n"
		"Content-Length: %zu\r\n\r\n%s",
		sizeof(part) - 1 + WIN_LEN + sizeof(closing) - 1, part);
	for (int i = 0; i < WIN_LEN; i++)
		request[header_len + i] = pattern(i);
	memcpy(request + header_len + WIN_LEN, closing, sizeof(closing) - 1);
	request_len = header_len + WIN_LEN + sizeof(closing) - 1;

	/* all but the last segment, the job running at once */
	job_defer = 0;
	for (off = 0; off < request_len - 300; off += n) {
		n = request_len - 300 - off > MSS_FULL / 2 ? MSS_FULL / 2 : request_len - 300 - off;
		client_send(TCP_ACK | TCP_PSH, (const char *)request + off, n);
		run_periodic(1);
	}
	before_ok = uip_conn->appstate.tstate == TSTATE_POST && !fl_kind && stream_len == 0;
	/* another kind of job runs, its FL_DONE to come; the last segment */
	job_defer = 1;
	fl_req = FL_BOOTCNT;
	CHECK(!flash_job_start(), "upload between segments: another kind of job doesn't start");
	fl_kind = FL_BOOTCNT;		/* as if it had started just before the upload */
	job_vf = 2;
	client_send(TCP_ACK | TCP_PSH, (const char *)request + off, request_len - off);
	job_run();
	job_defer = 0;
	run_periodic(1);
	drain(MSS_FULL);
	CHECK(before_ok && !flash_write_bad && flash_written_len == WIN_LEN
	      && !memcmp(flash_written, request + header_len, WIN_LEN)
	      && stream_len >= 12 && !memcmp(stream, "HTTP/1.1 400", 12),
	      "upload between segments: the last one, staged while another job ran, is written and answered");
	flash_size = saved_flash_size;
}

/* E3: the flash job is busy (or has no slot): an upload is answered 400 at
 * once; and while it writes for a connection that has ended, a new
 * connection is reset (config_buf, the stage, may not be written then). */
static void scenario_flash_busy(void)
{
	static const char req[] =
		"POST /upload HTTP/1.1\r\nHost: sw\r\n"
		"Cookie: session=0123456789ab\r\n"
		"Content-Type: multipart/form-data; boundary=upload-boundary\r\n"
		"Content-Length: 300\r\n\r\n"
		"--upload-boundary\r\n"
		"Content-Disposition: form-data; name=\"file\"; filename=\"firmware.bin\"\r\n"
		"Content-Type: application/octet-stream\r\n\r\nxyz";
	uint32_t saved_flash_size = flash_size;

	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");
	session_timeout = 200;
	last_session_use[0] = 0;
	flash_size = 0x100000;
	flash_written_len = 0;
	job_busy = 1;
	client_send(TCP_ACK | TCP_PSH, req, (int)strlen(req));
	drain(MSS_FULL);
	job_busy = 0;
	CHECK(stream_len >= 12 && !memcmp(stream, "HTTP/1.1 400", 12) && flash_written_len == 0,
	      "flash busy: an upload is refused at once, nothing written");

	fl_kind = FL_UPLOAD;
	uip_init();
	httpd_init();
	fl_kind = FL_UPLOAD;		/* httpd_init frees it: the job still runs */
	cli_seq = 5000;
	cli_rcv_nxt = 0;
	stream_len = 0;
	srv_flags = 0;
	client_send(TCP_SYN, NULL, 0);
	client_ack(MSS_FULL);
	CHECK(srv_flags & 0x04, "flash busy: a new connection is reset while the job runs");
	fl_kind = 0;
	flash_size = saved_flash_size;
}

/* ---- POST /cli and /cmd: replies longer than outbuf (P3C) --------------- */

/* cli's side, as tasks.c runs it: the command from config_buf
 * (httpd_webcmd_begin), then lines into outbuf while there is room
 * (WEBCMD_ROOM(CLI_WEB_ROOM), webcmd_wait clear), else webcmd_wait; at the
 * end httpd_webcmd_end. A run whose connection ended (webcmd_busy 0) stops. */
#define BENCH_CLI_ROOM	640
static int cli_lines, cli_n, cli_running;
static int cli_big = -1;	/* this line prints BIG_LINE bytes in one step */
#define BIG_LINE	2400
static uint8_t cli_err;

/* write_char_no_syslog()'s capture (rtlplayground.c) */
static void cap_putc(char c)
{
	if (!cmd_capture)
		return;
	if (slen < TCP_OUTBUF_SIZE - sizeof(CMD_TRUNCATED))
		outbuf[slen++] = c;
	else
		cmd_capture = 2;
}

static int cli_line(char *buf, int n)
{
	return sprintf(buf, "state.port.%d.rx_good %d\n", n % 9 + 1, n * 7919);
}

static void cli_model(void)
{
	char line[64];
	int n;

	if (webcmd_pending) {
		webcmd_pending = 0;
		cli_running = 1;
		cli_n = 0;
		httpd_webcmd_begin();
	}
	if (!cli_running)
		return;
	if (!webcmd_busy) {		/* cli_web_drop */
		cli_running = 0;
		return;
	}
	while (cli_n < cli_lines) {
		if (webcmd_wait || !WEBCMD_ROOM(BENCH_CLI_ROOM)) {
			webcmd_wait = 1;
			return;
		}
		WEBCMD_CAPTURE_ON();
		if (cli_n == cli_big) {
			for (n = 0; n < BIG_LINE; n++)
				cap_putc(n == BIG_LINE - 1 ? '\n' : 'x');
			cli_n++;
		} else {
			n = cli_line(line, cli_n++);
			for (int i = 0; i < n; i++)
				cap_putc(line[i]);
		}
		WEBCMD_CAPTURE_OFF();
	}
	err_status = cli_err;
	httpd_webcmd_end();
	cli_running = 0;
}

/* The whole expected body */
static int cli_expect(char *buf)
{
	int len = 0;

	for (int n = 0; n < cli_lines; n++)
		len += cli_line(buf + len, n);
	return len;
}

static int fin_seen;

static void cli_post(const char *path, const char *body)
{
	char req[512];
	int n = sprintf(req, "POST %s HTTP/1.1\r\nHost: sw\r\nCookie: session=0123456789ab\r\n"
			"Content-Type: text/plain;charset=UTF-8\r\nContent-Length: %d\r\n\r\n%s",
			path, (int)strlen(body), body);

	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");
	fin_seen = 0;
	client_send(TCP_ACK | TCP_PSH, req, n);
}

/* cli and the client in turn until the server's FIN, or drop_at bytes have
 * come (then the client resets the connection) */
static void cli_drive(int drop_at)
{
	for (int i = 0; i < 2000 && !fin_seen; i++) {
		cli_model();
		run_periodic(1);
		if (srv_flags & TCP_FIN)
			fin_seen = 1;
		if (drop_at && stream_len >= drop_at) {
			client_send(TCP_RST | TCP_ACK, NULL, 0);
			return;
		}
		client_ack(MSS_FULL);
		if (srv_flags & TCP_FIN)
			fin_seen = 1;
	}
}

static void scenario_cli_long_reply(void)
{
	static char want[STREAM_MAX];
	int off, len;

	cli_lines = 400;		/* ~11 KB: four and more outbufs */
	cli_err = ERR_OK;
	webcmd_cli = 0;
	cli_post("/cli", "get state.port");
	cli_drive(0);
	len = cli_expect(want);
	off = body_offset();
	if (verbose)
		printf("      /cli: %d lines, body %d B of %d, fin %d\n", cli_lines, stream_len - off, len, fin_seen);
	CHECK(off > 0 && !memcmp(stream, "HTTP/1.1 200 OK\r\n", 17), "/cli: a 200 reply");
	CHECK(len > 4 * TCP_OUTBUF_SIZE, "/cli: the reply is longer than four outbufs");
	CHECK(stream_len - off == len && !memcmp(stream + off, want, len),
	      "/cli: every line, once and in order (no truncation)");
	CHECK(fin_seen, "/cli: the reply ends at close");
	client_send(TCP_FIN | TCP_ACK, NULL, 0);	/* the client's side of it */
	cli_model();
	CHECK(!webcmd_busy && !webcmd_wait && !cli_running, "/cli: nothing left waiting");

	/* a short failing /cli is still 200: its status lines are in the body */
	cli_lines = 2;
	cli_err = 1;
	cli_post("/cli", "set port.1.mtu x");
	cli_drive(0);
	CHECK(!memcmp(stream, "HTTP/1.1 200 OK\r\n", 17), "/cli: a failure is 200, the body says");

	/* /cmd keeps its status when the reply fits, and can't once it has gone */
	cli_post("/cmd", "bogus");
	cli_drive(0);
	CHECK(!memcmp(stream, "HTTP/1.1 400 NO\r\n", 17), "/cmd: a short failure is still 400 NO");
	cli_lines = 300;
	cli_post("/cmd", "bogus");
	cli_drive(0);
	len = cli_expect(want);
	off = body_offset();
	CHECK(!memcmp(stream, "HTTP/1.1 200 OK\r\n", 17) && stream_len - off == len
	      && !memcmp(stream + off, want, len),
	      "/cmd: a long reply streams whole too (its header went out first)");
	cli_err = ERR_OK;
}

/* A step that prints more than outbuf's room is cut where it overflowed,
 * marked there, and the steps after it arrive whole */
static void scenario_cli_overflow(void)
{
	char line[64], *body, *at, *mark;
	int n, ok = 1;

	cli_lines = 200;
	cli_big = 100;
	cli_err = ERR_OK;
	cli_post("/cli", "get state.port");
	cli_drive(0);
	cli_big = -1;
	stream[stream_len < STREAM_MAX ? stream_len : STREAM_MAX - 1] = 0;
	body = (char *)stream + body_offset();
	mark = strstr(body, CMD_TRUNCATED);
	CHECK(mark && !strstr(mark + 1, CMD_TRUNCATED), "/cli overflow: marked once");
	at = body;
	for (n = 0; n < cli_lines; n++) {
		if (n == 100) {
			ok &= mark && mark > at;
			at = mark ? mark + strlen(CMD_TRUNCATED) : at;
			continue;
		}
		cli_line(line, n);
		at = strstr(at, line);
		if (!at) {
			ok = 0;
			break;
		}
		at += strlen(line);
	}
	CHECK(ok, "/cli overflow: every other line in order, the mark where the big one was cut");
	CHECK(fin_seen, "/cli overflow: the reply ends at close");
}

static void scenario_cli_dropped(void)
{
	static char want[STREAM_MAX];
	int off, len;

	cli_lines = 400;
	cli_err = ERR_OK;
	cli_post("/cli", "get state.port");
	cli_drive(3000);		/* the client goes, the reply half sent */
	CHECK(!webcmd_busy && !webcmd_wait && !webcmd_pending && !webcmd_done,
	      "/cli dropped: httpd forgets the reply");
	cli_model();
	CHECK(!cli_running, "/cli dropped: cli stops its run");

	/* the next requests are served whole */
	session_start(MSS_FULL);
	request_file(MSS_FULL);
	drain(MSS_FULL);
	CHECK(first_body_mismatch() == -1 && stream_len >= body_offset() + FILE_LEN,
	      "/cli dropped: the next GET is served whole");
	cli_post("/cli", "get state.port");
	cli_drive(0);
	len = cli_expect(want);
	off = body_offset();
	CHECK(fin_seen && stream_len - off == len && !memcmp(stream + off, want, len),
	      "/cli dropped: the next /cli is whole");
}

/* do sys.reset over POST /cli: cli queues the cause; the reply, longer than
 * outbuf and ended by the close, goes out whole, and the switch resets once
 * the connection has closed */
static void scenario_cli_reset_after_ack(void)
{
	static char want[STREAM_MAX];
	int off, len;

	reset_calls = 0;
	cli_lines = 300;
	cli_err = ERR_OK;
	cli_post("/cli", "do sys.reset");
	crash_pending = CRASH_WEB;	/* what its handler queues */
	cli_drive(0);
	len = cli_expect(want);
	off = body_offset();
	CHECK(fin_seen && reset_calls == 0 && len > TCP_OUTBUF_SIZE && stream_len - off == len
	      && !memcmp(stream + off, want, len), "/cli do sys.reset: the whole reply and its FIN, no reset yet");
	client_send(TCP_FIN | TCP_ACK, NULL, 0);
	CHECK(reset_calls == 1 && reset_cause == CRASH_WEB && reset_len == stream_len,
	      "/cli do sys.reset: the reset at the close, cause web");
	crash_pending = 0;
}

static void scenario_side_effect_gets(void)
{
	static const char *gets[] = { "/reset", "/cmd_log_clear", "/l2_del.json?idx=7" };
	char req[256];

	reset_calls = 0;
	history_clears = 0;
	l2_deleted = -1;
	for (int i = 0; i < 3; i++) {
		int n = sprintf(req, "GET %s HTTP/1.1\r\nHost: sw\r\nCookie: session=0123456789ab\r\n\r\n", gets[i]);

		session_start(MSS_FULL);
		strcpy(session_id, "0123456789ab");
		client_send(TCP_ACK | TCP_PSH, req, n);
		drain(MSS_FULL);
		CHECK(stream_contains("404 Not found"), gets[i]);
	}
	CHECK(!reset_calls && !history_clears && l2_deleted < 0, "GET: none of them changes anything");

	cli_post("/cmd_log_clear", "");
	drain(MSS_FULL);
	CHECK(history_clears == 1 && stream_contains("HTTP/1.1 200 OK"), "POST /cmd_log_clear clears the log");
	cli_post("/l2_del.json?idx=7", "");
	drain(MSS_FULL);
	CHECK(l2_deleted == 7, "POST /l2_del.json?idx=7 deletes entry 7");

	/* without a session: 401, nothing done */
	session_start(MSS_FULL);
	strcpy(session_id, "0123456789ab");
	client_send(TCP_ACK | TCP_PSH, "POST /reset HTTP/1.1\r\nContent-Type: text/plain\r\nContent-Length: 0\r\n\r\n", (int)strlen("POST /reset HTTP/1.1\r\nContent-Type: text/plain\r\nContent-Length: 0\r\n\r\n"));
	drain(MSS_FULL);
	CHECK(stream_contains("401 Unauthorized") && !reset_calls, "POST /reset without a session: 401");
	session_start(MSS_FULL);
	client_send(TCP_ACK | TCP_PSH, "POST /cli HTTP/1.1\r\nContent-Type: text/plain\r\nContent-Length: 3\r\n\r\nget", (int)strlen("POST /cli HTTP/1.1\r\nContent-Type: text/plain\r\nContent-Length: 3\r\n\r\nget"));
	drain(MSS_FULL);
	CHECK(stream_contains("401 Unauthorized") && !webcmd_pending, "POST /cli without a session: 401");
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "-v"))
		verbose = 1;

	printf("== httpd: accounting for the bytes actually sent ==\n");
	scenario_steady_window();
	scenario_shrinking_window();
	scenario_growing_window();
	scenario_rexmit_after_shrink();
	scenario_bad_l4_checksum();
	scenario_malformed_tcp_options();
	scenario_oversized_data_offset();
	scenario_mss_zero();
	scenario_urg_inline();
	scenario_upload_reset_owner();
	scenario_fragmented_post_header();
	scenario_split_content_type();
	scenario_split_header_terminator();
	scenario_header_then_body();
	scenario_header_with_partial_body();
	scenario_post_header_timeout();
	scenario_oversized_post_header();
	scenario_fragmented_missing_content_length();
	scenario_reset_after_ack();
	scenario_side_effect_gets();
	scenario_cli_long_reply();
	scenario_cli_overflow();
	scenario_cli_dropped();
	scenario_cli_reset_after_ack();
	scenario_fragmented_get_cookie();
	scenario_fragmented_get_cookie_value();
	scenario_split_get_header_terminator();
	scenario_fragmented_get_without_cookie();
	scenario_get_header_timeout();
	scenario_oversized_get_header();
	scenario_split_method_names();
	scenario_config_upload();
	scenario_upload("octet-stream control", "Content-Type: application/octet-stream\r\n", 0);
	scenario_upload("MacBinary, split part terminator", "Content-Type: application/macbinary\r\n", 1);
	scenario_upload("no part Content-Type", "", 0);
	scenario_upload_cr_before_boundary();
	scenario_upload_last_byte();
	scenario_upload_broken_matches();
	scenario_upload_window();
	scenario_upload_window_race();
	scenario_upload_during_other_job();
	scenario_flash_busy();

	scenario_login_with_valid_cookie_reuses_its_slot();
	scenario_login_without_cookie_replaces_least_recently_used();

	printf("\n%s (%d failure%s)\n",
	       failures ? "BENCH: FAILURES" : "BENCH: ALL PASS",
	       failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
