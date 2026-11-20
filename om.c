/*
 * om.c - the object model's engine (DESIGN.md section 12): the CLI verbs over
 * the generated tables.
 *
 *   get [-c] <path>        print the fields under path ("path value" lines)
 *   set <path> <value>     change the candidate
 *   del <path>             a field (or a whole instance) back to its default
 *   diff                   candidate vs running
 *   apply                  check the candidate, then apply what changed
 *   discard                candidate = running
 *   do <path>              run an action
 *
 * Every command ends in one status line: "ok", or "err <path>: <why>".
 * Long output (get, diff) and apply run as the cli's job, a line or an
 * instance a step (cmd_parser.c cli_job_more()).
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

/* the command */
static __xdata uint8_t c_pw;		/* the path's word */
static __xdata uint8_t c_cand;		/* get -c: from the candidate */
/* a resolved path */
static __xdata uint8_t r_o;		/* object */
static __xdata uint8_t r_n;		/* front-panel number, 0xff all (none given or '*') */
static __xdata uint8_t * __xdata r_rest;	/* the rest of the path, after the index */
static __xdata uint8_t * __xdata p;	/* parse pointer */
/* the current object, field, instance */
/* copies of the current descriptors: fixed addresses are much smaller code */
static __xdata struct om_obj obx;
static __xdata struct om_field fdx;
static __xdata uint8_t cur_n, cur_l, cur_f;
static __xdata uint8_t * __xdata vp;	/* the current value */
/* the job */
#define J_GET	1
#define J_DIFF	2
#define J_APPLY	3
static __xdata uint8_t j_mode, j_o, j_new, j_quiet;
/* numbers */
static __xdata uint8_t num[8], num_len, dig[21], k, i, v8, w, save_n;
static __xdata uint8_t zero;
static __xdata uint16_t v16, pm;	/* pm: a port mask being built */
static __code const uint8_t * __xdata cp;
static __xdata uint8_t vbuf[32];

/* ---- small helpers ---------------------------------------------------- */

static void sel_obj(uint8_t n)
{
	memcpyc((__xdata uint8_t *)&obx, (__code const uint8_t *)&om_obj[n], sizeof(obx));
}

static void sel_field(void)		/* obx's field cur_f */
{
	memcpyc((__xdata uint8_t *)&fdx, (__code const uint8_t *)&obx.f[cur_f], sizeof(fdx));
}

static uint8_t at_end(void)		/* p at the end of a word */
{
	return *p == ' ' || *p == 0;
}

