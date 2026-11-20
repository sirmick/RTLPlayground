/*
 * om.c - the object model's engine (DESIGN.md section 12): the CLI verbs over
 * the generated tables.
 *
 *   get [-c] <path>...     print the fields under each path ("path value" lines)
 *   set <path> <value>     change the candidate (a keyed entry is made if need be)
 *   del <path>             a field, an instance or a keyed entry back to its default
 *   diff                   candidate vs running
 *   apply                  check the candidate, then apply what changed
 *   discard                candidate = running
 *   do <path>              run an action
 *   save                   running to flash, as binary records (the flash job)
 *   load saved|default     a flash config into the candidate
 *   get -s | -d [<path>]   the saved config; running's fields off their default
 *
 * Every command ends in one status line: "ok", or "err <path>: <why>".
 * Long output (get, diff) and apply run as the cli's job, a line or an
 * instance a step (cmd_parser.c cli_job_more()).
 *
 * An object's name is its path with '#' for each index ("sys", "port.#",
 * "vlan.#", "stp.port.#", "port.#.storm.#"): one matcher and one two-level
 * cursor serve singletons, numbered, keyed and nested objects. The kinds
 * and types a schema doesn't use are compiled out (OM_USE_*, om_gen.h).
 *
 * The 8051 rules (doc/sched/memory.md): statics in xdata, no function with a
 * second parameter (arguments in globals), typed pointers, no recursion, no
 * 32-bit arithmetic: numbers go through the byte-wise routines below.
 */
#include <stdint.h>
#include <stddef.h>
#include "rtl837x_common.h"
#include "machine.h"
#include "cmd_parser.h"
#include "rtl837x_flash.h"
#include "flash_job.h"
#include "layout.h"
#include "om_gen.h"
#include "cmd_editor.h"
#include "con_ring.h"

extern __code const struct machine machine;
extern __xdata uint8_t cmd_words_len;
extern __xdata uint8_t cmd_words_b[];
extern __code const char * __code const om_enum[];
extern __code const struct om_lim om_lim[];
extern __code const char * __code const om_msg[];
extern __xdata uint8_t flash_buf[FLASH_BUF_SIZE];
extern __xdata struct flash_region_t flash_region;
extern __xdata uint16_t crc_value;
void crc16_bank1(__xdata uint8_t *v) __naked;	/* crc16.asm: *v into crc_value */

#define RUN	((__xdata uint8_t *)&cfg_run)
#define CAND	((__xdata uint8_t *)&cfg_cand)
#define ROLL	((__xdata uint8_t *)&cfg_roll)	/* what the hardware has, until settled; else running */

/* the command */
static __xdata uint8_t c_pw;		/* the path's word */
static __xdata uint8_t c_inst;		/* an err names the instance, not the word (apply) */
static __xdata uint8_t c_cand;		/* get -c: from the candidate */
static __xdata uint8_t c_def;		/* get -d: only fields off their default, Secrets too */
/* a resolved path */
static __xdata uint8_t r_o;		/* object */
static __xdata uint8_t r_st;		/* the path is in the state tree */
static __xdata uint8_t r_fix;		/* bit 0, 1: level 0, 1's index is given (else all) */
static __xdata uint16_t r_v;		/* level 0's index given, as cv holds it */
static __xdata uint8_t * __xdata r_path;	/* the path, past "state." */
static __xdata uint8_t * __xdata r_rest;	/* the rest of the path: the field part */
static __xdata uint8_t * __xdata p;	/* parse pointer */
/* copies of the current descriptors: fixed addresses are much smaller code */
static __xdata struct om_obj obx;
static __xdata struct om_field fdx;
static __xdata uint8_t ft;		/* fdx's type */
/* the cursor: an instance of obx */
static __xdata uint16_t cv;		/* level 0: front-panel number, number from 0, key, word */
static __xdata uint8_t ce;		/* level 0's element: logical port, number, slot */
#if OM_USE_IX2
__xdata uint8_t om_ix1;		/* level 1: number from 0, word */
static __xdata uint8_t r_v1;		/* as given */
__xdata uint16_t om_key;		/* a streamed object's instance (om.h) */
#if OM_USE_STREAM
static __xdata uint8_t r_open;		/* K: instances left, + 1 (K 1, K- OM_PAGE); 0 every one */
static __xdata uint8_t r_land;		/* K: the instance found must be K */
#endif
#endif
static __xdata uint8_t lv, lk, ln, la;	/* a level, its kind, count, arg */
static __xdata uint8_t cur_e, cur_l, cur_f, cur_ok;	/* element, logical port, field, valid */
static __xdata uint8_t * __xdata vp;	/* the current value */
static __code const char * __xdata np;	/* the object's name, being walked */
static __xdata uint8_t e_star;		/* emit_obj: '*' for the indices */
/* the job */
#define J_GET	1
#define J_DIFF	2
#define J_SAVED	3	/* get -s: the sector loaded into the candidate, printed as get -d */
#define J_SAVE	4	/* the cli waits for the flash job */
#define J_LOAD	5	/* a load, 256 B of records a step */
#define J_APPLY	6	/* J_SAVE on: the job runs to its end (CLI_JOB_OMAPPLY) */
#define J_COPY	7	/* discard: the queued copy */
#define J_CHECK	8	/* apply's checks, CHECK_N instances a step; then J_APPLY */
#define J_SET	9	/* set and del, SET_N instances a step (a get's cli job: a window waits) */
static __xdata uint8_t j_mode, j_o, j_new, j_quiet, j_in;
static __xdata uint8_t j_pr;		/* get -d: a field of this instance printed */
static __xdata uint8_t kd;		/* a default entry */
static __xdata uint8_t j_sv;		/* get -s: printing the loaded sector (bin_line()) */
static __xdata uint8_t j_hit;		/* get, this path: 1 a field matched, 2 done, 3 failed */
/* numbers */
static __xdata uint8_t num[8], num_len, dig[21], k, i, v8, w;
static __xdata uint8_t pn, sg, pf;	/* a port mask's front number; a sign; path_err's separator */
static __xdata uint8_t r_first;	/* the first object a get or set path matched */
static __xdata uint8_t zero;
static __xdata uint16_t v16, pm;	/* pm: a port mask being built */
static __code const uint8_t * __xdata cp;
__xdata uint8_t om_val[OM_VMAX], om_fid;	/* a parsed or derived value */

static __code const uint8_t tsize[] = {	/* by type; strings: their length + 1 */
	0, 1, 1, 2, 4, 8, 1, 0, 2, 2, 0, 4, 4, 6, 1, 2, 0, 2, 2, 1, 8
};

/* ---- small helpers ---------------------------------------------------- */

static void sel_obj(uint8_t n)
{
	memcpyc((__xdata uint8_t *)&obx, (__code const uint8_t *)&om_obj[n], sizeof(obx));
}

static void sel_field(void)		/* obx's field cur_f */
{
	memcpyc((__xdata uint8_t *)&fdx, (__code const uint8_t *)&obx.f[cur_f], sizeof(fdx));
	ft = fdx.tw & T_TYPE;
}

static void word(uint8_t n)		/* p at the command's word n */
{
	p = cmd_buffer + cmd_words_b[n];
}

static uint8_t ch(void)			/* the character at p: a call is much smaller code */
{
	return *p;
}

static uint8_t take(void)		/* the character at p, and past it */
{
	return *p++;
}

static uint8_t nc(void)			/* the character at np */
{
	return *np;
}

static uint8_t at_end(void)		/* p at the end of a word */
{
	return ch() == ' ' || ch() == 0;
}

/* the segment at p equals s (to '.', ',', ' ' or NUL): advance to its end */
static uint8_t seg_is(__code const char *s)
{
	__xdata uint8_t *q = p;
	while (*s && *q == *s) {
		q++;
		s++;
	}
	if (*s || !(*q == '.' || *q == ',' || *q == ' ' || *q == 0))
		return 0;
	p = q;
	return 1;
}

static void words(uint8_t e)		/* cp at om_enum[e]'s first word */
{
	cp = (__code const uint8_t *)om_enum[e];
}

static void skip1(void)			/* cp past a word */
{
	while (*cp++);
}

static void print_words(void)		/* cp's words, space-separated */
{
	while (*cp) {
		print_string((__code const char *)cp);
		skip1();
		if (*cp)
			write_char(' ');
	}
}

/* a front-panel number's logical port, or 0xff */
static uint8_t log_of(uint8_t n)
{
	if (n < 1 || n > 9)
		return 0xff;
	n = machine.phys_to_log_port[n - 1];
	return MACHINE_PORT_VALID(n) ? n : 0xff;
}

static uint8_t om_when(void)		/* fd exists on cur_l */
{
	switch ((fdx.tw >> T_WHEN) & 3) {
	case W_COPPER:
		return !machine.is_sfp[cur_l];
	case W_SFP:
		return machine.is_sfp[cur_l] != 0;
	case W_10G:
		return OM_TEN_G(cur_l);
	}
	return 1;
}

static uint8_t fsize(void)
{
	if (ft == T_STR || ft == T_SECRET)
		return fdx.arg + 1;
	return tsize[ft];
}

static __code const uint8_t *def_ptr(void)	/* the element default */
{
#if OM_USE_CHIPDEF
	if ((obx.flags & OM_CHIPDEF) && machine.isRTL8373)
		return obx.def + obx.size;
#endif
	return obx.def;
}

/* the element's address in a config copy (RUN, CAND, ROLL), or in om_st */
static __xdata uint8_t *inst(__xdata uint8_t *base)
{
	if (obx.flags & OM_STATE)
		return (__xdata uint8_t *)&om_st;
	return base + obx.base + (uint16_t)cur_e * obx.size;
}

/*
 * Whole copies (DESIGN.md 5.2, P3I): one config copy into another, X_CHUNK
 * bytes a step, queued (an undo's three in a row, a discard's then an
 * undo's). The jobs step them (x_step()); the engine is busy while one is
 * queued. A step never copies or compares a whole struct cfg.
 */
#define X_CHUNK	512
#define X_CAND_RUN	0	/* the copies: destination, source */
#define X_ROLL_RUN	1
#define X_RUN_CAND	2
#define X_RUN_ROLL	3
#define X_ROLL_CAND	4
#define X_CAND_ROLL	5
static __xdata uint8_t * __code const x_dst[] = { CAND, ROLL, RUN, RUN, ROLL, CAND };
static __xdata uint8_t * __code const x_src[] = { RUN, RUN, CAND, ROLL, CAND, ROLL };
#define X_STEPS	((sizeof(struct cfg) + X_CHUNK - 1) / X_CHUNK)
#define X_LAST	(sizeof(struct cfg) - (X_STEPS - 1) * X_CHUNK)
static __xdata uint8_t x_q[4], x_n, x_i, x_c;
static __xdata uint16_t x_at;

static void x_add(uint8_t op)
{
	x_q[x_n++] = op;
}

static uint8_t x_step(void)		/* the next chunk: 1 while more are queued */
{
	if (x_i == x_n) {
		x_n = 0;
		x_i = 0;
		return 0;
	}
	x_at = (uint16_t)x_c * X_CHUNK;	/* chunk x_c of copy x_i */
	memcpy(x_dst[x_q[x_i]] + x_at, x_src[x_q[x_i]] + x_at, x_c == X_STEPS - 1 ? X_LAST : X_CHUNK);
	if (++x_c == X_STEPS) {
		x_c = 0;
		x_i++;
	}
	return 1;
}

/*
 * The instances where the candidate differs from running (state.cfg.pending,
 * the prompt's '*', save's refusal): set and del keep it an instance at a
 * time, a load recounts it, apply and discard zero it, an undo counts what
 * it puts back.
 */
static __xdata uint16_t om_pend;
static __xdata uint8_t pd;

static uint8_t inst_differs(void)	/* cur_e: the candidate's isn't running's */
{
	if (memcmp(inst(CAND), inst(RUN), obx.size))
		return 1;
	return 0;
}

static __xdata uint8_t pend_off;	/* a load: counted after it (pend_step()) */

