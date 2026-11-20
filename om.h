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
 * A derive handler fills om_val with the value of field om_fid (stored
 * empty: its default) and returns OM_OK, or OM_ERR for a field it doesn't
 * derive; get and diff print the derived value.
 */
extern __xdata uint8_t om_val[], om_fid;

/* Every user port and the CPU: the reset default of port isolation */
#define OM_ISOLATE_DEFAULT	((uint16_t)(0x200 | (((1 << (MACHINE_MAX_PORT + 1)) - 1) & \
				~((1 << MACHINE_MIN_PORT) - 1))))

/* a 10G-capable port (machine.n_10g: logical 3, and 8 with two) */
#define OM_TEN_G(l)	(((l) == 3 && machine.n_10g) || ((l) == 8 && machine.n_10g == 2))

void om_init(void) __banked;		/* defaults into every copy */
void om_cmd(void) __banked;		/* get set del diff apply discard do */
uint8_t om_line(void) __banked;		/* the cli job's next line; 0 when done */
void om_boot_apply(void) __banked;	/* after the boot config: apply it */
/*
 * for handlers: "err <the command's path>: <message>", err_status set. The
 * message is an ID (OM_E_*, from the schema's message() lines): a handler's
 * own string would be __code in its bank, which om.c can't read.
 */
void om_fail(uint8_t msg) __banked;

#endif
