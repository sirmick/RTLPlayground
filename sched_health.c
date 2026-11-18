/*
 * sched_health.c - `health tasks`, `health clear`, and the boot-time line of
 * the crash record. Banked: it reads state_name/slot_name directly, so
 * place_banks.py puts it in the bank with sched_gen_names.c.
 */
#include <8051.h>
#include <stdint.h>
#include "rtl837x_common.h"
#include "sched.h"
#include "con_ring.h"
#include "crash.h"

static __xdata uint8_t i, ps;
static __xdata uint16_t u, lo;

static __code const char * __code const cause_name[] = {
	"?", "watchdog", "^R ^R", "reset command", "update"
};

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

/* crash_last as one line:
 * watchdog in stp/STP_SECOND after 5000 ms, PC 0x1234 bank 2, states ... */
static void crash_print(void)
{
	print_string(cause_name[crash_last.cause <= CRASH_UPDATE ? crash_last.cause : 0]);
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

void sched_health_tasks(void) __banked
{
	print_string("slot\ttask\tstate\tmask\twake\n");
	for (i = 0; i < SCHED_SLOTS; i++) {
		itoa(i);
		write_char('\t');
		ps = i;
		print_slot();
		write_char('\t');
		ps = sched_task[i].state;
		if (!ps) {
			print_string("free\n");
			continue;
		}
		print_state();
		write_char('\t');
		print_short(sched_task[i].mask);
		u = sched_task[i].wake - sched_now;
		if ((int16_t)u <= 0) {
			print_string("\tnow\n");
			continue;
		}
		print_string("\t+");
		itoa_short(u);
		write_char('\n');
	}
	print_string("state\truns\tmax\n");
	for (ps = 1; ps < S_COUNT; ps++) {
		print_state();
		write_char('\t');
		itoa_short(sched_st_runs[ps]);
		write_char('\t');
		u = sched_st_max[ps];
		print_fine();
		write_char('\n');
	}
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
	write_char('\n');
	if (crash_last.magic == CRASH_MAGIC) {
		print_string("last reset: ");
		crash_print();
	}
}

void sched_health_clear(void) __banked
{
	sched_clear_stats();
	ES = 0;			/* an ISR increment mid-write would leave a stray count */
	con_rx_overruns = 0;
	ES = 1;
}

/* At boot: a record whose magic and sum hold is printed once, kept for
 * health, and consumed. A power-on finds garbage and ignores it. */
void crash_boot(void) __banked
{
	crash_last.magic = 0;
	crash_prior = 0;
	if (crash_rec.magic != CRASH_MAGIC || crash_rec.sum != crash_sum())
		return;
	for (i = 0; i < sizeof(struct crash_rec); i++)
		((__xdata uint8_t *)&crash_last)[i] = ((__xdata uint8_t *)&crash_rec)[i];
	crash_prior = crash_rec.resets < 0xff ? crash_rec.resets + 1 : 0xff;
	crash_rec.magic = 0;
	print_string("Last reset: ");
	crash_print();
}