static void pend_pre(void)		/* before an instance of the candidate changes */
{
	if (!pend_off)
		pd = inst_differs();
}

static void pend_post(void)		/* and after */
{
	if (pend_off)
		return;
	if (inst_differs())
		om_pend++;
	if (pd)
		om_pend--;
}

static void lvl_load(void)		/* level lv's kind, count, arg */
{
	lk = obx.k[lv];
	ln = obx.n[lv];
	la = obx.a[lv];
}

static void emit_obj(void)		/* the instance's path: "port.3", "vlan.10" */
{
	if (obx.flags & OM_STATE)
		print_string("state.");
	lv = 0;
	for (np = obx.name; (v8 = nc()); np++) {
		if (v8 != '#') {
			write_char(v8);
			continue;
		}
		lvl_load();
		v16 = cv;
#if OM_USE_IX2
		if (lv)
			v16 = om_ix1;
#endif
		if (e_star) {
			write_char('*');
#if OM_USE_WORD
		} else if (lk == IX_WORD) {
			words(la);
			for (v8 = v16; v8; v8--)
				skip1();
			print_string((__code const char *)cp);
#endif
		} else {
			itoa_short(v16 + (lk == IX_NUM));
		}
		lv++;
	}
}

static void print_path_word(void)	/* the command's path, as typed */
{
	word(c_pw);
	while (!at_end()) {
		write_char(ch() < 0x21 || ch() > 0x7e ? '?' : ch());
		take();
	}
}

static void err_head(void)		/* "err <path>: " */
{
	print_string("err ");
	if (c_inst)		/* apply's checks and handlers: the instance */
		emit_obj();
	else
		print_path_word();
	print_string(": ");
}

static void err_end(void)
{
	write_char('\n');
	err_status = ERR_INVALID_ARGUMENT;
}

static void fail(__code const char *why)	/* "err <path>: why"; why in this bank */
{
	err_head();
	print_string(why);
	err_end();
}

void om_fail(uint8_t msg) __banked
{
	fail(om_msg[msg]);
}

static void err_line(__code const char *s)	/* a whole err line */
{
	print_string(s);
	err_status = ERR_INVALID_ARGUMENT;
}

static void ok(void)
{
	print_string("ok\n");
}

/* ---- numbers: little-endian, byte-wise, 8/16-bit arithmetic only ------ */

static void emit_dec(void)		/* num[0..num_len) */
{
	k = 0;
	do {
		v8 = 0;			/* nonzero quotient seen */
		v16 = 0;		/* remainder */
		for (i = num_len; i--; ) {
			v16 = (v16 << 8) | num[i];
			num[i] = v16 / 10;
			v16 -= num[i] * 10;
			v8 |= num[i];
		}
		dig[k++] = '0' + v16;
	} while (v8);
}

static void print_dig(void)
{
	while (k)
		write_char(dig[--k]);
}

/* decimal digits at p into num[0..4); 0 if none or too big */
static uint8_t parse_dec(void)
{
	num[0] = num[1] = num[2] = num[3] = 0;
	if (ch() < '0' || ch() > '9')
		return 0;
	while (ch() >= '0' && ch() <= '9') {
		v16 = take() - '0';
		for (i = 0; i < 4; i++) {
			v16 += num[i] * 10;
			num[i] = v16;
			v16 >>= 8;
		}
		if (v16)
			return 0;
	}
	return 1;
}

/* num[0..4) against a limit at cp, most significant byte first: <0, 0, >0 */
static int8_t cmp_lim(void)
{
	for (i = 4; i--; ) {
		if (num[i] != cp[i])
			return num[i] < cp[i] ? -1 : 1;
	}
	return 0;
}

static uint8_t in_lim(void)		/* num[] within the limit row at cp */
{
	if (cmp_lim() < 0)
		return 0;
	cp += 4;
	if (cmp_lim() > 0)
		return 0;
	return 1;
}

static void print_lim(void)		/* the limit row at cp: "lo-hi" */
{
	for (w = 0; w < 2; w++) {
		for (i = 0; i < 4; i++)
			num[i] = cp[i];
		num_len = 4;
		emit_dec();
		print_dig();
		if (!w)
			write_char('-');
		cp += 4;
	}
}

/* ---- values: emit -------------------------------------------------------- */

static void emit_pmask(void)
{
	v16 = vp[0] | (uint16_t)vp[1] << 8;
	k = 0;				/* printed something */
	for (pn = 1; pn <= 9; pn++) {
		v8 = log_of(pn);
		if (v8 == 0xff || !(v16 & (1 << v8)))
			continue;
		/* a run of members: n, or n-m */
		i = pn;
		while (pn < 9 && (v8 = log_of(pn + 1)) != 0xff && (v16 & (1 << v8)))
			pn++;
		if (k)
			write_char(',');
		itoa_short(i);
		if (pn != i) {
			write_char('-');
			itoa_short(pn);
		}
		k = 1;
	}
	if (v16 & PMASK_CPU) {
		if (k)
			write_char(',');
		print_string("cpu");
		k = 1;
	}
	if (!k)
		print_string("none");
}

#if OM_USE_MAC || OM_USE_BRID
static void emit_mac(void)		/* 6 bytes at vp */
{
	for (i = 0; i < 6; i++) {
		if (i)
			write_char(':');
		print_byte(vp[i]);
	}
}
#endif

#if OM_USE_TZ
static void two(void)			/* k as two digits */
{
	write_char('0' + k / 10);
	write_char('0' + k % 10);
}
#endif

static void emit_value(void)		/* fd's value at vp */
{
	switch (ft) {
	case T_BOOL:
		print_string(*vp ? "on" : "off");
		return;
	case T_ENUM:
		words(fdx.arg);
		for (v8 = *vp; v8 && *cp; v8--)
			skip1();
		print_string(*cp ? (__code const char *)cp : "?");	/* past the words: ? */
		return;
#if OM_USE_SECRET
	case T_SECRET:
		if (!c_def) {
			write_char('*');
			return;
		}
#endif
		/* fall through - get -d prints a Secret as a Str */
	case T_STR:
		print_string_x((__xdata char *)vp);
		return;
	case T_PMASK:
		emit_pmask();
		return;
#if OM_USE_IPV4
	case T_IPV4: case T_MASK4:
		for (i = 0; i < 4; i++) {
			if (i)
				write_char('.');
			itoa_short(vp[i]);
		}
		return;
#endif
#if OM_USE_MAC
	case T_MAC:
		if (fdx.arg && !(vp[0] | vp[1] | vp[2] | vp[3] | vp[4] | vp[5])) {
			print_string("auto");
			return;
		}
		emit_mac();
		return;
#endif
#if OM_USE_BRID
	case T_BRID:			/* priority / 4096, the system ID extension, the MAC */
		itoa_short(vp[0] >> 4);
		write_char('.');
		itoa_short((uint16_t)(vp[0] & 15) << 8 | vp[1]);
		write_char('.');
		vp += 2;
		emit_mac();
		return;
#endif
#if OM_USE_FLAGS
	case T_FLAGS: case T_FLAGS16:
		v16 = vp[0];
		if (ft == T_FLAGS16)
			v16 |= (uint16_t)vp[1] << 8;
		k = 0;
		words(fdx.arg);
		for (v8 = 0; *cp; v8++) {
			if (v16 & ((uint16_t)1 << v8)) {
				if (k)
					write_char(',');
				print_string((__code const char *)cp);
				k = 1;
			}
			skip1();
		}
		if (!k)
			print_string("none");
		return;
#endif
#if OM_USE_PORT
	case T_PORT:
		if (*vp == 0xff)
			print_string("none");
		else if (*vp == CPU_LOGICAL_PORT)
			print_string("cpu");
		else
			itoa_short(machine.log_to_phys_port[*vp]);
		return;
#endif
#if OM_USE_I16 || OM_USE_TZ
	case T_I16: case T_TZ:
		v16 = vp[0] | (uint16_t)vp[1] << 8;
		if (v16 & 0x8000) {
			write_char('-');
			v16 = -v16;
		} else if (ft == T_TZ) {
			write_char('+');
		}
#if OM_USE_TZ
		if (ft == T_TZ) {
			k = v16 / 60;
			two();
			write_char(':');
			k = v16 % 60;
			two();
			return;
		}
#endif
		itoa_short(v16);
		return;
#endif
	}
	/* numbers */
	num_len = fsize();
	for (i = 0; i < num_len; i++)
		num[i] = vp[i];
	emit_dec();
	if (ft == T_SCALED && fdx.arg) {
		while (k <= fdx.arg)
			dig[k++] = '0';
		while (k > fdx.arg)
			write_char(dig[--k]);
		write_char('.');
	}
	print_dig();
}

static void emit_path(void)
{
	emit_obj();
	if (*fdx.name) {		/* "": the instance's one value */
		write_char('.');
		print_string(fdx.name);
	}
}

static void emit_line(void)		/* "path value" */
{
	emit_path();
	write_char(' ');
#if OM_USE_DERIVE
	if ((fdx.tw & T_DERIVED) && !*vp) {	/* empty: the derived value */
		om_fid = fdx.id;
		if (om_fns[obx.derive](cur_e) == OM_OK)
			vp = om_val;
	}
#endif
	emit_value();
	write_char('\n');
}

/* ---- values: parse --------------------------------------------------------- */

static uint8_t byte_dec(void)		/* a decimal 0-255 at p into num[0] */
{
	if (!parse_dec() || num[1] | num[2] | num[3])
		return 0;
	return 1;
}

static uint8_t pmask_item(void)	/* one "n", "n-m", "cpu", "all", "none" at p, into pm */
{
	if (seg_is("none"))
		return 1;
	if (seg_is("cpu")) {
		if (!fdx.arg)
			return 0;
		pm |= PMASK_CPU;
		return 1;
	}
	if (seg_is("all")) {
		for (pn = 1; pn <= 9; pn++)
			if ((v8 = log_of(pn)) != 0xff)
				pm |= 1 << v8;
		if (fdx.arg)
			pm |= PMASK_CPU;
		return 1;
	}
	if (!byte_dec())
		return 0;
	k = num[0];
	i = k;
	if (ch() == '-') {
		take();
		if (!byte_dec() || num[0] < k)
			return 0;
		i = num[0];
	}
	for (pn = k; pn <= i; pn++) {
		if ((v8 = log_of(pn)) == 0xff)
			return 0;
		pm |= 1 << v8;
	}
	return 1;
}

#if OM_SET_MAC || OM_SET_BRID
static uint8_t hexd(void)		/* a hex digit at p, or 0xff */
{
	uint8_t c = take();
	if (c >= '0' && c <= '9')
		return c - '0';
	c |= 0x20;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return 0xff;
}

/* aa:bb:cc:dd:ee:ff (or '-') at p into om_val[w..w+6) */
static uint8_t parse_mac(void)
{
	for (i = 0; i < 6; i++) {
		if (i) {
			if (ch() != ':' && ch() != '-')
				return 0;
			take();
		}
		v8 = hexd();
		k = hexd();
		if ((v8 | k) > 15)
			return 0;
		om_val[w + i] = v8 << 4 | k;
	}
	return 1;
}
#endif

static uint8_t str_ch(void)		/* v8 may be in fdx's Str or Secret */
{
	if (v8 < 0x20 || v8 > 0x7e || v8 == '"' || v8 == '\\')
		return 0;
	if (v8 == ' ' && ft == T_SECRET)
		return 0;
	return 1;
}

