/*
 * sched_health.c - `health tasks`, `health clear`, the crash record at boot
 * and as state.sys.reset. Banked: it reads state_name/slot_name directly, so
 * place_banks.py puts it in the bank with sched_gen_names.c.
 */
#include <8051.h>
#include <stdint.h>
#include <stddef.h>
#include "rtl837x_common.h"
#include "sched.h"
#include "con_ring.h"
#include "crash.h"
#include "build_id.h"
#include "om_gen.h"

static __xdata uint8_t i, ps;
static __xdata uint16_t u, lo;

/* A 32-bit count kept as two halves, in hex. */
static void print_halves(void)		/* u = high, then lo */
{
	print_short(u);
	print_byte(lo >> 8);
	print_byte(lo);
}

/* Fine units u as microseconds up to ~45 ms, else milliseconds. */
static void print_fine(void)
{
	if (u == 0xffff) {
		print_string(">400ms");
	} else if (u <= 7281) {
		itoa_short(u * 6 + ((u * 9) >> 6));	/* x 6.144 */
		print_string("us");
	} else {
		itoa_short(u / 163);			/* x 6.144 / 1000 */
		print_string("ms");
	}
}

static void print_state(void)		/* state ps */
{
	print_string(ps < S_COUNT ? state_name[ps] : "?");
}

static void print_slot(void)		/* slot ps */
{
	print_string(ps < SCHED_SLOTS ? slot_name[ps] : "-");
}

#ifdef SCHEDTEST	/* its sim checks (schedtest/sim_neg.py) read the record as a line */
static __code const char * __code const cause_name[] = {
	"?", "watchdog", "^R ^R", "reset command", "update", "web", "button", "factory"
};

/* crash_last as one line:
 * watchdog in stp/STP_SECOND after 5000 ms, PC 0x1234 bank 2, states ... */
static void crash_print(void)
{
	print_string(cause_name[crash_last.cause <= CRASH_FACTORY ? crash_last.cause : 0]);
	print_string(" in ");
	ps = crash_last.slot;
	print_slot();
	write_char('/');
	ps = crash_last.state;
	print_state();
	print_string(" after ");
	u = crash_last.tick - crash_last.step_tick;
	itoa_short(u < 13107 ? u * SCHED_TICK_MS : 65535);
	print_string(" ms");
	if (crash_last.pc) {
		print_string(", PC ");
		print_short(crash_last.pc);
	}
	print_string(" bank ");
	itoa(crash_last.bank);
	print_string(", states");
	for (i = 0; i < 8; i++) {		/* oldest first */
		write_char(' ');
		ps = crash_last.ring[(crash_last.ring_idx + i) & 7];
		print_state();
	}
	write_char('\n');
}
#endif

/* health tasks, a line per call (the cli's health job, E1): the header,
 * each slot, the states' header, each state, then the totals. */
static __xdata uint8_t hl;

void sched_health_begin(void) __banked
{
	hl = 0;
}

uint8_t sched_health_line(void) __banked
{
	if (hl == 0) {
		print_string("slot\ttask\tstate\tmask\twake\n");
	} else if (hl <= SCHED_SLOTS) {
		i = hl - 1;
		itoa(i);
		write_char('\t');
		ps = i;
		print_slot();
		write_char('\t');
		ps = sched_task[i].state;
		if (!ps) {
			print_string("free\n");
		} else {
			print_state();
			write_char('\t');
			print_short(sched_task[i].mask);
			u = sched_task[i].wake - sched_now;
			if ((int16_t)u <= 0) {
				print_string("\tnow\n");
			} else {
				print_string("\t+");
				itoa_short(u);
				write_char('\n');
			}
		}
	} else if (hl == SCHED_SLOTS + 1) {
		print_string("state\truns\tmax\n");
	} else if (hl < SCHED_SLOTS + 1 + S_COUNT) {
		ps = hl - SCHED_SLOTS - 1;
		print_state();
		write_char('\t');
		itoa_short(sched_st_runs[ps]);
		write_char('\t');
		u = sched_st_max[ps];
		print_fine();
		write_char('\n');
	} else {
		print_string("passes ");
		u = sched_passes_hi;
		lo = sched_passes_lo;
		print_halves();
		print_string(" idle ");
		u = sched_idle_hi;
		lo = sched_idle_lo;
		print_halves();
		print_string(" longest ");
		u = sched_pass_max;
		print_fine();
		print_string("\noverruns ");
		itoa_short(sched_overruns);
		if (sched_overruns) {
			print_string(" last ");
			ps = sched_overrun_state;
			print_state();
		}
		print_string(", con rx overruns ");
		ES = 0;			/* the ISR writes it: read both bytes at once */
		u = con_rx_overruns;
		ES = 1;
		itoa_short(u);
		print_string(", hw timeouts ");	/* E4: bounded polls that gave up */
		itoa_short(hw_timeouts);
		if (hw_timeouts) {
			print_string(" last ");
			print_short(hw_timeout_reg);
		}
		write_char('\n');
#ifdef SCHEDTEST
		if (crash_last.magic == CRASH_MAGIC) {
			print_string("last reset: ");
			crash_print();
		}
#endif
		return 0;
	}
	hl++;
	return 1;
}

