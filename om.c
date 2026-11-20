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
#define ROLL	((__xdata uint8_t *)&cfg_roll)	/* an apply's, until settled; else scratch */

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
static __xdata uint8_t cv1, r_v1;	/* level 1: number from 0, word; as given */
#endif
static __xdata uint8_t lv, lk, ln, la;	/* a level, its kind, count, arg */
static __xdata uint8_t cur_e, cur_l, cur_f, cur_ok;	/* element, logical port, field, valid */
static __xdata uint8_t * __xdata vp;	/* the current value */
static __code const char * __xdata np;	/* the object's name, being walked */
static __xdata uint8_t e_star;		/* emit_obj: '*' for the indices */
/* the job */
#define J_GET	1
#define J_DIFF	2
#define J_SAVED	3	/* get -s: a record a step */
#define J_SAVE	4	/* the cli waits for the flash job */
#define J_LOAD	5	/* a load, 256 B of records a step */
#define J_APPLY	6	/* J_SAVE on: the job runs to its end (CLI_JOB_OMAPPLY) */
static __xdata uint8_t j_mode, j_o, j_new, j_quiet, j_in;
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

/* whole copies: a call each is smaller code than a memcpy */
static void cand_from_run(void)
{
	memcpy(&cfg_cand, &cfg_run, sizeof(cfg_cand));
}

static void roll_from_run(void)
{
	memcpy(&cfg_roll, &cfg_run, sizeof(cfg_roll));
}


static uint8_t cand_differs(void)	/* the candidate isn't running */
{
	if (memcmp(&cfg_cand, &cfg_run, sizeof(cfg_run)))
		return 1;
	return 0;
}

static void sel_obj(uint8_t n)
{
	memcpyc((__xdata uint8_t *)&obx, (__code const uint8_t *)&om_obj[n], sizeof(obx));
}