/* the value at p into om_val, checked; prints err and returns 0 if bad */
static uint8_t parse_value(void)
{
	switch (ft) {
	case T_BOOL:
		if (seg_is("on"))
			om_val[0] = 1;
		else if (seg_is("off"))
			om_val[0] = 0;
		else
			goto bad_words;
		break;
	case T_ENUM:
		words(fdx.arg);
		for (v8 = 0; *cp; v8++) {
			if (seg_is((__code const char *)cp)) {
				om_val[0] = v8;
				goto end;
			}
			skip1();
		}
		goto bad_words;
	case T_STR:
#if OM_USE_SECRET
	case T_SECRET:
#endif
		for (k = 0; (v8 = take()); k++) {
			if (k >= fdx.arg || !str_ch()) {
				fail(ft == T_SECRET ? "printable text without \" \\ or a space, at most its length" :
				     "printable text without \" or \\, at most its length");
				return 0;
			}
			om_val[k] = v8;
		}
		while (k <= fdx.arg)		/* the rest zero: equal text is equal bytes */
			om_val[k++] = 0;
		return 1;
	case T_PMASK:
		pm = 0;
		while (1) {
			if (!pmask_item()) {
				fail("ports like 1-4,9 (none, all, cpu)");
				return 0;
			}
			if (ch() != ',')
				break;
			take();
		}
		om_val[0] = pm;
		om_val[1] = pm >> 8;
		break;
#if OM_SET_IPV4
	case T_IPV4: case T_MASK4:
		for (w = 0; w < 4; w++) {
			if (w && take() != '.')
				goto bad_ip;
			if (!byte_dec())
				goto bad_ip;
			om_val[w] = num[0];
		}
		if (ft == T_MASK4) {
			v8 = 0;			/* a zero bit seen */
			for (w = 0; w < 4; w++) {
				k = om_val[w];
				if (v8 && k)
					goto bad_ip;
				if (k != 0xff) {
					if ((uint8_t)~k & (uint8_t)(~k + 1))
						goto bad_ip;
					v8 = 1;
				}
			}
		}
		break;
bad_ip:
		fail(ft == T_MASK4 ? "a.b.c.d, contiguous" : "a.b.c.d");
		return 0;
#endif
#if OM_SET_MAC
	case T_MAC:
		if (fdx.arg && seg_is("auto")) {
			memset(om_val, 0, 6);
			break;
		}
		w = 0;
		if (!parse_mac())
			goto bad_mac;
		break;
#endif
#if OM_SET_BRID
	case T_BRID:
		if (!byte_dec() || num[0] > 15 || take() != '.')
			goto bad_mac;
		om_val[0] = num[0] << 4;
		if (!parse_dec() || num[2] | num[3] || num[1] > 15 || take() != '.')
			goto bad_mac;
		om_val[0] |= num[1];
		om_val[1] = num[0];
		w = 2;
		if (!parse_mac())
			goto bad_mac;
		break;
#endif
#if OM_SET_MAC || OM_SET_BRID
bad_mac:
		fail(ft == T_BRID ? "0-15.0-4095.aa:bb:cc:dd:ee:ff" :
			fdx.arg ? "aa:bb:cc:dd:ee:ff or auto" : "aa:bb:cc:dd:ee:ff");
		return 0;
#endif
#if OM_SET_FLAGS
	case T_FLAGS: case T_FLAGS16:
		v16 = 0;
		if (!seg_is("none")) {
			while (1) {
				words(fdx.arg);
				for (v8 = 0; *cp; v8++) {
					if (seg_is((__code const char *)cp))
						break;
					skip1();
				}
				if (!*cp)
					goto bad_words;
				v16 |= (uint16_t)1 << v8;
				if (ch() != ',')
					break;
				take();
			}
		}
		om_val[0] = v16;
		om_val[1] = v16 >> 8;
		break;
#endif
#if OM_SET_PORT
	case T_PORT:
		if (fdx.arg && seg_is("none")) {
			om_val[0] = 0xff;
			break;
		}
		if (!byte_dec() || (om_val[0] = log_of(num[0])) == 0xff) {
			fail(fdx.arg ? "a port, or none" : "a port");
			return 0;
		}
		break;
#endif
#if OM_SET_I16 || OM_SET_TZ
	case T_I16: case T_TZ:		/* a sign, then the number, offset binary as its limits */
		sg = ch() == '-';
		if (sg || ch() == '+')
			take();
		if (!parse_dec() || num[2] | num[3])
			goto bad_range;
		v16 = num[0] | (uint16_t)num[1] << 8;
#if OM_SET_TZ
		if (ft == T_TZ) {	/* hh:mm, mm 00, 30 or 45 */
			k = num[0];
			if (num[1] || take() != ':' || !byte_dec() ||
			    !(num[0] == 0 || num[0] == 30 || num[0] == 45))
				goto bad_range;
			v16 = k * 60 + num[0];
		}
#endif
		if (v16 > 0x7fff + sg)
			goto bad_range;
		if (sg)
			v16 = -v16;
		v16 ^= 0x8000;
		num[0] = v16;
		num[1] = v16 >> 8;
		goto in_range;
#endif
	case T_U8: case T_U16: case T_U32:
		if (!parse_dec())
			goto bad_range;
#if OM_SET_I16 || OM_SET_TZ
in_range:
#endif
		cp = (__code const uint8_t *)&om_lim[fdx.arg];
		if (!in_lim())
			goto bad_range;
		for (i = 0; i < 4; i++)
			om_val[i] = num[i];
#if OM_SET_I16 || OM_SET_TZ
		if (ft == T_I16 || ft == T_TZ)
			om_val[1] ^= 0x80;
#endif
		break;
	default:
		fail("read only");
		return 0;
	}
end:
	if (!at_end() || (ch() == ' ' && p[1])) {
		fail("one value");
		return 0;
	}
	return 1;
bad_words:
	err_head();
	if (ft == T_BOOL) {
		print_string("on off");
	} else {
		words(fdx.arg);
		print_words();
	}
	err_end();
	return 0;
bad_range:
	err_head();
	cp = (__code const uint8_t *)&om_lim[fdx.arg];
#if OM_SET_I16 || OM_SET_TZ
	if (ft == T_I16 || ft == T_TZ) {	/* "lo..hi", as the values print */
		for (w = 0; w < 2; w++) {
			om_val[0] = cp[0];
			om_val[1] = cp[1] ^ 0x80;
			vp = om_val;
			emit_value();
			if (!w)
				print_string("..");
			cp += 4;
		}
	} else
#endif
	print_lim();
	err_end();
	return 0;
}

/* ---- the cursor ---------------------------------------------------------- */

#if OM_USE_KEY
static __xdata uint8_t j_copies;	/* where entries are looked for: 1 running, 2 candidate, */
#define K_SORT	4			/* and in key order (get, diff); else in slot order */
static __xdata uint8_t ks, kb;
static __xdata uint16_t kv, kbest;

static uint16_t key_at(__xdata uint8_t *base)	/* slot ks's key in a copy */
{
	cur_e = ks;
	vp = inst(base);
	return vp[0] | (uint16_t)vp[1] << 8;
}

/* kv: slot ks's key in the copies looked at, if the path allows it; else 0 (a
 * slot's key is the same in running and the candidate, or 0 in one: key_new()) */
static void slot_key(void)
{
	kv = 0;
	if (j_copies & 1)
		kv = key_at(RUN);
	if (!kv && (j_copies & 2))
		kv = key_at(CAND);
	if ((r_fix & 1) && kv != r_v)
		kv = 0;
}

/*
 * The next entry, as cv (its key) and ce (its slot). In key order, the
 * smallest key at or after cv: a pass over the slots (get and diff, which
 * print, and step an instance at a time). In slot order, the next slot in
 * use after ce (cv 0: the first): set, del, do and the checks walk every
 * entry in one step.
 */
static uint8_t key_step(void)
{
	if (!(j_copies & K_SORT)) {
		for (ks = cv ? ce + 1 : 0; ks < obx.n[0]; ks++) {
			slot_key();
			if (kv) {
				cv = kv;
				ce = ks;
				return 1;
			}
		}
		return 0;
	}
	kbest = 0xffff;
	kb = 0xff;
	for (ks = 0; ks < obx.n[0]; ks++) {
		slot_key();
		if (kv && kv >= cv && kv < kbest) {
			kbest = kv;
			kb = ks;
		}
	}
	if (kb == 0xff)
		return 0;
	cv = kbest;
	ce = kb;
	return 1;
}
#endif

static uint8_t call(uint8_t fn)
{
	return om_fns[fn](cur_e);
}

/* level 0's next index at or after cv that the path allows: 0 if none */
static uint8_t step0(void)
{
	lk = obx.k[0];
#if OM_USE_KEY
	if (lk == IX_KEY)
		return key_step();
#endif
#if OM_USE_STREAM
	/* a streamed level: the getter finds the instance at or after cv; K
	 * must land on K, K- is a page of OM_PAGE from K on */
	if (lk == IX_STREAM) {
		if ((r_fix & 1) && cv < r_v)
			cv = r_v;
		if (r_open && !--r_open)
			return 0;
		om_key = cv;
		if (call(obx.get) || (r_land && om_key != r_v))
			return 0;
		cv = om_key;
		return 1;
	}
#endif
	if (r_fix & 1) {
		if (cv > r_v)
			return 0;
		cv = r_v;
	}
	for (; cv < obx.n[0] + (lk == IX_PORT); cv++) {
		ce = cv;
		if (lk != IX_PORT || (ce = log_of(cv)) != 0xff)
			return 1;
	}
	return 0;
}

#if OM_USE_IX2
static uint8_t step1(void)		/* level 1's, at or after om_ix1 */
{
	if (r_fix & 2) {
		if (om_ix1 > r_v1)
			return 0;
		om_ix1 = r_v1;
	}
	return om_ix1 < obx.n[1];
}
#endif

static void elem(void)			/* the cursor's element */
{
	cur_l = ce;
	cur_e = ce;
#if OM_USE_IX2
	cur_e = ce * obx.n[1] + om_ix1;
#endif
}

static void first_inst(void)		/* cur_ok: there is one */
{
	cv = 0;
	cur_ok = step0();
#if OM_USE_IX2
	om_ix1 = 0;
	step1();
#endif
	elem();
}

static void next_inst(void)
{
#if OM_USE_IX2
	om_ix1++;
	if (step1()) {
		cur_e++;
		return;
	}
	om_ix1 = 0;
	step1();
#endif
	cv++;
	cur_ok = step0();
	elem();
}


/* ---- paths ------------------------------------------------------------ */

static void path_at(void);

static void path_start(void)		/* the path word c_pw: its tree */
{
	word(c_pw);
	path_at();
}

static __code const char s_state[] = "state";

static void path_at(void)		/* the path at p */
{
	r_st = OM_CFG;
	if (seg_is(s_state)) {
		r_st = OM_STATE;
		if (ch() == '.')
			take();
	}
	r_path = p;
}

/* level lv's index at p into r_v (r_v1); prints err and returns 0 if bad */
static uint8_t ix_parse(void)
{
	lvl_load();
#if OM_USE_WORD
	if (lk == IX_WORD) {
		words(la);
		for (v16 = 0; *cp; v16++) {
			if (seg_is((__code const char *)cp))
				goto got;
			skip1();
		}
		goto bad;
	}
#endif
	if (!parse_dec() || num[2] || num[3])
		goto bad;
#if OM_USE_STREAM
	if (lk == IX_STREAM) {
		r_open = 2;
		r_land = 1;
		if (ch() == '-') {
			take();
			r_open = OM_PAGE + 1;
			r_land = 0;
		}
	}
#endif
	if (!(ch() == '.' || at_end()))
		goto bad;
	v16 = num[0] | (uint16_t)num[1] << 8;
	if (lk == IX_PORT) {
		if (num[1] || log_of(num[0]) == 0xff)
			goto bad;
	}
#if OM_USE_NUM || OM_USE_IDX
	else if (lk == IX_NUM || lk == IX_IDX) {
		if (lk == IX_NUM)
			v16--;		/* 0: past ln */
		if (v16 >= ln)
			goto bad;
	}
#endif
#if OM_USE_KEY || OM_USE_STREAM
	else {
		cp = (__code const uint8_t *)&om_lim[la];
		if (!in_lim())
			goto bad;
	}
#endif
#if OM_USE_WORD
got:
#endif
#if OM_USE_IX2
	if (lv) {
		r_v1 = v16;
		return 1;
	}
#endif
	r_v = v16;
	return 1;
bad:
	err_head();
	if (lk == IX_PORT) {
		print_string("no such port");
#if OM_USE_NUM
	} else if (lk == IX_NUM) {
		print_string("1-");
		itoa_short(ln);
#endif
#if OM_USE_IDX
	} else if (lk == IX_IDX) {
		print_string("0-");
		itoa_short(ln - 1);
#endif
#if OM_USE_KEY || OM_USE_STREAM
	} else if (lk == IX_KEY || lk == IX_STREAM) {
		cp = (__code const uint8_t *)&om_lim[la];
		print_lim();
#endif
#if OM_USE_WORD
	} else {
		words(la);
		print_words();
#endif
	}
	err_end();
	return 0;
}

