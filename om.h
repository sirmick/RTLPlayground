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

/* om_obj.flags */
#define OM_CFG		0
#define OM_STATE	1
#define OM_PORTOBJ	2	/* numbered by front-panel port */

/* om_field.when: where a field exists (om_when()) */
#define W_ALWAYS	0
#define W_COPPER	1
#define W_SFP		2
#define W_10G		3

struct om_field {
	__code const char *name;
	uint8_t id;		/* stable ID (schema/registry.txt), config fields */
	uint8_t type;
	uint8_t off;		/* offsetof in the object's struct */
	uint8_t arg;		/* enum index, string length, decimals, handler */
	uint8_t lim;		/* om_lim[] row, integers */
	uint8_t when;
};

struct om_lim {
	uint32_t lo, hi;
};

struct om_obj {
	__code const char *name;
	uint8_t id, flags, nf;
	__code const struct om_field *f;
	uint16_t base;		/* offsetof(struct cfg, obj); 0 in om_st */
	uint8_t size;
	__code const uint8_t *def;	/* default instance (config) */
	uint8_t apply, check, get;	/* om_fns[] index, 0xff = none */
};

/* A handler: one argument (the logical port, or 0), OM_OK or OM_ERR */
typedef uint8_t (*om_fn)(uint8_t) __banked;
#define OM_OK	0
#define OM_ERR	1

/* Every user port and the CPU: the reset default of port isolation */
#define OM_ISOLATE_DEFAULT	((uint16_t)(0x200 | (((1 << (MACHINE_MAX_PORT + 1)) - 1) & \
				~((1 << MACHINE_MIN_PORT) - 1))))

/* a 10G-capable port (machine.n_10g: logical 3, and 8 with two) */
#define OM_TEN_G(l)	(((l) == 3 && machine.n_10g) || ((l) == 8 && machine.n_10g == 2))

void om_init(void) __banked;		/* defaults into every copy */
void om_cmd(void) __banked;		/* get set del diff apply discard do */
uint8_t om_line(void) __banked;		/* the cli job's next line; 0 when done */
void om_boot_apply(void) __banked;	/* after the boot config: apply it */
/* for handlers: "err <the command's path>: why", err_status set */
void om_fail(__code const char *why) __banked;

#endif
