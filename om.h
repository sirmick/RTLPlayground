#ifndef _OM_H_
#define _OM_H_
/*
 * The object model (~/rtlpg-bench/object-model/DESIGN.md): one tree of
 * config, state and actions, described in schema/ and turned into tables
 * (om_gen.h/.c in the build directory) that this engine reads. Text form,
 * everywhere: one line per field, "path value".
 */
#include <stdint.h>
#include "machine.h"

#ifndef OM_HELP
#define OM_HELP		1	/* help (DESIGN.md 13); 0 compiles it out, its text too */
#endif

/* field types (tools/om_gen.py: T) */
#define T_BOOL		1
#define T_U8		2
#define T_U16		3
#define T_U32		4
#define T_U64		5
#define T_ENUM		6
#define T_STR		7
#define T_PMASK		8
#define T_SCALED	9
#define T_ACT		10
#define T_IPV4		11
#define T_MASK4		12	/* an IPv4 netmask: contiguous */
#define T_MAC		13
#define T_FLAGS		14	/* up to 8 words, a bit each */
#define T_FLAGS16	15
#define T_SECRET	16	/* a Str that get prints as * */
#define T_I16		17
#define T_TZ		18	/* I16 minutes, as +hh:mm */
#define T_PORT		19	/* one port: logical inside, front-panel number outside */
#define T_BRID		20	/* bridge ID: priority (big endian) and MAC, as prio/4096.MAC */
#define T_TYPE		0x1f	/* om_field.tw: the type; the when rule above it */
#define T_WHEN		5
#define T_DERIVED	0x80	/* a Str stored empty is printed as the object's derive handler's */

/* om_obj.flags */
#define OM_CFG		0
#define OM_STATE	1
#define OM_CHIPDEF	2	/* two element defaults: RTL8372, RTL8373 */

/* om_obj.k[]: an index level's kind */
#define IX_PORT		0	/* front-panel number; the element is the logical port */
#define IX_NUM		1	/* 1..n (n 1 and no '#' in the path: no index) */
#define IX_KEY		2	/* keyed: n slots, a key in om_lim[a] at the element's start */
#define IX_WORD		3	/* the words of om_enum[a] */
#define IX_IDX		4	/* 0..n-1 */
#define IX_STREAM	5	/* 0..om_lim[a].hi, found by the getter (state, level 0 only) */

/* om_field.when: where a field exists (om_when()) */
#define W_ALWAYS	0
#define W_COPPER	1
#define W_SFP		2
#define W_10G		3

struct om_field {
	__code const char *name;
	uint8_t id;		/* stable ID (schema/registry.txt), config fields */
	uint8_t tw;		/* type | when << T_WHEN | T_DERIVED */
	uint8_t off;		/* offsetof in the element's struct */
	uint8_t arg;		/* enum, string length, decimals, om_lim[] row, handler, flag */
};

struct om_lim {
	uint32_t lo, hi;	/* I16, TZ: biased by 0x8000 */
};

struct om_obj {
	__code const char *name;	/* the path, '#' an index: "port.#", "port.#.storm.#" */
	__code const struct om_field *f;
	__code const uint8_t *def;	/* element default (config); keyed: then the default entries */
	uint16_t base;		/* offsetof(struct cfg, obj); 0 in om_st */
	uint8_t id, flags, nf, size;
	uint8_t k[2], n[2], a[2];	/* index levels: kind, count, enum or om_lim[] row */
	uint8_t ndef;		/* keyed: default entries */
	uint8_t apply, check, get, derive;	/* om_fns[] index, 0xff = none */
};

/* A handler: one argument (the element: logical port, number, slot), OM_OK or OM_ERR */
typedef uint8_t (*om_fn)(uint8_t) __banked;
#define OM_OK	0
#define OM_ERR	1

/*
 * A state getter fills om_st for its instance: idx is level 0's element;
 * a second level's index is om_ix1. A streamed object's getter finds the
 * first instance at or after om_key, sets om_key to it and fills om_st, or
 * returns OM_ERR if there is none (the search wrapped). get <path>.K- is
 * a page: the first OM_PAGE instances at or after K.
 */
extern __xdata uint8_t om_ix1;
extern __xdata uint16_t om_key;
#define OM_PAGE	25	/* get <stream>.K-: at most this many instances from K on */

/*
 * A derive handler fills om_val with the value of field om_fid (stored
 * empty: its default) and returns OM_OK, or OM_ERR for a field it doesn't
 * derive; get and diff print the derived value.
 */