/*
 * obx's name against the path: 1 if the path names obx, an instance or a
 * field in it, or a prefix of obx's name ("stp" names stp.port.#), with the
 * indices in r_fix/r_v and the field part in r_rest; 0 if not; 2 after
 * printing err for a bad index.
 */
static uint8_t match(void)
{
	if ((obx.flags & OM_STATE) != r_st)
		return 0;
	p = r_path;
	np = obx.name;
	lv = 0;
	r_fix = 0;
#if OM_USE_STREAM
	r_open = r_land = 0;
#endif
	while (nc()) {
		if (at_end()) {
			if (nc() == '.' || np == obx.name)
				break;		/* a prefix: every instance */
			return 0;
		}
		if (nc() != '#') {
			if (take() != nc())
				return 0;
			np++;
			continue;
		}
		np++;
		if (ch() == '*') {
			take();
		} else if ((!OM_USE_WORD || obx.k[lv] != IX_WORD) && (ch() < '0' || ch() > '9')) {
			p--;			/* none ("port.pvid"): every one; back to the '.' */
		} else {
			if (!ix_parse())
				return 2;
			r_fix |= lv + 1;
		}
		lv++;
	}
	if (ch() == '.')
		take();
	else if (!at_end())
		return 0;
	r_rest = p;
	return 1;
}

/* fd's name against the rest of the path: exact, or (prefix) under it */
static uint8_t fmatch(uint8_t exact)
{
	__xdata uint8_t *q = r_rest;
	__code const char *s = fdx.name;
	while (!(*q == ' ' || *q == 0) && *q == *s) {
		q++;
		s++;
	}
	if (!(*q == ' ' || *q == 0))
		return 0;
	cp = (__code const uint8_t *)s;	/* the rest of the name (om_tab()) */
	if (!*s)
		return 1;
	return !exact && (q == r_rest || *s == '.');
}

static uint8_t has_field(void)		/* a field of obx's under the rest of the path */
{
	for (cur_f = 0; cur_f < obx.nf; cur_f++) {
		sel_field();
		if (ft != T_ACT && fmatch(0))
			return 1;
	}
	return 0;
}

/* err <path>: the fields of the object it named first, else the objects */
static void path_err(void)
{
	err_head();
	if (r_first != 0xff) {
		sel_obj(r_first);
		for (cur_f = 0; cur_f < obx.nf; cur_f++) {
			sel_field();
			if (cur_f)
				write_char(' ');
			print_string(fdx.name);
		}
	} else {
		e_star = 1;
		pf = 0;
		for (r_o = 0; r_o < OM_N_OBJ; r_o++) {
			sel_obj(r_o);
			if ((obx.flags & OM_STATE) == r_st) {
				if (pf)
					write_char(' ');
				emit_obj();
				pf = 1;
			}
		}
		e_star = 0;
	}
	err_end();
}

/* ---- the job: get, diff and apply, a line or an instance a step ---------- */

static void job_inst(void)		/* entering an instance */
{
	if ((obx.flags & OM_STATE) && obx.get != 0xff
#if OM_USE_STREAM
	    && obx.k[0] != IX_STREAM		/* step0 has filled om_st */
#endif
	    ) {
#if OM_USE_IX2
		cur_e = cur_l;			/* level 0's element; level 1's is om_ix1 */
#endif
		call(obx.get);
	}
	cur_f = 0;
	j_in = 3;
	j_pr = 0;
#if OM_USE_KEY
	/* diff: an entry made or deleted is a line of its own */
	if (j_mode == J_DIFF && obx.k[0] == IX_KEY) {
		ks = ce;
		j_in = (key_at(RUN) == cv) | (key_at(CAND) == cv) << 1;
		cur_e = ce;
		if (j_in != 3) {
			print_string(j_in == 1 ? "- " : "+ ");
			emit_obj();
			write_char('\n');
		}
	}
#endif
}

static void job_start(uint8_t mode)
{
	j_mode = mode;
	cli_job = mode >= J_SAVE ? CLI_JOB_OMAPPLY : CLI_JOB_OM;
	om_cut = 0;
	j_new = 1;
	j_hit = 0;
	r_first = 0xff;
#if OM_USE_KEY
	if (mode == J_DIFF)
		j_copies = 3 | K_SORT;
	if (mode == J_APPLY)
		j_copies = 3;
#endif
}

static uint8_t f_diff(void);
static void inst_def(void);
#if OM_USE_KEY
static void key_dels(void);
#endif

/* the next field to print, or 0 at the end of the instance */
static uint8_t next_field(void)
{
	for (; cur_f < obx.nf; cur_f++) {
		sel_field();
		if (ft == T_ACT || !om_when())
			continue;
		if (j_mode == J_GET) {
			if (fmatch(0) && (!c_def || f_diff()) && !(j_sv && ft == T_SECRET))
				return 1;
		} else {
			vp = inst(RUN) + fdx.off;
			if (memcmp(vp, inst(CAND) + fdx.off, fsize()))
				return 1;
		}
	}
	return 0;
}

/*
 * From j_o: the next object the job takes, at its first instance; 0 at the
 * end, or (j_hit 3) after an error. get checks each path as it goes: one
 * that names nothing (or no field on the port it names) ends the job in err.
 */
static uint8_t next_obj(void)
{
	while (1) {
		for (; j_o < OM_N_OBJ; j_o++) {
			sel_obj(j_o);
			if (j_mode == J_GET) {
				path_start();
				v8 = match();
				if (v8 == 2)
					goto fail;
				if (!v8)
					continue;
				if (r_first == 0xff)
					r_first = j_o;
				if (!has_field())
					continue;
				j_hit |= 1;
				if (c_def) {
					j_hit = 2;	/* -d: a path may have nothing off its default */
#if OM_USE_KEY
					if (obx.k[0] == IX_KEY)
						key_dels();
#endif
				}
			} else if (obx.flags & OM_STATE) {
				continue;
			} else {
				r_fix = 0;
			}
			first_inst();
			if (cur_ok) {
				job_inst();
				return 1;
			}
			if ((r_fix & 1)
#if OM_USE_STREAM
			    && (r_land || !r_open)	/* an empty page: nothing to print */
#endif
			    ) {
				fail("no such entry");
				goto fail;
			}
			j_hit = 2;		/* no entries: nothing to print */
		}
		if (j_mode != J_GET)
			return 0;
		if (j_hit != 2) {
			if (j_hit)
				fail("not on this port");
			else
				path_err();
			goto fail;
		}
		if (++c_pw >= cmd_words_len)
			return 0;
		j_o = 0;
		j_hit = 0;
		r_first = 0xff;
	}
fail:
	j_hit = 3;
	return 0;
}

static void next_instance(void)
{
	next_inst();
	if (cur_ok)
		job_inst();
}

/* ---- apply: the cfg job (om_job.c) or boot, an instance a step ---------- */

/*
 * apply copies running to the rollback copy and the candidate to running,
 * then calls the apply handler of each instance where running differs from
 * the rollback copy (what the hardware has). A rollback swaps the two and
 * walks again. The walk has its own cursor (a_o, a_e) and takes the element
 * index straight: no get can run meanwhile (om_cmd() refuses while om_busy).
 */
__xdata uint8_t om_apply_st, om_confirm_s, om_confirmed, om_dirty, om_cut, om_full;
static __xdata uint8_t l_saved;		/* the candidate is the saved config, as loaded */
static uint8_t check_all(void);
static void check_start(void);
static uint8_t check_step(void);
static uint8_t set_step(void);
static void set_end(void);
static __xdata uint8_t a_o, a_e, a_n, a_fo, a_fe, a_timeout;

static void cv_of_e(void)		/* cur_e's indices, for its path in a message */
{
	ce = cur_e;
#if OM_USE_IX2
	om_ix1 = cur_e % obx.n[1];
	ce = cur_e / obx.n[1];
#endif
	cv = ce;
	if (obx.k[0] == IX_PORT)
		cv = machine.log_to_phys_port[ce];
#if OM_USE_KEY
	if (obx.k[0] == IX_KEY) {
		ks = ce;
		cv = key_at(RUN);
		if (!cv)
			cv = key_at(ROLL);	/* a deleted entry */
	}
#endif
}

static void walk_start(void)
{
	a_o = 0;
	a_e = 0;
}

static void apply_begin(void)		/* the candidate to running (queued); the walk from the top */
{
	if (om_pend)
		om_dirty = 1;
	if (l_saved)			/* what load saved put there */
		om_dirty = 0;
	om_pend = 0;
	x_add(X_ROLL_RUN);
	x_add(X_RUN_CAND);
	om_apply_st = E_CFGAPPLY_RUNNING;
	walk_start();
}

/* from (a_o, a_e): the next config element where running differs from
 * a_other (the rollback copy, or the candidate), as obx and cur_e; 0 at the end */
static __xdata uint8_t * __xdata a_other;

#define WALK_N	16			/* instances a step looks at */
static __xdata uint8_t a_left, a_r;

/* 1: found; 0: the end; 2: WALK_N looked at, the walk goes on at the next call */
static uint8_t walk_next(void)
{
	a_left = WALK_N;
	for (; a_o < OM_N_OBJ; a_o++, a_e = 0) {
		sel_obj(a_o);
		if (obx.flags & OM_STATE)
			continue;
		a_n = obx.n[0] * obx.n[1];
		while (a_e < a_n) {
			if (!a_left--)
				return 2;
			cur_e = a_e++;
			if (memcmp(inst(RUN), inst(a_other), obx.size) || (om_full && obx.k[0] != IX_KEY))
				return 1;
		}
	}
	return 0;
}

/* the next instance that changed: apply it. OM_A_MORE, OM_A_DONE, OM_A_FAIL */
uint8_t om_apply_next(void) __banked
{
	if (x_step())			/* the copies first */
		return OM_A_MORE;
	a_other = ROLL;
	a_r = walk_next();
	if (!a_r)
		return OM_A_DONE;
	if (a_r == 2)
		return OM_A_MORE;
	if (om_apply_st == E_CFGAPPLY_UNDO)
		om_pend++;		/* the candidate keeps what is undone */
	if (obx.apply == 0xff)
		return OM_A_MORE;
	c_inst = 1;			/* a handler's err names the instance */
	cv_of_e();
	if (call(obx.apply) != OM_OK) {
		a_fo = a_o;
		a_fe = cur_e;
		return OM_A_FAIL;
	}
	return OM_A_MORE;
}

/* a failure or the window's end: running back to the rollback copy; the walk re-applies */
void om_undo(void) __banked
{
	a_timeout = om_apply_st == E_CFGAPPLY_CONFIRM;
	om_apply_st = E_CFGAPPLY_UNDO;
	/* swap running and the rollback copy, through the candidate: at a failure
	 * it is running anyway, and after a window it becomes running again */
	x_add(X_CAND_RUN);
	x_add(X_RUN_ROLL);
	x_add(X_ROLL_CAND);
	om_pend = 0;
	walk_start();
}

/* the job's end: r the last walk's result; its copies are queued (om_copy()) */
void om_settle(uint8_t r) __banked
{
	if (om_apply_st == E_CFGAPPLY_UNDO) {
		om_apply_st = r == OM_A_DONE ? E_CFGAPPLY_ROLLED_BACK : E_CFGAPPLY_FAILED;
		if (a_timeout) {		/* nobody waits for this one */
			x_add(X_CAND_RUN);
			om_pend = 0;
			print_string("event state.cfg.apply rolled_back\n");
		}
	} else {
		om_apply_st = E_CFGAPPLY_DONE;
	}
	om_confirm_s = 0;
	x_add(X_ROLL_RUN);
}