/* the segment at p equals s (to '.', ' ' or NUL): advance past it */
static uint8_t seg_is(__code const char *s)
{
	__xdata uint8_t *q = p;
	while (*s && *q == *s) {
		q++;
		s++;
	}
	if (*s || !(*q == '.' || *q == ' ' || *q == 0))
		return 0;
	p = *q == '.' ? q + 1 : q;
	return 1;
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
	switch (fdx.when) {
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
	switch (fdx.type) {
	case T_U16: case T_PMASK: case T_SCALED:
		return 2;
	case T_U32:
		return 4;
	case T_U64:
		return 8;
	case T_STR:
		return fdx.arg + 1;
	case T_ACT:
		return 0;
	}
	return 1;
}

/* the instance's address in a config copy (base: cfg_run/cand/roll), or in om_st */
static __xdata uint8_t *inst(__xdata uint8_t *base)
{
	if (obx.flags & OM_STATE)
		return (__xdata uint8_t *)&om_st;
	base += obx.base;
	if (obx.flags & OM_PORTOBJ)
		base += (uint16_t)cur_l * obx.size;
	return base;
}

static void print_path_word(void)	/* the command's path, as typed */
{
	p = cmd_buffer + cmd_words_b[c_pw];
	while (!at_end()) {
		write_char(*p < 0x21 || *p > 0x7e ? '?' : *p);
		p++;
	}
}

void om_fail(__code const char *why) __banked
{
	print_string("err ");
	if (c_pw) {
		print_path_word();
	} else {		/* apply's checks: the instance */
		print_string(obx.name);
		if (obx.flags & OM_PORTOBJ) {
			write_char('.');
			itoa_short(cur_n);
		}
	}
	print_string(": ");
	print_string(why);
	write_char('\n');
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
	if (*p < '0' || *p > '9')
		return 0;
	while (*p >= '0' && *p <= '9') {
		v16 = *p++ - '0';
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

/* num[0..4) against a limit, most significant byte first: <0, 0, >0 */
static int8_t cmp_lim(void)
{
	for (i = 4; i--; ) {
		if (num[i] != cp[i])
			return num[i] < cp[i] ? -1 : 1;
	}
	return 0;
}

static void print_lim(void)		/* "lo-hi" */
{
	cp = (__code const uint8_t *)&om_lim[fdx.lim];
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

static void enum_words(void)		/* cp at fd's first enum word */
{
	cp = (__code const uint8_t *)om_enum[fdx.arg];
}

static void emit_pmask(void)
{
	v16 = vp[0] | (uint16_t)vp[1] << 8;
	k = 0;				/* printed something */
	for (cur_n = 1; cur_n <= 9; cur_n++) {
		v8 = log_of(cur_n);
		if (v8 == 0xff || !(v16 & (1 << v8)))
			continue;
		/* a run of members: n, or n-m */
		i = cur_n;
		while (cur_n < 9 && (v8 = log_of(cur_n + 1)) != 0xff && (v16 & (1 << v8)))
			cur_n++;
		if (k)
			write_char(',');
		itoa_short(i);
		if (cur_n != i) {
			write_char('-');
			itoa_short(cur_n);
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

static void emit_value(void)		/* fd's value at vp */
{
	switch (fdx.type) {
	case T_BOOL:
		print_string(*vp ? "on" : "off");
		return;
	case T_ENUM:
		enum_words();
		for (v8 = *vp; v8 && *cp; v8--)
			while (*cp++);
		print_string(*cp ? (__code const char *)cp : "?");	/* past the words: ? */
		return;
	case T_STR:
		print_string_x((__xdata char *)vp);
		return;
	case T_PMASK:
		save_n = cur_n;
		emit_pmask();
		cur_n = save_n;
		return;
	}
	/* numbers */
	num_len = fsize();
	for (i = 0; i < num_len; i++)
		num[i] = vp[i];
	emit_dec();
	if (fdx.type == T_SCALED && fdx.arg) {
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
	if (obx.flags & OM_STATE)
		print_string("state.");
	print_string(obx.name);
	if (obx.flags & OM_PORTOBJ) {
		write_char('.');
		itoa_short(cur_n);
	}
	write_char('.');
	print_string(fdx.name);
}

static void emit_line(void)		/* "path value" */
{
	emit_path();
	write_char(' ');
	emit_value();
	write_char('\n');
}

/* ---- values: parse --------------------------------------------------------- */

static uint8_t pmask_item(void)	/* one "n", "n-m", "cpu", "all", "none" at p, into v16 */
{
	if (seg_is("none"))
		return 1;
	if (*p == 'c' && p[1] == 'p' && p[2] == 'u') {
		if (!fdx.arg)
			return 0;
		p += 3;
		pm |= PMASK_CPU;
		return 1;
	}
	if (*p == 'a' && p[1] == 'l' && p[2] == 'l') {
		p += 3;
		for (cur_n = 1; cur_n <= 9; cur_n++)
			if ((v8 = log_of(cur_n)) != 0xff)
				pm |= 1 << v8;
		if (fdx.arg)
			pm |= PMASK_CPU;
		return 1;
	}
	if (!parse_dec() || num[1] || num[2] || num[3])
		return 0;
	k = num[0];
	i = k;
	if (*p == '-') {
		p++;
		if (!parse_dec() || num[1] || num[2] || num[3] || num[0] < k)
			return 0;
		i = num[0];
	}
	for (cur_n = k; cur_n <= i; cur_n++) {
		if ((v8 = log_of(cur_n)) == 0xff)
			return 0;
		pm |= 1 << v8;
	}
	return 1;
}

/* the value at p into vbuf, checked; prints err and returns 0 if bad */
static uint8_t parse_value(void)
{
	switch (fdx.type) {
	case T_BOOL:
		if (seg_is("on"))
			vbuf[0] = 1;
		else if (seg_is("off"))
			vbuf[0] = 0;
		else
			goto bad_words;
		break;
	case T_ENUM:
		enum_words();
		for (v8 = 0; *cp; v8++) {
			if (seg_is((__code const char *)cp)) {
				vbuf[0] = v8;
				goto end;
			}
			while (*cp++);
		}
		goto bad_words;
	case T_STR:
		for (k = 0; *p; k++, p++) {
			if (k >= fdx.arg || *p < 0x20 || *p > 0x7e || *p == '"' || *p == '\\') {
				om_fail("printable text without \" or \\, at most its length");
				return 0;
			}
			vbuf[k] = *p;
		}
		vbuf[k] = 0;
		return 1;
	case T_PMASK:
		pm = 0;
		while (1) {
			if (!pmask_item()) {
				om_fail("ports like 1-4,9 (none, all, cpu)");
				return 0;
			}
			if (*p != ',')
				break;
			p++;
		}
		vbuf[0] = pm;
		vbuf[1] = pm >> 8;
		break;
	case T_U8: case T_U16: case T_U32: case T_SCALED:
		if (!parse_dec())
			goto bad_range;
		cp = (__code const uint8_t *)&om_lim[fdx.lim];
		if (cmp_lim() < 0)
			goto bad_range;
		cp += 4;
		if (cmp_lim() > 0)
			goto bad_range;
		for (i = 0; i < 4; i++)
			vbuf[i] = num[i];
		break;
	default:
		om_fail("read only");
		return 0;
	}
end:
	if (!at_end() || (*p == ' ' && p[1])) {
		om_fail("one value");
		return 0;
	}
	return 1;
bad_words:
	print_string("err ");
	print_path_word();
	print_string(": ");
	if (fdx.type == T_BOOL) {
		print_string("on off");
	} else {
		enum_words();
		while (*cp) {
			print_string((__code const char *)cp);
			while (*cp++);
			if (*cp)
				write_char(' ');
		}
	}
	write_char('\n');
	err_status = ERR_INVALID_ARGUMENT;
	return 0;
bad_range:
	print_string("err ");
	print_path_word();
	print_string(": ");
	print_lim();
	write_char('\n');
	err_status = ERR_INVALID_ARGUMENT;
	return 0;
}

/* ---- paths ------------------------------------------------------------ */

/* the object and index of the path word; prints err and returns 0 if bad */
static uint8_t resolve(void)
{
	p = cmd_buffer + cmd_words_b[c_pw];
	v8 = seg_is("state") ? OM_STATE : OM_CFG;
	for (r_o = 0; r_o < OM_N_OBJ; r_o++) {
		sel_obj(r_o);
		if ((obx.flags & OM_STATE) == v8 && seg_is(obx.name))
			break;
	}
	if (r_o == OM_N_OBJ) {
		print_string("err ");
		print_path_word();
		print_string(":");
		for (r_o = 0; r_o < OM_N_OBJ; r_o++) {
			if ((om_obj[r_o].flags & OM_STATE) == v8) {
				write_char(' ');
				print_string(om_obj[r_o].name);
			}
		}
		write_char('\n');
		err_status = ERR_INVALID_ARGUMENT;
		return 0;
	}
	r_n = 0xff;
	if (obx.flags & OM_PORTOBJ) {
		if (*p == '*') {
			p++;
			if (*p == '.')
				p++;
		} else if (*p >= '0' && *p <= '9') {
			if (!parse_dec() || num[1] || log_of(num[0]) == 0xff || !(*p == '.' || at_end())) {
				om_fail("no such port");
				return 0;
			}
			r_n = num[0];
			if (*p == '.')
				p++;
		}
	}
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

static void fields_err(void)		/* err <path>: the object's fields */
{
	print_string("err ");
	print_path_word();
	print_string(":");
	for (cur_f = 0; cur_f < obx.nf; cur_f++) {
		write_char(' ');
		sel_field();
		print_string(fdx.name);
	}
	write_char('\n');
	err_status = ERR_INVALID_ARGUMENT;
}

/* the field the rest of the path names exactly; prints err and returns 0 if none */
static uint8_t find_field(void)
{
	for (cur_f = 0; cur_f < obx.nf; cur_f++) {
		sel_field();
		if (fmatch(1))
			return 1;
	}
	fields_err();
	return 0;
}

/* the next instance of ob at or after cur_n (0xff: none left) */
static void next_inst(void)
{
	if (!(obx.flags & OM_PORTOBJ)) {
		cur_l = 0;
		return;
	}
	for (; cur_n <= 9; cur_n++) {
		if ((r_n == 0xff || r_n == cur_n) && (cur_l = log_of(cur_n)) != 0xff)
			return;
	}
	cur_n = 0xff;
}

static void first_inst(void)
{
	cur_n = 1;
	next_inst();
}

static void call(uint8_t fn)
{
	om_fns[fn](cur_l);
}

/* ---- the job: get, diff and apply, a line or an instance a step ---------- */

static void job_inst(void)		/* entering an instance */
{
	if ((obx.flags & OM_STATE) && obx.get != 0xff)
		call(obx.get);
	cur_f = 0;
}

static void job_start(uint8_t mode)
{
	j_mode = mode;
	cli_job = mode == J_APPLY ? CLI_JOB_OMAPPLY : CLI_JOB_OM;
	j_new = 1;
	if (mode != J_GET)
		r_n = 0xff;
}

/* the next field to print, or 0 at the end of the instance */
static uint8_t next_field(void)
{
	for (; cur_f < obx.nf; cur_f++) {
		sel_field();
		if (fdx.type == T_ACT || !om_when())
			continue;
		if (j_mode == J_GET) {
			if (fmatch(0))
				return 1;
		} else {
			vp = inst((__xdata uint8_t *)&cfg_run) + fdx.off;
			if (memcmp(vp, inst((__xdata uint8_t *)&cfg_cand) + fdx.off, fsize()))
				return 1;
		}
	}
	return 0;
}

static uint8_t next_obj(void)		/* diff, apply: the next config object from j_o */
{
	for (; j_o < OM_N_OBJ; j_o++) {
		sel_obj(j_o);
		if (!(obx.flags & OM_STATE)) {
			first_inst();
			if (cur_n != 0xff) {
				job_inst();
				return 1;
			}
		}
	}
	return 0;
}

static void next_instance(void)
{
	if (obx.flags & OM_PORTOBJ) {
		cur_n++;
		next_inst();
	} else {
		cur_n = 0xff;
	}
	if (cur_n != 0xff)
		job_inst();
}

/* apply: the instance, if it changed; 1 if it did */
static uint8_t apply_inst(void)
{
	vp = inst((__xdata uint8_t *)&cfg_run);
	if (!memcmp(vp, inst((__xdata uint8_t *)&cfg_cand), obx.size))
		return 0;
	memcpy(inst((__xdata uint8_t *)&cfg_roll), vp, obx.size);
	memcpy(vp, inst((__xdata uint8_t *)&cfg_cand), obx.size);
	if (obx.apply != 0xff)
		call(obx.apply);
	return 1;
}

uint8_t om_line(void) __banked
{
	if (j_new) {
		j_new = 0;
		if (j_mode == J_GET) {
			sel_obj(r_o);
			first_inst();
			if (cur_n != 0xff)
				job_inst();
		} else {
			j_o = 0;
			if (!next_obj())
				goto done;
		}
	}
	while (1) {
		if (cur_n == 0xff) {		/* the object is done */
			if (j_mode == J_GET)
				goto done;
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
				vp = inst((__xdata uint8_t *)(c_cand ? &cfg_cand : &cfg_run)) + fdx.off;
				emit_line();
			} else {
				print_string("- ");
				emit_line();
				vp = inst((__xdata uint8_t *)&cfg_cand) + fdx.off;
				print_string("+ ");
				emit_line();
			}
			cur_f++;
			return 1;
		}
		next_instance();
	}
done:
	if (!j_quiet)
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
	if (cmd_words_len <= c_pw) {
		for (r_o = 0; r_o < OM_N_OBJ; r_o++) {
			if (om_obj[r_o].flags & OM_STATE)
				print_string("state.");
			print_string(om_obj[r_o].name);
			write_char('\n');
		}
		ok();
		return;
	}
	if (!resolve())
		return;
	/* something must match (on the one port named, if one is) */
	first_inst();
	w = 0;
	for (cur_f = 0; cur_f < obx.nf; cur_f++) {
		sel_field();
		if (fdx.type != T_ACT && fmatch(0)) {
			w = 1;
			if (r_n == 0xff || om_when())
				break;
		}
	}
	if (cur_f == obx.nf) {
		if (w)
			om_fail("not on this port");
		else
			fields_err();
		return;
	}
	job_start(J_GET);
}

/* set/del/do: the field, and each instance the path names */
static uint8_t target(void)
{
	c_pw = 1;
	if (cmd_words_len < 2) {
		print_string("err: a path\n");
		err_status = ERR_INVALID_ARGUMENT;
		return 0;
	}
	if (!resolve() || !find_field())
		return 0;
	if (obx.flags & OM_STATE) {
		om_fail("read only");
		return 0;
	}
	return 1;
}

static void om_set(uint8_t del)
{
	if (!target())
		return;
	if (fdx.type == T_ACT) {
		om_fail(del ? "an action" : "an action: do it");
		return;
	}
	if (!del) {
		p = cmd_words_len > 2 ? cmd_buffer + cmd_words_b[2] : &zero;
		if (!parse_value())
			return;
	}
	k = fsize();
	v8 = 0;
	for (first_inst(); cur_n != 0xff; cur_n++, next_inst()) {
		if (!om_when())
			continue;
		vp = inst((__xdata uint8_t *)&cfg_cand) + fdx.off;
		if (del)
			memcpyc(vp, obx.def + fdx.off, k);
		else
			memcpy(vp, vbuf, k);
		v8 = 1;
		if (!(obx.flags & OM_PORTOBJ))
			break;
	}
	if (!v8) {
		om_fail("not on this port");
		return;
	}
	ok();
}

static void om_do(void)
{
	if (!target())
		return;
	if (fdx.type != T_ACT) {
		om_fail("not an action");
		return;
	}
	for (first_inst(); cur_n != 0xff; cur_n++, next_inst()) {
		if (!om_when()) {
			if (r_n != 0xff) {
				om_fail("not on this port");
				return;
			}
			continue;
		}
		if (om_fns[fdx.arg](cur_l) != OM_OK)
			return;
		if (!(obx.flags & OM_PORTOBJ))
			break;
	}
	ok();
}

/* every changed instance's check, on the candidate */
static uint8_t check_all(void)
{
	c_pw = 0;
	for (r_o = 0; r_o < OM_N_OBJ; r_o++) {
		sel_obj(r_o);
		if ((obx.flags & OM_STATE) || obx.check == 0xff)
			continue;
		r_n = 0xff;
		for (first_inst(); cur_n != 0xff; cur_n++, next_inst()) {
			if (om_fns[obx.check](cur_l) != OM_OK)
				return 0;
			if (!(obx.flags & OM_PORTOBJ))
				break;
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
		for (cur_l = 0; cur_l < ((obx.flags & OM_PORTOBJ) ? OM_PORTS : 1); cur_l++) {
			vp = inst((__xdata uint8_t *)&cfg_run);
			memcpyc(vp, obx.def, obx.size);
		}
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
