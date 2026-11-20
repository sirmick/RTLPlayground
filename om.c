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
#include "rtl837x_common.h"
#include "machine.h"
#include "cmd_parser.h"
#include "om_gen.h"

extern __code const struct machine machine;
extern __xdata uint8_t cmd_words_len;
extern __xdata uint8_t cmd_words_b[];
extern __code const char * __code const om_enum[];
extern __code const struct om_lim om_lim[];
extern __code const char * __code const om_msg[];

#define RUN	((__xdata uint8_t *)&cfg_run)
#define CAND	((__xdata uint8_t *)&cfg_cand)
#define ROLL	((__xdata uint8_t *)&cfg_roll)

/* the command */
static __xdata uint8_t c_pw;		/* the path's word; 0: apply's checks */
static __xdata uint8_t c_cand;		/* get -c: from the candidate */
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
#define J_APPLY	3
static __xdata uint8_t j_mode, j_o, j_new, j_quiet, j_in;
static __xdata uint8_t j_hit;		/* get, this path: 1 a field matched, 2 done, 3 failed */
/* numbers */
static __xdata uint8_t num[8], num_len, dig[21], k, i, v8, w, save_n;
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
	switch (fdx.tw >> T_WHEN) {
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
	if (c_pw)
		print_path_word();
	else			/* apply's checks: the instance */
		emit_obj();
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
	for (save_n = 1; save_n <= 9; save_n++) {
		v8 = log_of(save_n);
		if (v8 == 0xff || !(v16 & (1 << v8)))
			continue;
		/* a run of members: n, or n-m */
		i = save_n;
		while (save_n < 9 && (v8 = log_of(save_n + 1)) != 0xff && (v16 & (1 << v8)))
			save_n++;
		if (k)
			write_char(',');
		itoa_short(i);
		if (save_n != i) {
			write_char('-');
			itoa_short(save_n);
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
	case T_STR:
		print_string_x((__xdata char *)vp);
		return;
	case T_PMASK:
		emit_pmask();
		return;
#if OM_USE_SECRET
	case T_SECRET:
		write_char('*');
		return;
#endif
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
	case T_BRID:
		itoa_short(vp[0] >> 4);
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
	if (obx.derive != 0xff && !*vp) {	/* empty: the derived value */
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
		for (save_n = 1; save_n <= 9; save_n++)
			if ((v8 = log_of(save_n)) != 0xff)
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
	for (save_n = k; save_n <= i; save_n++) {
		if ((v8 = log_of(save_n)) == 0xff)
			return 0;
		pm |= 1 << v8;
	}
	return 1;
}

#if OM_USE_MAC || OM_USE_BRID
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
			if (k >= fdx.arg || v8 < 0x20 || v8 > 0x7e || v8 == '"' || v8 == '\\') {
				fail("printable text without \" or \\, at most its length");
				return 0;
			}
			om_val[k] = v8;
		}
		om_val[k] = 0;
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
#if OM_USE_IPV4
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
#if OM_USE_MAC
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
#if OM_USE_BRID
	case T_BRID:
		if (!byte_dec() || num[0] > 15 || take() != '.')
			goto bad_mac;
		om_val[0] = num[0] << 4;
		om_val[1] = 0;
		w = 2;
		if (!parse_mac())
			goto bad_mac;
		break;
#endif
#if OM_USE_MAC || OM_USE_BRID
bad_mac:
		fail(ft == T_BRID ? "0-15.aa:bb:cc:dd:ee:ff" :
			fdx.arg ? "aa:bb:cc:dd:ee:ff or auto" : "aa:bb:cc:dd:ee:ff");
		return 0;
#endif
#if OM_USE_FLAGS
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
#if OM_USE_PORT
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
#if OM_USE_I16 || OM_USE_TZ
	case T_I16: case T_TZ:
		save_n = ch() == '-';
		if (ch() == '-' || ch() == '+')
			take();
		if (!parse_dec() || num[2] | num[3])
			goto bad_signed;
		v16 = num[0] | (uint16_t)num[1] << 8;
#if OM_USE_TZ
		if (ft == T_TZ) {	/* hh:mm, mm 00, 30 or 45 */
			k = num[0];
			if (num[1] || take() != ':' || !byte_dec() ||
			    !(num[0] == 0 || num[0] == 30 || num[0] == 45))
				goto bad_signed;
			v16 = k * 60 + num[0];
		}
#endif
		if (save_n) {
			if (v16 > 0x8000)
				goto bad_signed;
			v16 = 0x8000 - v16;
		} else {
			if (v16 > 0x7fff)
				goto bad_signed;
			v16 += 0x8000;
		}
		num[0] = v16;
		num[1] = v16 >> 8;
		cp = (__code const uint8_t *)&om_lim[fdx.arg];
		if (!in_lim())
			goto bad_signed;
		om_val[0] = num[0];
		om_val[1] = num[1] ^ 0x80;
		break;
bad_signed:		/* "lo..hi" */
		err_head();
		cp = (__code const uint8_t *)&om_lim[fdx.arg];
		for (w = 0; w < 2; w++) {
			om_val[0] = cp[0];
			om_val[1] = cp[1] ^ 0x80;
			vp = om_val;
			emit_value();
			if (!w)
				print_string("..");
			cp += 4;
		}
		err_end();
		return 0;
#endif
	case T_U8: case T_U16: case T_U32:
		if (!parse_dec())
			goto bad_range;
		cp = (__code const uint8_t *)&om_lim[fdx.arg];
		if (!in_lim())
			goto bad_range;
		for (i = 0; i < 4; i++)
			om_val[i] = num[i];
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
	print_lim();
	err_end();
	return 0;
}

/* ---- the cursor ---------------------------------------------------------- */

#if OM_USE_KEY
static __xdata uint8_t j_copies;	/* where entries are looked for: 1 running, 2 candidate */
static __xdata uint8_t ks, kb, kc;
static __xdata uint16_t kv, kbest;

static uint16_t key_at(__xdata uint8_t *base)	/* slot ks's key in a copy */
{
	cur_e = ks;
	vp = inst(base);
	return vp[0] | (uint16_t)vp[1] << 8;
}

/* the smallest key at or after cv that the path allows: its slot */
static uint8_t key_step(void)
{
	kbest = 0xffff;
	kb = 0xff;
	for (ks = 0; ks < obx.n[0]; ks++) {
		for (kc = 1; kc <= 2; kc++) {
			if (!(j_copies & kc))
				continue;
			kv = key_at(kc == 1 ? RUN : CAND);
			if (kv && kv >= cv && kv < kbest && (!(r_fix & 1) || kv == r_v)) {
				kbest = kv;
				kb = ks;
			}
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
	if (save_n != 0xff) {
		sel_obj(save_n);
		for (cur_f = 0; cur_f < obx.nf; cur_f++) {
			sel_field();
			if (cur_f)
				write_char(' ');
			print_string(fdx.name);
		}
	} else {
		e_star = 1;
		w = 0;
		for (r_o = 0; r_o < OM_N_OBJ; r_o++) {
			sel_obj(r_o);
			if ((obx.flags & OM_STATE) == r_st) {
				if (w)
					write_char(' ');
				emit_obj();
				w = 1;
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
	cli_job = mode == J_APPLY ? CLI_JOB_OMAPPLY : CLI_JOB_OM;
	j_new = 1;
	j_hit = 0;
	save_n = 0xff;
#if OM_USE_KEY
	if (mode != J_GET)
		j_copies = 3;
#endif
}

/* the next field to print, or 0 at the end of the instance */
static uint8_t next_field(void)
{
	for (; cur_f < obx.nf; cur_f++) {
		sel_field();
		if (ft == T_ACT || !om_when())
			continue;
		if (j_mode == J_GET) {
			if (fmatch(0))
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
				if (save_n == 0xff)
					save_n = j_o;
				if (!has_field())
					continue;
				j_hit |= 1;
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
		save_n = 0xff;
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

/* apply: the instance, if it changed; 1 if it did */
static uint8_t apply_inst(void)
{
	vp = inst(RUN);
	if (!memcmp(vp, inst(CAND), obx.size))
		return 0;
	memcpy(inst(ROLL), vp, obx.size);
	memcpy(vp, inst(CAND), obx.size);
	if (obx.apply != 0xff)
		call(obx.apply);
	return 1;
}

uint8_t om_line(void) __banked
{
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
		if (j_mode == J_APPLY) {
			w = apply_inst();
			next_instance();
			if (w)
				return 1;	/* an instance a step */
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
	}
done:
	if (!j_quiet && j_hit != 3)
		ok();
	return 0;
}

/* ---- the verbs ---------------------------------------------------------- */

static void om_get(void)
{
	c_pw = 1;
	c_cand = 0;
	if (cmd_words_len > 1 && cmd_buffer[cmd_words_b[1]] == '-' && cmd_buffer[cmd_words_b[1] + 1] == 'c') {
		c_cand = 1;
		c_pw = 2;
	}
#if OM_USE_KEY
	j_copies = c_cand ? 2 : 1;
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
		print_string("err: a path\n");
		err_status = ERR_INVALID_ARGUMENT;
		return 0;
	}
	path_start();
	save_n = 0xff;
	for (r_o = 0; r_o < OM_N_OBJ; r_o++) {
		sel_obj(r_o);
		v8 = match();
		if (v8 == 2)
			return 0;
		if (!v8)
			continue;
		if (save_n == 0xff)
			save_n = r_o;
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

static void om_set(uint8_t del)
{
	if (!target(del))
		return;
	if (cur_f != 0xff && ft == T_ACT) {
		fail(del ? "an action" : "an action: do it");
		return;
	}
	if (!del) {
		p = cmd_words_len > 2 ? cmd_buffer + cmd_words_b[2] : &zero;
		if (!parse_value())
			return;
	}
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
	c_pw = 0;
#if OM_USE_KEY
	j_copies = 2;
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

void om_cmd(void) __banked
{
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
		if (check_all())
			job_start(J_APPLY);
	} else if (seg_is("discard")) {
		memcpy(&cfg_cand, &cfg_run, sizeof(cfg_cand));
		ok();
	}
}

void om_init(void) __banked
{
	for (r_o = 0; r_o < OM_N_OBJ; r_o++) {
		sel_obj(r_o);
		if (obx.flags & OM_STATE)
			continue;
		save_n = obx.n[0] * obx.n[1];
		for (cur_e = 0; cur_e < save_n; cur_e++)
			memcpyc(inst(RUN), def_ptr(), obx.size);
#if OM_USE_KEY
		cp = obx.def;
		for (cur_e = 0; cur_e < obx.ndef; cur_e++) {
			cp += obx.size;
			memcpyc(inst(RUN), cp, obx.size);
		}
#endif
	}
	memcpy(&cfg_cand, &cfg_run, sizeof(cfg_run));
	memcpy(&cfg_roll, &cfg_run, sizeof(cfg_run));
}

/* after the boot config's set lines: check and apply them, at once */
void om_boot_apply(void) __banked
{
	if (!check_all()) {
		print_string("Boot config not applied\n");
		memcpy(&cfg_cand, &cfg_run, sizeof(cfg_cand));
		return;
	}
	job_start(J_APPLY);
	j_quiet = 1;
	cmd_finish();
	j_quiet = 0;
}