uint8_t om_copy(void) __banked		/* the queued copies' next chunk: 1 while more */
{
	return x_step();
}

static uint8_t om_busy(void)		/* the cfg job is walking, or a copy is queued */
{
	if (x_n)
		return 1;
	if (om_apply_st == E_CFGAPPLY_RUNNING)	/* ifs: a || returned costs a bit */
		return 1;
	if (om_apply_st == E_CFGAPPLY_UNDO)
		return 1;
	return 0;
}

static uint8_t in_window(void)		/* a confirm window is open: err (the verb) */
{
	c_pw = 0;
	if (om_apply_st != E_CFGAPPLY_CONFIRM)
		return 0;
	fail("confirm first");
	return 1;
}

/* apply [seconds]: check the candidate, then hand it to the cfg job */
static void apply_cmd(void)
{
	c_pw = 0;
	v16 = 0;
	if (cmd_words_len > 1) {
		word(1);
		if (!parse_dec() || num[2] | num[3] || !at_end() ||
		    !(v16 = num[0] | (uint16_t)num[1] << 8) || v16 > 3600) {
			fail("1-3600 seconds");
			return;
		}
	}
	if (in_window())
		return;
	om_confirm_s = v16;
	check_start();
	job_start(J_CHECK);		/* the checks, then the walk: the cli waits, then says how it went */
}

static void apply_go(void)		/* the checks passed: hand the candidate to the cfg job */
{
	if (!om_job_start()) {
		err_line("err apply: busy\n");
		j_hit = 3;
		return;
	}
	apply_begin();
	om_confirmed = 0;
	j_mode = J_APPLY;
}

/* the cli's wait for the walk: 1 while it runs; then ok, or err with the instance */
static uint8_t apply_wait(void)
{
	if (om_busy())
		return 1;
	if (om_apply_st == E_CFGAPPLY_ROLLED_BACK || om_apply_st == E_CFGAPPLY_FAILED) {
		sel_obj(a_fo);
		cur_e = a_fe;
		cv_of_e();
		c_inst = 1;
		fail(om_apply_st == E_CFGAPPLY_FAILED ? "failed, and its rollback" : "failed, rolled back");
		j_hit = 3;
	}
	return 0;
}

static uint8_t bin_line(void);

uint8_t om_line(void) __banked
{
	if (om_cut) {			/* a rollback took the engine (om_job.c) */
		err_line("err: cut by a rollback\n");
		return 0;
	}
	if (j_mode == J_APPLY) {
		if (apply_wait())
			return 1;
		goto done;
	}
	if (j_mode == J_COPY) {
		if (x_step())
			return 1;
		goto done;
	}
	if (j_mode == J_SET) {
		if (set_step())
			return 1;
		set_end();
		j_hit = 3;		/* set_end() said it */
		goto done;
	}
	if (j_mode == J_CHECK) {
		v8 = check_step();
		if (v8 == 1)
			return 1;
		if (v8) {
			j_hit = 3;		/* refused: the check said why */
			goto done;
		}
		apply_go();
		if (j_hit == 3)
			goto done;
		return 1;
	}
	if (j_mode >= J_SAVED) {
		if (bin_line())
			return 1;
		goto done;
	}
	if (j_new) {
		j_new = 0;
		j_o = 0;
		if (!next_obj())
			goto done;
	}
	while (1) {
		if (!cur_ok) {			/* the object is done */
			j_o++;
			if (!next_obj())
				goto done;
			continue;
		}
		if (next_field()) {
			if (j_mode == J_GET) {
				vp = inst(c_cand ? CAND : RUN) + fdx.off;
				emit_line();
				j_hit = 2;
				j_pr = 1;
			} else {
				if (j_in & 1) {
					print_string("- ");
					emit_line();
				}
				if (j_in & 2) {
					vp = inst(CAND) + fdx.off;
					print_string("+ ");
					emit_line();
				}
			}
			cur_f++;
			return 1;
		}
#if OM_USE_KEY
		/* get -d: an entry with nothing off its default is one line that
		 * makes it, its first field; a default entry is just there */
		if (c_def && !j_pr && j_mode == J_GET && obx.k[0] == IX_KEY) {
			inst_def();
			cur_f = 0;
			sel_field();
			if (cp == obx.def && fmatch(0)) {
				vp = inst(c_cand ? CAND : RUN) + fdx.off;
				emit_line();
			}
		}
#endif
		next_instance();
		if (j_mode != J_APPLY)
			return 1;		/* an instance a step: a keyed one is a pass over the slots */
	}
done:
	if (j_sv) {			/* get -s printed: the candidate back, then running's copy */
		j_sv = 0;
		x_add(X_CAND_ROLL);
		x_add(X_ROLL_RUN);
		j_mode = J_COPY;
		return 1;
	}
	if (!j_quiet && j_hit != 3)
		ok();
	return 0;
}

/* ---- the verbs ---------------------------------------------------------- */

static uint8_t sector_ok(uint8_t dflt);

static void om_get(void)
{
	c_pw = 1;
	c_cand = 0;
	if (cmd_words_len > 1 && cmd_buffer[cmd_words_b[1]] == '-') {
		v8 = cmd_buffer[cmd_words_b[1] + 1];
		c_pw = 2;
		if (v8 == 's') {		/* the saved config, all of it: get -d's lines of it */
			if (in_window())
				return;
			if (sector_ok(0))
				job_start(J_SAVED);
			return;
		}
		if (v8 == 'c')
			c_cand = 1;
		else if (v8 == 'd') {
			if (in_window())
				return;
			c_pw = 2;		/* in_window()'s err names the verb */
			c_def = 1;
		}
		else
			c_pw = 1;		/* a path: err */
		if (c_def && cmd_words_len == 2) {	/* get -d: every path, as "" after the flag */
			cmd_words_b[2] = cmd_words_b[1] + 2;
			cmd_words_len = 3;
		}
	}
#if OM_USE_KEY
	j_copies = (c_cand ? 2 : 1) | K_SORT;
#endif
	if (cmd_words_len <= c_pw) {
		e_star = 1;
		for (r_o = 0; r_o < OM_N_OBJ; r_o++) {
			sel_obj(r_o);
			emit_obj();
			write_char('\n');
		}
		e_star = 0;
		ok();
		return;
	}
	job_start(J_GET);
}

/*
 * set/del/do: the object and field the path names (cur_f 0xff: del of a
 * whole instance or entry); prints err and returns 0 if none
 */
static uint8_t target(uint8_t del)
{
	c_pw = 1;
	if (cmd_words_len < 2) {
		err_line("err: a path\n");
		return 0;
	}
	path_start();
	r_first = 0xff;
	for (r_o = 0; r_o < OM_N_OBJ; r_o++) {
		sel_obj(r_o);
		v8 = match();
		if (v8 == 2)
			return 0;
		if (!v8)
			continue;
		if (r_first == 0xff)
			r_first = r_o;
		cur_f = 0xff;
		if (del && at_end())		/* p is r_rest */
			goto found;
		for (cur_f = 0; cur_f < obx.nf; cur_f++) {
			sel_field();
			if (fmatch(1))
				goto found;
		}
	}
	path_err();
	return 0;
found:
	if (obx.flags & OM_STATE) {
		fail("read only");
		return 0;
	}
#if OM_USE_KEY
	j_copies = 2;
#endif
	return 1;
}

#if OM_USE_KEY
/*
 * set on a keyed entry the candidate lacks: make it, in running's slot for
 * the key, else in a slot free in both: so a key never moves between
 * running and the candidate, and apply sees each slot's old and new entry.
 * 0 if no slot is free.
 */
static __xdata uint8_t * __xdata kp;

static uint8_t key_new(void)
{
	kb = 0xff;
	kp = RUN + obx.base;		/* running's slots, a pointer: a key a few instructions */
	for (ks = 0; ks < obx.n[0]; ks++, kp += obx.size) {
		kv = kp[0] | (uint16_t)kp[1] << 8;
		if (kv == r_v) {
			kb = ks;
			break;
		}
		if (!kv && kb == 0xff && !key_at(CAND))
			kb = ks;
	}
	if (kb == 0xff)
		return 0;
	cur_e = kb;
	pend_pre();
	vp = inst(CAND);
	memcpyc(vp, obx.def, obx.size);
	vp[0] = r_v;
	vp[1] = r_v >> 8;
	pend_post();
	return 1;
}
#endif

#if OM_USE_KEY
/* get -d: "del <path>" for each default entry the copy lacks, before the
 * object's other lines (a replay of them can't make it again) */
static __code const uint8_t * __xdata kdp;

static void def_next(void)		/* the next default entry (kdp from obx.def): its key in kbest */
{
	kdp += obx.size;
	kbest = kdp[0] | (uint16_t)kdp[1] << 8;
}

static __xdata uint8_t kf;
static __xdata uint16_t kr, kk;

static void key_dels(void)
{
	kf = r_fix;			/* the path's, for next_obj() */
	kr = r_v;
	kdp = obx.def;
	for (kd = 0; kd < obx.ndef; kd++) {
		def_next();
		kk = kbest;		/* first_inst() uses kbest */
		r_fix = 1;		/* the entry, looked up as a path naming it */
		r_v = kk;
		first_inst();
		if (cur_ok)
			continue;
		cv = kk;
		print_string("del ");
		emit_obj();
		write_char('\n');
	}
	r_fix = kf;
	r_v = kr;
}

/* del of a keyed object, after every entry is gone: its default entries again */
static uint8_t key_defs(void)
{
	cp = obx.def;
	for (k = 0; k < obx.ndef; k++) {
		cp += obx.size;
		r_v = cp[0] | (uint16_t)cp[1] << 8;
		if (!key_new())
			return 0;
		pend_pre();
		memcpyc(vp, cp, obx.size);
		pend_post();
	}
	return 1;
}
#endif

/*
 * set and del's instances from the cursor, SET_N a step (each two compares
 * and a copy: 130 us a VLAN): 1 while there are more. j_del 2: del of a
 * keyed object, its slots from ks, then its default entries.
 */
#define SET_N	16
static __xdata uint8_t j_del, j_any;

static uint8_t set_step(void)
{
	a_left = SET_N;
#if OM_USE_KEY
	if (j_del == 2) {
		for (; ks < obx.n[0]; ks++) {
			if (!a_left--)
				return 1;
			if (!key_at(CAND))	/* cur_e is ks: a free slot is the default already */
				continue;
			pend_pre();
			memcpyc(inst(CAND), obx.def, obx.size);
			pend_post();
		}
		j_any = key_defs();
		return 0;
	}
#endif
	for (; cur_ok; next_inst()) {
		if (cur_f != 0xff && !om_when())
			continue;
		if (!a_left--)
			return 1;
		pend_pre();
		vp = inst(CAND);
		if (cur_f == 0xff) {
			memcpyc(vp, def_ptr(), obx.size);
		} else {
			vp += fdx.off;
			if (j_del)
				memcpyc(vp, def_ptr() + fdx.off, k);
			else
				memcpy(vp, om_val, k);
		}
		pend_post();
		j_any = 1;
	}
	return 0;
}

static void set_end(void)		/* ok, or why not */
{
	if (j_any)
		ok();
	else
		fail(j_del == 2 ? "table full" : "not on this port");
}