extern __xdata uint8_t om_val[], om_fid;

/* Every user port and the CPU: the reset default of port isolation */
#define OM_ISOLATE_DEFAULT	((uint16_t)(0x200 | (((1 << (MACHINE_MAX_PORT + 1)) - 1) & \
				~((1 << MACHINE_MIN_PORT) - 1))))

/* Every user port: VLAN 1's default members */
#define OM_PORTS_USER	(OM_ISOLATE_DEFAULT & ~0x200)

/* a 10G-capable port (machine.n_10g: logical 3, and 8 with two) */
#define OM_TEN_G(l)	(((l) == 3 && machine.n_10g) || ((l) == 8 && machine.n_10g == 2))

/*
 * Staging (DESIGN.md 5.2-5.3): apply runs as the cfg job (om_job.c), an
 * instance a step; apply N then holds a confirm window of N seconds, and its
 * end without confirm rolls back. om_apply_st is state.cfg.apply (E_CFGAPPLY_*).
 */
#define OM_A_MORE	0
#define OM_A_DONE	1
#define OM_A_FAIL	2
extern __xdata uint8_t om_apply_st, om_confirm_s, om_confirmed, om_dirty;
extern __xdata uint8_t om_full;	/* the boot's apply: what the hardware has is unknown */
extern __xdata uint8_t om_cut;		/* the get or diff's next line says a rollback cut it */
uint8_t om_apply_next(void) __banked;	/* the next changed instance: OM_A_* */
void om_undo(void) __banked;		/* running back to the rollback copy; walk again */
void om_settle(uint8_t r) __banked;	/* the walk is over (r: its last result): its copies queued */
uint8_t om_copy(void) __banked;		/* the queued copies' next chunk: 1 while more */
void om_prompt(void) __banked;		/* "[confirm N] " and "*" before "> " */
uint8_t om_job_start(void) __banked;	/* om_job.c: the cfg job; 0 if no slot */
void om_job_wake(void) __banked;		/* om_job.c: confirm came */

/* write_char's capture: om_cap 1 + bytes into om_txt (om.c val_ok()), 0xff: nowhere */
#define OM_TXT	48
extern __xdata uint8_t om_cap, om_txt[OM_TXT];
void om_capture(char c) __banked;	/* write_char's, while om_cap */

void om_init(void) __banked;		/* defaults into every copy */
uint8_t om_cmd(void) __banked;		/* get set del diff apply confirm discard do save load: 0 if none */
/* Tab on con_line: OM_TAB_NEW (+ 1: list), or OM_TAB_MORE: its next step.
 * 0 completed, 1 nothing (BEL: the next Tab lists), OM_TAB_MORE: call again */
#define OM_TAB_MORE	2
#define OM_TAB_NEW	3
uint8_t om_tab(uint8_t arg) __banked;
uint8_t om_line(void) __banked;		/* the cli job's next line; 0 when done */
void om_boot_apply(void) __banked;	/* after the boot config: apply it */
uint8_t om_boot_load(uint8_t dflt) __banked;	/* the saved (or default) sector into the candidate: E_CFGFORMAT_* */
uint16_t om_save_chunk(void) __banked;	/* FL_SAVE: the next chunk into flash_buf */
uint8_t om_save_end(void) __banked;	/* FL_SAVE: read it back, a step at a time: 1 while more */
/*
 * for handlers: "err <the command's path>: <message>", err_status set. The
 * message is an ID (OM_E_*, from the schema's message() lines): a handler's
 * own string would be __code in its bank, which om.c can't read.
 */
void om_fail(uint8_t msg) __banked;
void om_fail_ports(uint8_t msg) __banked;	/* and the ports in om_val (a PortMask): "... 3-4" */
/* at boot (om_booting), a reference check fixes its field instead of refusing
 * (mgmt.vlan to 0, port.N.pvid to 1: mgmt must survive); om_boot_note names it
 * in state.cfg.boot_defaults by a message whose text is its path. If the fix
 * still refuses (VLAN 1 itself missing), the check sets om_boot_ref to the
 * referenced object (O_CFG_*) and refuses: boot resets that object instead */
extern __xdata uint8_t om_booting, om_boot_ref;
void om_boot_note(uint8_t msg) __banked;

/* om_vlan.c: vid is 0, or a VLAN of the candidate (port.N.pvid, mgmt.vlan) */
uint8_t om_vlan_known(uint16_t vid) __banked;

#endif