/* All of it at once (schedtest, the host tests) */
void sched_health_tasks(void) __banked
{
	hl = 0;
	while (sched_health_line())
		;
}

void sched_health_clear(void) __banked
{
	sched_clear_stats();
	ES = 0;			/* an ISR increment mid-write would leave a stray count */
	con_rx_overruns = 0;
	ES = 1;
}

/* At boot: a record whose magic and sum hold, written by this image, is
 * kept for state.sys.reset, and consumed. A power-on finds garbage and
 * ignores it; a record another image left (a web update keeps xdata) is
 * dropped unread: its slots and states aren't this image's. An update's is
 * the exception: kept, without them (slot none, states "-"). */
/* the record's step fields, tick to ring_idx: none (slot -, states -) */
static void crash_blank(void)
{
	memset((__xdata uint8_t *)&crash_rec.tick, 0,
	       offsetof(struct crash_rec, spare) - offsetof(struct crash_rec, tick));
	crash_rec.slot = 0xff;
}

void crash_boot(void) __banked
{
	crash_last.magic = 0;
	crash_prior = 0;
	if (crash_rec.magic != CRASH_MAGIC || crash_rec.sum != crash_sum())
		goto out;
	if (crash_rec.image_lo != CRASH_IMAGE_LO || crash_rec.image_hi != CRASH_IMAGE_HI) {
		if (crash_rec.cause != CRASH_UPDATE) {
			crash_rec.magic = 0;
			goto out;
		}
		crash_blank();			/* consumed below: the sum no longer matters */
	}
	for (i = 0; i < sizeof(struct crash_rec); i++)
		((__xdata uint8_t *)&crash_last)[i] = ((__xdata uint8_t *)&crash_rec)[i];
	crash_prior = crash_rec.resets < 0xff ? crash_rec.resets + 1 : 0xff;
	crash_rec.magic = 0;
#ifdef SCHEDTEST
	print_string("Last reset: ");
	crash_print();
#endif
out:
	/* the last run's notes: a reset before the scheduler runs (^R ^R in the
	 * boot) records no step of its own */
	crash_blank();
}

/* state.sys.reset (schema/state_sys.py): the record crash_boot kept; none,
 * cause power. The names are copied here, beside their tables. */
#define R	om_st.sys_reset
static __xdata char * __xdata dp;

static void put(__code const char *s)
{
	while (*s)
		*dp++ = *s++;
	*dp = 0;
}

uint8_t om_get_sys_reset(uint8_t idx) __banked
{
	(void)idx;
	memset((__xdata uint8_t *)&R, 0, sizeof(R));
	if (crash_last.magic != CRASH_MAGIC)
		return OM_OK;
	R.cause = crash_last.cause;
	ps = crash_last.slot;
	dp = R.slot;
	put(ps < SCHED_SLOTS ? slot_name[ps] : "-");
	ps = crash_last.state;
	dp = R.state;
	put(ps < S_COUNT ? state_name[ps] : "?");
	u = crash_last.tick - crash_last.step_tick;
	R.step_ms = u < 13107 ? u * SCHED_TICK_MS : 65535;
	R.pc = crash_last.pc;
	R.bank = crash_last.bank;
	dp = R.states;
	for (i = 0; i < 8; i++) {		/* oldest first */
		if (i)
			put(" ");
		ps = crash_last.ring[(crash_last.ring_idx + i) & 7];
		put(ps < S_COUNT ? state_name[ps] : "?");
	}
	return OM_OK;
}

/* rcause's words are crash.h's causes (SDCC's _Static_assert only warns) */
typedef char rcause_is_cause[E_RCAUSE_WATCHDOG == CRASH_SWDT && E_RCAUSE_CTRL_R == CRASH_CTRL_R &&
	E_RCAUSE_CMD == CRASH_RESET_CMD && E_RCAUSE_UPDATE == CRASH_UPDATE && E_RCAUSE_WEB == CRASH_WEB &&
	E_RCAUSE_BUTTON == CRASH_BUTTON && E_RCAUSE_FACTORY == CRASH_FACTORY ? 1 : -1];