static void om_set(uint8_t del)
{
	if (!target(del))
		return;
	l_saved = 0;
	if (cur_f != 0xff && ft == T_ACT) {
		fail(del ? "an action" : "an action: do it");
		return;
	}
	if (!del) {
		p = cmd_words_len > 2 ? cmd_buffer + cmd_words_b[2] : &zero;
		if (!parse_value())
			return;
	}
	j_del = del;
	j_any = 0;
#if OM_USE_KEY
	if (cur_f == 0xff && obx.k[0] == IX_KEY && !(r_fix & 1)) {
		j_del = 2;
		ks = 0;
		goto step;
	}
#endif
	first_inst();
#if OM_USE_KEY
	if (!cur_ok && !del && (r_fix & 1)) {
		if (!key_new()) {
			fail("table full");
			return;
		}
		first_inst();
	}
#endif
	if (!cur_ok) {
		fail("no such entry");
		return;
	}
	k = fsize();
step:
	if (set_step()) {
		job_start(J_SET);
		cli_job = CLI_JOB_OM;
		return;
	}
	set_end();
}

static void om_do(void)
{
	if (!target(0))
		return;
	if (ft != T_ACT) {
		fail("not an action");
		return;
	}
	for (first_inst(); cur_ok; next_inst()) {
		if (!om_when()) {
			if (r_fix & 1) {
				fail("not on this port");
				return;
			}
			continue;
		}
		if (call(fdx.arg) != OM_OK)
			return;
	}
	ok();
}

/* every instance's check, on the candidate: CHECK_N instances a step from
 * (r_o, its cursor). 1: more; 0: all passed; 2: one refused (err printed) */
#define CHECK_N	8
static __xdata uint8_t c_go;		/* r_o's instances are being walked */

static void check_start(void)
{
	r_o = 0;
	c_go = 0;
}

static uint8_t check_step(void)
{
	c_inst = 1;
#if OM_USE_KEY
	j_copies = 3;		/* entries deleted in the candidate too: a check may refuse it */
#endif
	r_fix = 0;
	a_left = CHECK_N;
	for (; r_o < OM_N_OBJ; r_o++, c_go = 0) {
		sel_obj(r_o);
		if ((obx.flags & OM_STATE) || obx.check == 0xff)
			continue;
		if (!c_go) {
			first_inst();
			c_go = 1;
		}
		for (; cur_ok; next_inst()) {
			if (!a_left--)
				return 1;
			if (call(obx.check) != OM_OK)
				return 2;
		}
	}
	return 0;
}

static uint8_t check_all(void)		/* at boot: at once */
{
	check_start();
	while ((v8 = check_step()) == 1);
	if (v8)
		return 0;
	return 1;
}

/* ---- the saved config: binary records (DESIGN.md 5.4, 15.3) ------------- */

/*
 * A header, "\0RC1" u16 length u16 CRC (crc16_bank1 from 0, over the
 * records); then an instance: u8 object ID, u16 index, u8 fields; a field:
 * u8 ID, u8 length, the value as in the struct (a Str without its NUL).
 * IDs are the registry's. Only the fields off their default, and only the
 * instances that have one; a keyed object is a reset (index 0) and then
 * every entry. The index: 0 for a singleton, the front-panel number, the
 * number from 1, the key, the word; a second level in the low byte.
 *
 * The save encodes whole instances from a cursor, a page's worth a step
 * (the flash job writes each); the header goes last, so a cut before it
 * leaves the sector reading blank; then the sector's CRC is read back.
 * Reading checks the CRC over every record before it takes one: 256 B a
 * step for load and get -s, all at once at boot. The decoder reads the
 * flash 16 B at a time. tools/om_gen.py fails the build if the worst case
 * can outgrow the sector.
 */
#define OM_HDR	8
#define E_ROOM	256			/* a save step starts no instance past it: a page, and */
					/* one record at most (OM_REC_MAX, tools/om_gen.py) */
static __xdata uint16_t b_len, b_crc;	/* the records' length and CRC */
static __xdata uint16_t w_len, w_crc;	/* the save's, to check the read-back against */
static __xdata uint16_t e_n, l_crc;
static __xdata uint8_t e_b, e_nf, l_phase;
/* the save's cursor: the next object, and (e_st) the instance to go on at */
static __xdata uint8_t e_o, e_st, e_ce, e_k;
static __xdata uint16_t e_cv;
#if OM_USE_IX2
static __xdata uint8_t e_cv1;
#endif
static __xdata uint8_t b_fmt;		/* rd_open()'s: E_CFGFORMAT_* */
/* state.cfg.format, save, saved_bytes, load.skipped: om_get_cfg() copies them */
static __xdata struct {
	uint8_t boot, save;		/* what boot found, E_CFGFORMAT_*; E_CFGSAVE_* */
	uint16_t saved, skipped;
} bs;
_Static_assert(offsetof(struct st_cfg, load_skipped) == offsetof(struct st_cfg, format) + 4,
	       "state.cfg's format, save, saved_bytes, load.skipped: bs's order");

static __code const uint8_t om_magic[4] = { 0, 'R', 'C', '1' };
static __xdata uint8_t e_put;

static void put(uint8_t b)		/* a record byte into flash_buf, and the CRC */
{
	e_b = b;
	crc16_bank1(&e_b);
	flash_buf[e_n++] = b;
}

/* x_k bytes at vp (xdata) and cp (code): 1 if they differ */
static __xdata uint8_t x_k, x_j;
static uint8_t code_differs(void)
{
	for (x_j = 0; x_j < x_k; x_j++)
		if (vp[x_j] != cp[x_j])
			return 1;
	return 0;
}

/* cp: the instance's default. get -d's (c_def): a default entry of a keyed
 * object has its own (vlan.1's members); else, and for save, the element's */
static void inst_def(void)
{
	cp = def_ptr();
#if OM_USE_KEY
	if (!c_def || obx.k[0] != IX_KEY)
		return;
	kdp = obx.def;
	for (kd = 0; kd < obx.ndef; kd++) {
		def_next();
		if (kbest == cv)
			cp = kdp;
	}
#endif
}

/* fdx's value in running (get -c -d: the candidate) is off its default (vp at
 * it); not for actions */
static uint8_t f_diff(void)
{
	vp = inst(c_def && c_cand ? CAND : RUN) + fdx.off;
	if (ft == T_ACT || !om_when())
		return 0;
	inst_def();
	cp += fdx.off;
	x_k = fsize();
	return code_differs();
}

static void f_len(void)			/* k: the bytes of vp's value that are saved */
{
	k = fsize();
	if (ft == T_STR || ft == T_SECRET)
		for (k = 0; k < fdx.arg && vp[k]; k++);
}

static void put16(uint16_t v)
{
	put(v);
	put(v >> 8);
}

/* e_nf: the instance's fields off their default; with e_put, their records */
static void inst_fields(void)
{
	e_nf = 0;
	for (cur_f = 0; cur_f < obx.nf; cur_f++) {
		sel_field();
		if (!f_diff())
			continue;
		e_nf++;
		if (!e_put)
			continue;
		put(fdx.id);
		f_len();
		put(k);
		for (i = 0; i < k; i++)
			put(vp[i]);
	}
}

/* every config element of d_base at its default, and the default entries
 * of keyed objects with the object's last: DEF_B bytes a step (an element
 * at least), from (d_o, d_e); 1 while there are more */
static __xdata uint8_t * __xdata d_base;
static __xdata uint8_t d_o, d_e;
static __xdata uint16_t d_b;

#define DEF_B	160			/* bytes a step: from code, ~50 instructions a byte */

static uint8_t def_step(void)
{
	d_b = 0;
	for (; d_o < OM_N_OBJ; d_o++, d_e = 0) {
		sel_obj(d_o);
		if (obx.flags & OM_STATE)
			continue;
		r_first = obx.n[0] * obx.n[1];
		while (d_e < r_first) {
			if (d_b >= DEF_B)
				return 1;
			d_b += obx.size;
			cur_e = d_e++;
			memcpyc(inst(d_base), def_ptr(), obx.size);
		}
#if OM_USE_KEY
		cp = obx.def;
		for (cur_e = 0; cur_e < obx.ndef; cur_e++) {
			cp += obx.size;
			memcpyc(inst(d_base), cp, obx.size);
		}
#endif
	}
	return 0;
}

static void def_start(void)
{
	d_o = 0;
	d_e = 0;
}

static void pend_start(void)		/* after the records */
{
	pend_off = 0;
	walk_start();
	om_pend = 0;
}

/* after a load: om_pend counted again, a walk step at a time: 1 while more */
static uint8_t pend_step(void)
{
	a_other = CAND;
	a_r = walk_next();
	if (a_r == 1)
		om_pend++;
	return a_r;
}

/*
 * The next records into flash_buf (e_n bytes), whole instances, none
 * started past E_ROOM, at most E_KN of them (each two passes over its
 * fields: 1.9 ms a VLAN), from the cursor. An instance at its defaults is
 * one compare with the element default.
 */
#define E_KN	4

static void enc_chunk(void)
{
	e_n = 0;
	e_k = 0;
	crc_value = b_crc;
	for (; e_o < OM_N_OBJ; e_o++) {
		sel_obj(e_o);
		if (obx.flags & OM_STATE)
			continue;
		r_fix = 0;
#if OM_USE_KEY
		j_copies = 1 | K_SORT;	/* entries in key order: the bytes don't hang on slots */
#endif
		if (e_st) {		/* where the last step stopped */
			cv = e_cv;
			ce = e_ce;
#if OM_USE_IX2
			om_ix1 = e_cv1;
#endif
			elem();
			cur_ok = 1;
		} else {
#if OM_USE_KEY
			if (obx.k[0] == IX_KEY) {	/* a reset: the entries follow */
				if (e_n + 4 > E_ROOM)
					goto full;
				put(obx.id);
				put16(0);
				put(0);
			}
#endif
			first_inst();
			e_st = 1;
		}
		for (; cur_ok; next_inst()) {
			vp = inst(RUN);
			cp = def_ptr();
			x_k = obx.size;
			if (!code_differs() && obx.k[0] != IX_KEY)
				continue;
			e_put = 0;
			inst_fields();
			if (!e_nf && obx.k[0] != IX_KEY)
				continue;
			if (e_n >= E_ROOM || e_k == E_KN) {	/* the next step's */
				e_cv = cv;
				e_ce = ce;
#if OM_USE_IX2
				e_cv1 = om_ix1;
#endif
				goto full;
			}
			v16 = cv;
			if (obx.k[0] == IX_NUM && obx.n[0] > 1)
				v16++;
#if OM_USE_IX2
			if (obx.n[1] > 1)
				v16 = v16 << 8 | om_ix1;
#endif
			put(obx.id);
			put16(v16);
			put(e_nf);
			e_put = 1;
			inst_fields();
			e_k++;
		}
		e_st = 0;
	}
full:
	b_crc = crc_value;
	b_len += e_n;
}

/* the reader: the flash at rd_at, 16 B at a time, from the sector at rd_base */
static __xdata uint32_t rd_at, rd_base;
static __xdata uint8_t rb[16], rb_i;
static __xdata uint16_t rd_left;	/* record bytes left */

static uint8_t rd(void)
{
	if (rb_i == sizeof(rb)) {
		flash_region.addr = rd_at;
		flash_region.len = sizeof(rb);
		flash_read_bulk(rb);
		/* within the sector: the low half never carries before its end */
		((__xdata uint16_t *)&rd_at)[0] += sizeof(rb);
		rb_i = 0;
	}
	if (rd_left)
		rd_left--;
	e_b = rb[rb_i++];
	crc16_bank1(&e_b);		/* rd_open()'s check */
	return e_b;
}

static __xdata uint16_t r16;

static uint16_t rd16(void)
{
	r16 = rd();
	r16 |= rd() << 8;
	return r16;
}

static void rd_start(void)		/* rd() from the sector's start */
{
	rd_at = rd_base;
	rb_i = sizeof(rb);
}

/* the sector at rd_base: its header. b_fmt says; ok, rd() is at the records
 * and their CRC still to check (crc_step()) */
static void rd_head(void)
{
	rd_start();
	rd_left = 0;
	b_fmt = E_CFGFORMAT_BLANK;
	v8 = rd();
	if (v8 == 0xff)
		return;
	b_fmt = E_CFGFORMAT_BAD;		/* old command text too */
	if (v8)
		return;
	for (i = 1; i < 4; i++)
		if (rd() != om_magic[i])
			return;
	b_len = rd16();
	b_crc = rd16();
	if (b_len > CONFIG_LEN - OM_HDR)
		return;
	rd_left = b_len;
	l_crc = 0;
	b_fmt = E_CFGFORMAT_OK;
}