static void sel_field(void)		/* obx's field cur_f */
{
	memcpyc((__xdata uint8_t *)&fdx, (__code const uint8_t *)&obx.f[cur_f], sizeof(fdx));
	ft = fdx.tw & T_TYPE;
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
			v16 = cv1;
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
	p = cmd_buffer + cmd_words_b[c_pw];
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
	write_char('.');
	print_string(fdx.name);
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

/* level 0's next index at or after cv that the path allows: 0 if none */
static uint8_t step0(void)
{
	lk = obx.k[0];
#if OM_USE_KEY
	if (lk == IX_KEY)
		return key_step();
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
static uint8_t step1(void)		/* level 1's, at or after cv1 */
{
	if (r_fix & 2) {
		if (cv1 > r_v1)
			return 0;
		cv1 = r_v1;
	}
	return cv1 < obx.n[1];
}
#endif

static void elem(void)			/* the cursor's element */
{
	cur_l = ce;
	cur_e = ce;
#if OM_USE_IX2
	cur_e = ce * obx.n[1] + cv1;
#endif
}

static void first_inst(void)		/* cur_ok: there is one */
{
	cv = 0;
	cur_ok = step0();
#if OM_USE_IX2
	cv1 = 0;
	step1();
#endif
	elem();
}

static void next_inst(void)
{
#if OM_USE_IX2
	cv1++;
	if (step1()) {
		cur_e++;
		return;
	}
	cv1 = 0;
	step1();
#endif
	cv++;
	cur_ok = step0();
	elem();
}

static uint8_t call(uint8_t fn)
{
	return om_fns[fn](cur_e);
}

/* ---- paths ------------------------------------------------------------ */

static void path_start(void)		/* the path word c_pw: its tree */
{
	p = cmd_buffer + cmd_words_b[c_pw];
	r_st = OM_CFG;
	if (seg_is("state")) {
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
	if (!parse_dec() || num[2] || num[3] || !(ch() == '.' || at_end()))
		goto bad;
	v16 = num[0] | (uint16_t)num[1] << 8;
	if (lk == IX_PORT) {
		if (num[1] || log_of(num[0]) == 0xff)
			goto bad;
	}
#if OM_USE_NUM
	else if (lk == IX_NUM) {
		if (!v16 || v16 > ln)
			goto bad;
		v16--;
	}
#endif
#if OM_USE_KEY
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
#if OM_USE_KEY
	} else if (lk == IX_KEY) {
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
	if ((obx.flags & OM_STATE) && obx.get != 0xff)
		call(obx.get);
	cur_f = 0;
	j_in = 3;
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

/* the next field to print, or 0 at the end of the instance */
static uint8_t next_field(void)
{
	for (; cur_f < obx.nf; cur_f++) {
		sel_field();
		if (ft == T_ACT || !om_when())
			continue;
		if (j_mode == J_GET) {
			if (fmatch(0) && (!c_def || f_diff()))
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
				if (c_def)
					j_hit = 2;	/* -d: a path may have nothing off its default */
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
			if (r_fix & 1) {
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
__xdata uint8_t om_apply_st, om_confirm_s, om_confirmed, om_dirty, om_cut;
static __xdata uint8_t l_saved;		/* the candidate is the saved config, as loaded */
static uint8_t check_all(void);
static __xdata uint8_t a_o, a_e, a_n, a_fo, a_fe, a_timeout;

static void cv_of_e(void)		/* cur_e's indices, for its path in a message */
{
	ce = cur_e;
#if OM_USE_IX2
	cv1 = cur_e % obx.n[1];
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

static void apply_begin(void)		/* the candidate to running; the walk from the top */
{
	if (cand_differs())
		om_dirty = 1;
	if (l_saved)			/* what load saved put there */
		om_dirty = 0;
	roll_from_run();
	memcpy(&cfg_run, &cfg_cand, sizeof(cfg_run));
	om_apply_st = E_CFGAPPLY_RUNNING;
	walk_start();
}

/* from (a_o, a_e): the next config element where running differs from
 * a_other (the rollback copy, or the candidate), as obx and cur_e; 0 at the end */
static __xdata uint8_t * __xdata a_other;

static uint8_t walk_next(void)
{
	for (; a_o < OM_N_OBJ; a_o++, a_e = 0) {
		sel_obj(a_o);
		if (obx.flags & OM_STATE)
			continue;
		a_n = obx.n[0] * obx.n[1];
		while (a_e < a_n) {
			cur_e = a_e++;
			if (memcmp(inst(RUN), inst(a_other), obx.size))
				return 1;
		}
	}
	return 0;
}

/* the next instance that changed: apply it. OM_A_MORE, OM_A_DONE, OM_A_FAIL */
uint8_t om_apply_next(void) __banked
{
	a_other = ROLL;
	if (!walk_next())
		return OM_A_DONE;
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
	cand_from_run();
	memcpy(&cfg_run, &cfg_roll, sizeof(cfg_run));
	memcpy(&cfg_roll, &cfg_cand, sizeof(cfg_roll));
	walk_start();
}

/* the job's end: r the last walk's result */
void om_settle(uint8_t r) __banked
{
	if (om_apply_st == E_CFGAPPLY_UNDO) {
		om_apply_st = r == OM_A_DONE ? E_CFGAPPLY_ROLLED_BACK : E_CFGAPPLY_FAILED;
		if (a_timeout) {		/* nobody waits for this one */
			cand_from_run();
			print_string("event state.cfg.apply rolled_back\n");
		}
	} else {
		om_apply_st = E_CFGAPPLY_DONE;
	}
	om_confirm_s = 0;
	roll_from_run();
}

static uint8_t om_busy(void)		/* the cfg job is walking */
{
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
		p = cmd_buffer + cmd_words_b[1];
		if (!parse_dec() || num[2] | num[3] || !at_end() ||
		    !(v16 = num[0] | (uint16_t)num[1] << 8) || v16 > 3600) {
			fail("1-3600 seconds");
			return;
		}
	}
	if (in_window())
		return;
	if (!check_all())
		return;
	om_confirm_s = v16;
	if (!om_job_start()) {
		err_line("err apply: busy\n");
		return;
	}
	apply_begin();
	om_confirmed = 0;
	job_start(J_APPLY);		/* the cli waits for the walk, then says how it went */
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
		next_instance();
		if (j_mode != J_APPLY)
			return 1;		/* an instance a step: a keyed one is a pass over the slots */
	}
done:
	if (!j_quiet && j_hit != 3)
		ok();
	return 0;
}

/* ---- the verbs ---------------------------------------------------------- */

static uint8_t sector_ok(uint8_t dflt);
static void defaults(__xdata uint8_t *base);

static void om_get(void)
{
	c_pw = 1;
	c_cand = 0;
	if (cmd_words_len > 1 && cmd_buffer[cmd_words_b[1]] == '-') {
		v8 = cmd_buffer[cmd_words_b[1] + 1];
		c_pw = 2;
		if (v8 == 's') {		/* the saved config, all of it */
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
			defaults(ROLL);		/* f_diff()'s */
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
static uint8_t key_new(void)
{
	kb = 0xff;
	for (ks = 0; ks < obx.n[0]; ks++) {
		kv = key_at(RUN);
		if (kv == r_v) {
			kb = ks;
			break;
		}
		if (!kv && !key_at(CAND) && kb == 0xff)
			kb = ks;
	}
	if (kb == 0xff)
		return 0;
	cur_e = kb;
	vp = inst(CAND);
	memcpyc(vp, obx.def, obx.size);
	vp[0] = r_v;
	vp[1] = r_v >> 8;
	return 1;
}
#endif

#if OM_USE_KEY
static void key_clear(void)		/* the candidate's entries of obx gone */
{
	for (cur_e = 0; cur_e < obx.n[0]; cur_e++)
		memcpyc(inst(CAND), obx.def, obx.size);
}

/* del of a keyed object: every entry gone, then its default entries again */
static uint8_t key_reset(void)
{
	key_clear();
	cp = obx.def;
	for (k = 0; k < obx.ndef; k++) {
		cp += obx.size;
		r_v = cp[0] | (uint16_t)cp[1] << 8;
		if (!key_new()) {
			fail("no free entry");
			return 0;
		}
		memcpyc(vp, cp, obx.size);
	}
	return 1;
}
#endif

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
#if OM_USE_KEY
	if (cur_f == 0xff && obx.k[0] == IX_KEY && !(r_fix & 1)) {
		if (key_reset())
			ok();
		return;
	}
#endif
	first_inst();
#if OM_USE_KEY
	if (!cur_ok && !del && (r_fix & 1)) {
		if (!key_new()) {
			fail("no free entry");
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
	v8 = 0;
	for (; cur_ok; next_inst()) {
		vp = inst(CAND);
		if (cur_f == 0xff) {
			memcpyc(vp, def_ptr(), obx.size);
		} else {
			if (!om_when())
				continue;
			vp += fdx.off;
			if (del)
				memcpyc(vp, def_ptr() + fdx.off, k);
			else
				memcpy(vp, om_val, k);
		}
		v8 = 1;
	}
	if (!v8) {
		fail("not on this port");
		return;
	}
	ok();
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

/* every instance's check, on the candidate */
static uint8_t check_all(void)
{
	c_inst = 1;
#if OM_USE_KEY
	j_copies = 3;		/* entries deleted in the candidate too: a check may refuse it */
#endif
	r_fix = 0;
	for (r_o = 0; r_o < OM_N_OBJ; r_o++) {
		sel_obj(r_o);
		if ((obx.flags & OM_STATE) || obx.check == 0xff)
			continue;
		for (first_inst(); cur_ok; next_inst()) {
			if (call(obx.check) != OM_OK)
				return 0;
		}
	}
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
static __xdata uint16_t e_n, l_crc, l_stop;
static __xdata uint8_t e_b, e_nf, e_legacy, l_phase;
/* the save's cursor: the next object, and (e_st) the instance to go on at */
static __xdata uint8_t e_o, e_st, e_ce;
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

/* fdx's value in running is off its default (vp at it); not for actions.
 * The defaults are in the rollback copy (save_cmd(), om_get()) */
static uint8_t f_diff(void)
{
	vp = inst(RUN) + fdx.off;
	if (ft == T_ACT || !om_when())
		return 0;
	if (memcmp(vp, inst(ROLL) + fdx.off, fsize()))
		return 1;
	return 0;
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

/* every config element of a copy at its default, and the default entries
 * of keyed objects; with base ROLL (f_diff()'s), every slot at the element's */
static void defaults(__xdata uint8_t *base)
{
	for (r_o = 0; r_o < OM_N_OBJ; r_o++) {
		sel_obj(r_o);
		if (obx.flags & OM_STATE)
			continue;
		r_first = obx.n[0] * obx.n[1];
		for (cur_e = 0; cur_e < r_first; cur_e++)
			memcpyc(inst(base), def_ptr(), obx.size);
#if OM_USE_KEY
		cp = obx.def;
		for (cur_e = 0; cur_e < obx.ndef && base != ROLL; cur_e++) {
			cp += obx.size;
			memcpyc(inst(base), cp, obx.size);
		}
#endif
	}
}

/*
 * The next records into flash_buf (e_n bytes), whole instances, none
 * started past E_ROOM, from the cursor. The rollback copy holds the
 * defaults meanwhile (save_cmd()): it is scratch outside an apply's walk
 * and window, and save refuses in one; an instance at its defaults is then
 * one memcmp.
 */
static void enc_chunk(void)
{
	e_n = 0;
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
			cv1 = e_cv1;
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
			if (!memcmp(inst(RUN), inst(ROLL), obx.size) && obx.k[0] != IX_KEY)
				continue;
			e_put = 0;
			inst_fields();
			if (!e_nf && obx.k[0] != IX_KEY)
				continue;
			if (e_n >= E_ROOM) {	/* the next step's */
				e_cv = cv;
				e_ce = ce;
#if OM_USE_IX2
				e_cv1 = cv1;
#endif
				goto full;
			}
			v16 = cv;
			if (obx.k[0] == IX_NUM && obx.n[0] > 1)
				v16++;
#if OM_USE_IX2
			if (obx.n[1] > 1)
				v16 = v16 << 8 | cv1;
#endif
			put(obx.id);
			put16(v16);
			put(e_nf);
			e_put = 1;
			inst_fields();
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
	b_fmt = E_CFGFORMAT_LEGACY;		/* old command text: a letter */
	if (v8 >= 'A')
		return;
	b_fmt = E_CFGFORMAT_BAD;
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

/* the CRC over the next 256 B of records: 1 while there are more; then b_fmt
 * ok (rd() at the first record again) or bad */
static uint8_t crc_step(void)
{
	crc_value = l_crc;
	i = 0;
	do {				/* 256 */
		if (!rd_left)
			break;
		rd();
	} while (++i);
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
 * this build has no such object */
static uint8_t d_obj(void)
{
	for (r_o = 0; r_o < OM_N_OBJ; r_o++) {
		sel_obj(r_o);
		if (!(obx.flags & OM_STATE) && obx.id == d_oid)
			goto found;
	}
	return 0;
found:
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

/* load: rd() at the records; the candidate from the defaults */
static void load_begin(void)
{
	defaults(CAND);
	bs.skipped = 0;
	d_nf = 0;
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

/* the next 64 B of records into the candidate: 0 when they are all in.
 * A record this build can't take is skipped and counted: an unknown
 * object, field or index (its fields with it), a value outside the range */
static uint8_t load_step(void)
{
	l_stop = rd_left > 64 ? rd_left - 64 : 0;
	while (1) {
		if (rd_left && rd_left <= l_stop)
			return 1;
		v8 = dec_next();
		if (v8 == D_END)
			return 0;
		if (v8 == D_INST) {
#if OM_USE_KEY
			if (d_obj() && obx.k[0] == IX_KEY && !d_ix) {
				key_clear();	/* a keyed object: its entries follow */
				d_ok = 0;
				continue;
			}
#endif
			d_ok = d_inst();
			if (!d_ok)
				bs.skipped++;
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

/* get -s: the saved config's next field that a load takes, as a line (not a
 * Secret); 0 at the end */
static uint8_t saved_line(void)
{
	while ((v8 = dec_next()) != D_END) {
		if (v8 == D_INST) {
			d_ok = d_obj();
#if OM_USE_KEY
			if (d_ok && obx.k[0] == IX_KEY) {
				cv = r_v;		/* the key; 0: the object's reset */
				if (!cv)
					d_ok = 0;
				continue;
			}
#endif
			if (d_ok) {
				first_inst();
				d_ok = cur_ok;
			}
			continue;
		}
		if (d_ok && d_field() && ft != T_SECRET && om_when() && val_ok()) {
			vp = om_val;		/* what a load takes */
			emit_line();
			return 1;
		}
	}
	return 0;
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
	fail(b_fmt == E_CFGFORMAT_BLANK ? "blank" : "bad, or old command text");
	return 0;
}

static void load_cmd(void)
{
	p = cmd_buffer + cmd_words_b[1];
	if (cmd_words_len != 2 || !(seg_is("saved") || seg_is("default"))) {
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
	if (cand_differs()) {
		fail("apply or discard first");
		return;
	}
	rd_base = CONFIG_START;		/* old command text there: say what goes */
	rd_start();
	e_legacy = rd();
	fl_req = FL_SAVE;
	if (!flash_job_start()) {
		fail("busy");
		return;
	}
	defaults(ROLL);
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

/* its FL_VERIFY steps: the sector's CRC read back, 256 B a step: 1 while
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
		bs.boot = E_CFGFORMAT_OK;	/* legacy no more */
		om_dirty = 0;
	}
	return 0;
}

/* boot: the saved config (or the default) into the candidate, all at once;
 * then om_boot_apply(). bs.boot says what the saved sector held. */
uint8_t om_boot_load(uint8_t dflt) __banked
{
	rd_sector(dflt);
	if (!dflt) {
		bs.boot = b_fmt;
		bs.saved = 0;
	}
	if (b_fmt != E_CFGFORMAT_OK)
		return b_fmt;
	load_begin();
	while (load_step());
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
		if (j_mode == J_LOAD)
			load_begin();
		return 1;
	}
	if (j_mode == J_LOAD)
		return load_step();
	if (j_mode == J_SAVED)
		return saved_line();
	if (fl_kind == FL_SAVE)
		return 1;
	if (bs.save != E_CFGSAVE_DONE) {
		c_pw = 0;
		fail("failed");
		j_hit = 3;
	} else if (e_legacy >= 'A' && e_legacy != 0xff) {	/* rd_open()'s test */
		print_string("saved over old text: unmigrated commands dropped\n");
	}
	return 0;
}

void om_cmd(void) __banked
{
	if (om_busy()) {			/* the walk shares the engine's cursor */
		err_line("err: applying\n");
		return;
	}
	c_inst = 0;
	c_def = 0;
	p = cmd_buffer + cmd_words_b[0];
	if (seg_is("get")) {
		om_get();
	} else if (seg_is("set")) {
		om_set(0);
	} else if (seg_is("del")) {
		om_set(1);
	} else if (seg_is("do")) {
		om_do();
	} else if (seg_is("diff")) {
		job_start(J_DIFF);
	} else if (seg_is("apply")) {
		apply_cmd();
	} else if (seg_is("confirm")) {
		if (om_apply_st != E_CFGAPPLY_CONFIRM) {
			err_line("err confirm: no window\n");
			return;
		}
		om_confirmed = 1;
		om_job_wake();
		ok();
	} else if (seg_is("discard")) {
		cand_from_run();
		l_saved = 0;
		ok();
	} else if (seg_is("save")) {
		save_cmd();
	} else if (seg_is("load")) {
		load_cmd();
	}
}

void om_init(void) __banked
{
	defaults(RUN);
	cand_from_run();
	roll_from_run();
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
	apply_begin();
	while ((v8 = om_apply_next()) != OM_A_DONE) {
		if (v8 == OM_A_FAIL) {
			if (om_apply_st == E_CFGAPPLY_UNDO)
				break;
			om_undo();
		}
	}
	om_settle(v8);
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
	om_st.cfg.pending = 0;
	walk_start();			/* the instances where the candidate differs */
	a_other = CAND;
	while (walk_next())
		om_st.cfg.pending++;
	sel_obj(j_o);			/* the get job's object: state.cfg */
	cur_e = 0;
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
	if (cand_differs())
		write_char('*');
}