/* the CRC over the next CRC_N B of records (a byte is a read and a banked
 * CRC call: 16 us): 1 while there are more; then b_fmt ok (rd() at the first
 * record again) or bad */
#define CRC_N	128

static uint8_t crc_step(void)
{
	crc_value = l_crc;
	for (i = 0; i < CRC_N && rd_left; i++)
		rd();
	l_crc = crc_value;
	if (rd_left)
		return 1;
	b_fmt = E_CFGFORMAT_BAD;
	if (l_crc != b_crc)
		return 0;
	rd_start();
	for (i = 0; i < OM_HDR; i++)
		rd();
	rd_left = b_len;
	b_fmt = E_CFGFORMAT_OK;
	return 0;
}

static void rd_open(void)		/* rd_head(), and the CRC at once (boot) */
{
	rd_head();
	if (b_fmt == E_CFGFORMAT_OK)
		while (crc_step());
}

#define D_END	0
#define D_INST	1
#define D_FIELD	2
static __xdata uint8_t d_oid, d_nf, d_fid, d_len, d_ok;
static __xdata uint16_t d_ix;

static uint8_t dec_next(void)		/* the next instance or field: D_* */
{
	if (!d_nf) {
		if (rd_left < 4)
			return D_END;
		d_oid = rd();
		d_ix = rd16();
		d_nf = rd();
		return D_INST;
	}
	d_nf--;
	d_fid = rd();
	d_len = rd();
	memset(om_val, 0, OM_VMAX);
	for (i = 0; i < d_len; i++) {
		v8 = rd();
		if (i < OM_VMAX)
			om_val[i] = v8;
	}
	return D_FIELD;
}

/* the record's object, and its index as a path's (r_fix, r_v, r_v1): 0 if
 * this build has no such object. A record's object is mostly the last one's
 * (d_lo, 0xff none yet): the table isn't searched again for it */
static __xdata uint8_t d_lo;

static uint8_t d_obj(void)
{
	r_o = d_lo;
	if (r_o != 0xff && om_obj[r_o].id == d_oid) {
		sel_obj(r_o);
		goto found;
	}
	for (r_o = 0; r_o < OM_N_OBJ; r_o++) {	/* the ID from the table: a descriptor copy each is slow */
		if (om_obj[r_o].id != d_oid)
			continue;
		sel_obj(r_o);
		if (!(obx.flags & OM_STATE))
			goto found;
	}
	return 0;
found:
	d_lo = r_o;
	r_fix = 1;
	r_v = d_ix;
#if OM_USE_IX2
	if (obx.n[1] > 1) {
		r_v1 = d_ix;
		r_v = d_ix >> 8;
		r_fix = 3;
	}
#endif
	if (obx.k[0] == IX_NUM && obx.n[0] > 1)
		r_v--;			/* 0: 0xffff, no such number */
	return 1;
}

static uint8_t d_field(void)		/* the record's field in obx, as fdx */
{
	for (cur_f = 0; cur_f < obx.nf; cur_f++) {
		sel_field();
		if (ft != T_ACT && fdx.id == d_fid)
			return 1;
	}
	return 0;
}

/*
 * om_val (d_len bytes) is a value fdx may hold: what set takes. It is
 * printed into om_txt (write_char's capture, om_cap) and parsed back, so a
 * load checks a value as set does; parse_value()'s own err is dropped too.
 */
__xdata uint8_t om_cap, om_txt[OM_TXT];
static __xdata uint8_t v_err;

void om_capture(char c) __banked
{
	if (om_cap < OM_TXT)
		om_txt[om_cap++ - 1] = c;
}

static uint8_t val_ok(void)
{
	if (ft == T_STR || ft == T_SECRET) {
		if (d_len > fdx.arg)
			return 0;
	} else if (d_len != fsize()) {
		return 0;
	}
	v_err = err_status;
	om_cap = 1;
	c_def = 1;			/* a Secret as itself */
	vp = om_val;
	emit_value();
	om_txt[om_cap - 1] = 0;		/* om_cap: the next byte's index + 1 */
	om_cap = 0xff;			/* parse_value()'s err: nowhere */
	p = om_txt;
	k = parse_value();
	om_cap = 0;
	c_def = 0;
	err_status = v_err;
	return k;
}

/* load: rd() at the records; the candidate from the defaults (def_step()
 * first), the records (load_step()), then om_pend (pend_step()) */
static __xdata uint8_t l_def, l_rec;
static __xdata uint16_t sv_pend, sv_skip;	/* get -s: what its load mustn't change */

static void load_begin(void)
{
	d_base = CAND;
	def_start();
	l_def = 1;
	l_rec = 1;
	pend_off = 1;
	bs.skipped = 0;
	d_nf = 0;
	d_lo = 0xff;
#if OM_USE_KEY
	j_copies = 2;
#endif
}

/* the record's instance in the candidate (a keyed entry made if need be): 0
 * if this build or board has none */
static uint8_t d_inst(void)
{
	if (!d_obj())
		return 0;
#if OM_USE_KEY
	if (obx.k[0] == IX_KEY) {
		num[0] = r_v;
		num[1] = r_v >> 8;
		num[2] = num[3] = 0;
		cp = (__code const uint8_t *)&om_lim[obx.a[0]];
		if (!in_lim())
			return 0;
		first_inst();
		if (!cur_ok && !key_new())
			return 0;
	}
#endif
	first_inst();
	return cur_ok;
}

/* the next records into the candidate, LOAD_N units a step: an instance or a
 * field 1 (a field is printed and parsed: val_ok()), a keyed entry LOAD_KU
 * (three passes over the slots): 0 when they are all in. The budget is per
 * record, so more objects don't make a step longer. A record this build
 * can't take is skipped and counted: an unknown object, field or index (its
 * fields with it), a value outside the range */
#define LOAD_N	8
#define LOAD_KU	7
static __xdata uint8_t l_u;

static uint8_t load_step(void)
{
	l_u = 0;
	while (1) {
		if (l_u >= LOAD_N)
			return 1;
		v8 = dec_next();
		if (v8 == D_END)
			return 0;
		l_u++;
		if (v8 == D_INST) {
#if OM_USE_KEY
			if (d_obj() && obx.k[0] == IX_KEY && !d_ix) {
				/* a keyed object: its entries follow. The candidate is
				 * at the defaults: only the default entries to clear */
				for (cur_e = 0; cur_e < obx.ndef; cur_e++)
					memcpyc(inst(CAND), obx.def, obx.size);
				d_ok = 0;
				continue;
			}
#endif
			d_ok = d_inst();
			if (!d_ok)
				bs.skipped++;
#if OM_USE_KEY
			if (obx.k[0] == IX_KEY)
				l_u += LOAD_KU - 1;
#endif
			continue;
		}
		if (!d_ok)
			continue;
		if (!d_field() || !om_when() || !val_ok()) {
			bs.skipped++;		/* it keeps its default */
			continue;
		}
		memcpy(inst(CAND) + fdx.off, om_val, fsize());
	}
}

static void rd_sector(uint8_t dflt)	/* the saved or the default sector: rd_open() it */
{
	rd_base = dflt ? DEFAULT_CONFIG_START : CONFIG_START;
	rd_open();
}

/* the verb's sector: 1 if its header is good (the job checks the CRC);
 * else err (the verb's word) */
static uint8_t sector_ok(uint8_t dflt)
{
	c_pw = 0;
	d_nf = 0;
	rd_base = dflt ? DEFAULT_CONFIG_START : CONFIG_START;
	rd_head();
	l_phase = 1;			/* the job checks the CRC first */
	if (b_fmt == E_CFGFORMAT_OK)
		return 1;
	fail(b_fmt == E_CFGFORMAT_BLANK ? "blank" : "bad");
	return 0;
}

static __code const char w_load[] = "saved\0default\0";	/* load's words: om_tab() completes them */

static void load_cmd(void)
{
	word(1);
	if (cmd_words_len != 2 || !(seg_is(w_load) || seg_is(w_load + 6))) {
		err_line("err load: saved or default\n");
		return;
	}
	if (in_window())
		return;
	if (!sector_ok(cmd_buffer[cmd_words_b[1]] == 'd'))
		return;
	l_saved = cmd_buffer[cmd_words_b[1]] == 's';
	job_start(J_LOAD);		/* runs to its end, its reply lost or not */
}

static void save_cmd(void)
{
	if (in_window())
		return;
	if (om_pend) {
		fail("apply or discard first");
		return;
	}
	fl_req = FL_SAVE;
	if (!flash_job_start()) {
		fail("busy");
		return;
	}
	e_o = 0;
	e_st = 0;
	b_len = 0;
	b_crc = 0;
	bs.save = E_CFGSAVE_ERASING;
	job_start(J_SAVE);
}

/*
 * The flash job's FL_PROG step: the next records into flash_buf for fl_addr,
 * their length; the last is the header, at CONFIG_START. fl_len is the
 * length and 1 more until then (the job subtracts what it writes).
 */
uint16_t om_save_chunk(void) __banked
{
	if (bs.save == E_CFGSAVE_ERASING)
		fl_addr = CONFIG_START + OM_HDR;	/* the records first; the header last */
	bs.save = E_CFGSAVE_WRITING;
	enc_chunk();
	fl_len = e_n + 1;
	if (e_n)
		return e_n;
	fl_addr = CONFIG_START;		/* the records are written: the header */
	w_len = b_len;
	w_crc = b_crc;
	for (i = 0; i < 4; i++)
		put(om_magic[i]);
	put16(w_len);
	put16(w_crc);
	fl_len = OM_HDR;
	return OM_HDR;
}

/* its FL_VERIFY steps: the sector's CRC read back, CRC_N B a step: 1 while
 * there is more */
uint8_t om_save_end(void) __banked
{
	if (bs.save == E_CFGSAVE_WRITING) {
		bs.save = E_CFGSAVE_FAILED;	/* until the CRC says otherwise */
		rd_base = CONFIG_START;
		rd_head();
		if (b_fmt == E_CFGFORMAT_OK && b_len == w_len && b_crc == w_crc)
			return 1;
	} else if (crc_step()) {
		return 1;
	} else if (b_fmt == E_CFGFORMAT_OK) {
		bs.save = E_CFGSAVE_DONE;
		bs.saved = b_len + OM_HDR;
		bs.boot = E_CFGFORMAT_OK;
		om_dirty = 0;
	}
	return 0;
}

/* boot: the saved config (or the default) into the candidate, all at once;
 * then om_boot_apply(). bs.boot says what the saved sector held, on a
 * recovery boot (the default straight away) too. */
uint8_t om_boot_load(uint8_t dflt) __banked
{
	rd_sector(0);
	bs.boot = b_fmt;
	bs.saved = 0;
	if (dflt)
		rd_sector(1);
	if (b_fmt != E_CFGFORMAT_OK)
		return b_fmt;
	load_begin();
	while (def_step());
	while (load_step());
	pend_start();
	while (pend_step());
	l_saved = 0;			/* the default isn't what is saved */
	if (!dflt) {
		bs.saved = b_len + OM_HDR;
		l_saved = 1;
	}
	return E_CFGFORMAT_OK;
}

/* the cli's wait for the save, the load's steps, get -s: 1 while there are more */
static uint8_t bin_line(void)
{
	if (j_mode != J_SAVE && l_phase) {	/* the CRC first: a bad one takes nothing */
		if (crc_step())
			return 1;
		l_phase = 0;
		if (b_fmt != E_CFGFORMAT_OK) {
			c_pw = 0;
			fail("bad");
			j_hit = 3;
			return 0;
		}
		if (j_mode == J_SAVED)		/* the candidate aside, into the rollback copy */
			x_add(X_ROLL_CAND);
		if (j_mode != J_SAVE) {
			sv_pend = om_pend;
			sv_skip = bs.skipped;
			load_begin();
		}
		return 1;
	}
	if (j_mode != J_SAVE) {		/* load; get -s: the sector into the candidate (it aside first) */
		if (x_step())
			return 1;
		if (l_def) {
			l_def = def_step();
			return 1;
		}
		if (l_rec) {
			l_rec = load_step();
			if (!l_rec && j_mode == J_LOAD)
				pend_start();
			return 1;
		}
		if (j_mode == J_LOAD)
			return pend_step();
		pend_off = 0;			/* get -s: get -c -d of it */
		om_pend = sv_pend;
		bs.skipped = sv_skip;
		c_def = 1;
		c_cand = 1;
		j_sv = 1;
		cmd_words_b[2] = cmd_words_b[1] + 2;	/* every path, as get -d */
		cmd_words_len = 3;
		c_pw = 2;
		r_first = 0xff;
		j_hit = 0;
		j_mode = J_GET;
		j_new = 1;
#if OM_USE_KEY
		j_copies = 2 | K_SORT;
#endif
		return 1;
	}
	if (fl_kind == FL_SAVE)
		return 1;
	if (bs.save != E_CFGSAVE_DONE) {
		c_pw = 0;
		fail("failed");
		j_hit = 3;
	}
	return 0;
}

/* the verbs, in om_cmd()'s order: om_tab() completes them too */
static __code const char om_verbs[] =
	"get\0set\0del\0do\0diff\0apply\0confirm\0discard\0save\0load\0";
#define V_DO	3
#define V_PATH	4	/* get to diff: a path */
#define V_LOAD	9

static uint8_t verb_of(void)		/* the verb at p: its index, or 0xff */
{
	cp = (__code const uint8_t *)om_verbs;
	for (k = 0; *cp; k++) {
		if (seg_is((__code const char *)cp))
			return k;
		skip1();
	}
	return 0xff;
}

uint8_t om_cmd(void) __banked
{
	word(0);
	k = verb_of();
	if (k == 0xff)
		return 0;
	if (om_busy()) {			/* the walk shares the engine's cursor */
		err_line("err: applying\n");
		return 1;
	}
	c_inst = 0;
	c_def = 0;
	switch (k) {
	case 0:
		om_get();
		break;
	case 1: case 2:
		om_set(k - 1);
		break;
	case 3:
		om_do();
		break;
	case 4:
		job_start(J_DIFF);
		break;
	case 5:
		apply_cmd();
		break;
	case 6:
		if (om_apply_st != E_CFGAPPLY_CONFIRM) {
			err_line("err confirm: no window\n");
			break;
		}
		om_confirmed = 1;
		om_job_wake();
		ok();
		break;
	case 7:
		x_add(X_CAND_RUN);
		om_pend = 0;
		l_saved = 0;
		job_start(J_COPY);
		break;
	case 8:
		save_cmd();
		break;
	default:
		load_cmd();
	}
	return 1;
}

#ifndef SCHEDTEST	/* schedtest has no line editor (cmd_editor.c): no completion */
/*
 * Tab completion (DESIGN.md 13): con_line's last word, at the cursor, against
 * the verbs, the tables' paths and load's words (not a value after set: the
 * bytes). The line goes into cmd_buffer (cli is idle: no job) and the
 * word's segments before its last '.' are matched as a command's path; each
 * candidate is printed into om_txt (the capture) and t_best keeps their
 * common prefix, or the list prints it (an object name's segment as often as
 * objects have it: "stp. stp."). A step takes one object the path names
 * (from t_o) and T_STEP of its candidates: it walks to them from the start
 * again (the engine keeps nothing between steps) and builds only its own.
 */
#define T_STEP	4			/* candidates a step: the walk is again each step */
static __xdata uint8_t t_list, t_n, t_v, t_i, t_from, t_more, t_o, t_adv;
static __xdata uint8_t * __xdata t_pre;	/* the word's part a candidate starts with */
static __xdata uint8_t t_best[OM_TXT];	/* the candidates' common part */

static uint8_t t_skip(void)		/* a candidate, not this step's */
{
	if ((uint8_t)(t_i++ - t_from) < T_STEP)
		return 0;
	if (t_i > t_from)
		t_more = 1;
	return 1;
}

static void t_same(void)		/* p past its common prefix with om_txt, k its length */
{
	for (k = 0; ch() && ch() == om_txt[k]; k++)
		take();
}

static void t_cand(void)		/* om_txt's candidate, if it starts as the word does */
{
	write_char(0);
	om_cap = 0xff;
	p = t_pre;
	t_same();
	if (ch())
		return;
	if (t_list) {
		om_cap = 0;
		write_char(' ');
		print_string_x((__xdata char *)om_txt);
		om_cap = 0xff;
	} else if (!t_n) {
		memcpy(t_best, om_txt, OM_TXT);
	} else {
		p = t_best;
		t_same();
		*p = 0;
	}
	t_n++;
}

static void t_seg(uint8_t end)		/* cp's segment, then '.' if one follows, else end */
{
	if (t_skip())
		return;
	om_cap = 1;
	while (*cp && *cp != '.')
		write_char(*cp++);
	write_char(*cp == '.' ? '.' : end);
	t_cand();
}

static void t_words(void)		/* cp's words */
{
	while (*cp) {
		t_seg(' ');
		skip1();
	}
}

static void t_num(void)			/* v16 and '.' */
{
	if (t_skip())
		return;
	om_cap = 1;
	itoa_short(v16);
	write_char('.');
	t_cand();
}

static void t_ix(void)			/* level lv's indices */
{
	lvl_load();
#if OM_USE_WORD
	if (lk == IX_WORD) {
		words(la);
		while (*cp) {
			t_seg('.');
			skip1();
		}
		return;
	}
#endif
	for (w = 1; w <= ln; w++) {
		v16 = w;
#if OM_USE_KEY
		if (lk == IX_KEY) {
			ks = w - 1;
			v16 = key_at(CAND);
			if (!v16)
				continue;
		}
#endif
		if (lk != IX_PORT || log_of(w) != 0xff)
			t_num();
	}
}

uint8_t om_tab(uint8_t arg) __banked
{
	if (arg != OM_TAB_MORE) {
		t_list = arg - OM_TAB_NEW;
		t_from = 0;
		t_o = 0;
		t_n = 0;
		t_best[0] = 0;
		if (t_list)
			write_char('\n');
	}
	if (cursor != cmd_line_len || om_busy() || cli_job)
		goto bel;
	con_line[cmd_line_len] = 0;
	memcpy(cmd_buffer, con_line, CMD_BUF_SIZE);
	cmd_tokenize();
	if (err_status)
		goto bel;
	r_path = t_pre = cmd_buffer;		/* the last word; its part after its last '.' */
	for (p = cmd_buffer; ch(); ) {
		v8 = take();
		if (v8 == ' ')
			r_path = t_pre = p;
		else if (v8 == '.')
			t_pre = p;
	}
	if (*r_path)				/* none yet after a space */
		cmd_words_len--;
	t_i = 0;
	t_more = 0;
	t_adv = 0;
	om_cap = 0xff;				/* ix_parse()'s err: nowhere */
	if (!cmd_words_len) {
		cp = (__code const uint8_t *)om_verbs;
		goto words;
	}
	word(0);
	t_v = verb_of();
	if (t_v == V_LOAD && cmd_words_len == 1) {
		cp = (__code const uint8_t *)w_load;
		goto words;
	}
	if (t_v > V_PATH || (t_v && cmd_words_len > 1))
		goto end;			/* a path: get's words, the others' first */
	if (t_pre[-1] == '.') {			/* the path: the word to its last '.' */
		t_pre[-1] = 0;
		p = r_path;
	} else {				/* none: an empty path */
		if (!t_o) {
			cp = (__code const uint8_t *)s_state;
			t_seg('.');
		}
		p = &zero;
	}
	path_at();
	for (r_o = t_o; r_o < OM_N_OBJ; r_o++) {	/* the next object the path names: one a step */
		sel_obj(r_o);
		if (match() != 1)
			continue;
		t_adv = 1;
		if (nc() == '.')
			np++;
		if (nc()) {			/* the name's next segment */
			if (nc() == '#') {
				t_ix();
			} else {
				cp = (__code const uint8_t *)np;
				t_seg('.');
			}
			break;
		}
		for (cur_f = 0; cur_f < obx.nf; cur_f++) {
			sel_field();
			if ((ft == T_ACT) != (t_v == V_DO))
				continue;
			if (!fmatch(0) || !*cp)		/* under the path: cp its rest */
				continue;
			if (*cp == '.')
				cp++;
			t_seg(' ');
		}
		break;
	}
	goto end;
words:
	t_words();
end:
	om_cap = 0;
	if (t_more) {
		t_from += T_STEP;
		return OM_TAB_MORE;
	}
	if (t_adv) {				/* the object after it, next step */
		t_o = r_o + 1;
		t_from = 0;
		return OM_TAB_MORE;
	}
	if (t_list) {
		print_cmd_prompt();
		print_string_x((__xdata char *)con_line);
		return 1;
	}
	/*
	 * The completion goes into the RX ring in front of what came after the
	 * Tab, and the editor types it in. The reader's con_rx_tail moves back a
	 * byte at a time, and never to within 3 of the ISR's head: the ISR never
	 * writes the byte before the tail (CON_ISR_RX's full).
	 */
	if (t_n) {
		memcpy(om_txt, t_best, OM_TXT);
		p = t_pre;
		t_same();			/* k: the word's part, t_best's start */
		for (w = k; t_best[w]; w++);
		if (w != k) {
			while (w != k && (uint8_t)(con_rx_head - con_rx_tail) < CON_RX_SIZE - 3)
				con_rx_buf[--con_rx_tail] = t_best[--w];
			return 0;
		}
	}
bel:
	om_cap = 0;
	write_char('\a');
	return 1;
}
#endif

void om_init(void) __banked
{
	d_base = RUN;
	def_start();
	while (def_step());
	x_add(X_CAND_RUN);
	x_add(X_ROLL_RUN);
	while (x_step());
	om_pend = 0;
	om_apply_st = E_CFGAPPLY_IDLE;
	om_dirty = 0;
}

/* after the boot config's set lines: check and apply them, at once */
void om_boot_apply(void) __banked
{
	if (!check_all()) {
		om_apply_st = E_CFGAPPLY_ROLLED_BACK;	/* nothing applied: running is the defaults, */
		goto not_applied;			/* the candidate what was refused (diff) */
	}
	/* A soft reset keeps the chip and the PHYs as they were: every instance
	 * but keyed entries (vlan_setup clears those) is applied whole, at its
	 * default too, so the defaults are what the chip has after a boot */
	om_full = 1;
	apply_begin();
	while ((v8 = om_apply_next()) != OM_A_DONE) {
		if (v8 == OM_A_FAIL) {
			om_full = 0;
			if (om_apply_st == E_CFGAPPLY_UNDO)
				break;
			om_undo();
		}
	}
	om_full = 0;
	om_settle(v8);
	while (x_step());
	if (om_apply_st == E_CFGAPPLY_DONE) {
		om_apply_st = E_CFGAPPLY_IDLE;
		return;
	}
not_applied:				/* state.cfg.apply keeps rolled_back or failed */
	print_string("Boot config not applied\n");
}

/* state.cfg (schema/state_cfg.py) */
uint8_t om_get_cfg(uint8_t idx) __banked
{
	om_st.cfg.pending = om_pend;
	om_st.cfg.dirty = om_dirty;
	om_st.cfg.apply = om_apply_st;
	om_st.cfg.confirm_s = om_confirm_s;
	memcpy(&om_st.cfg.format, &bs, sizeof(bs));	/* the same order (state_cfg.py) */
	return OM_OK;
}

/* the prompt: "[confirm N] " in a window, "*" while the candidate differs */
void om_prompt(void) __banked
{
	if (om_apply_st == E_CFGAPPLY_CONFIRM) {
		print_string("[confirm ");
		itoa_short(om_confirm_s);
		print_string("] ");
	}
	if (om_pend)
		write_char('*');
}
