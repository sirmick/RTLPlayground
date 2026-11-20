/*
 * test_sched.c - the scheduler core (sched.c), the console rings
 * (con_ring.c), the watchdog and crash record (crash.c) and the health
 * output (sched_health.c), compiled unmodified against a fake tick, fake
 * Timer 2 and fake ISR bits (doc/sched/testing.md, layer 1).
 *
 * The tasks are the TASK/STEP lines below, swept (tools/sched_sweep.py) and
 * generated (tools/sched_gen.py) with the run sequence in
 * sched_core/sequence.def, "a b a w job job job job job" (--slots 8). Every step
 * logs itself and then does what act[] says for its state.
 */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "sched.h"
#include "con_ring.h"
#include "crash.h"
#include "build_id.h"
#include "om_gen.h"

static int tests_run, tests_failed;
#define CHECK(cond, msg) do { \
	tests_run++; \
	if (!(cond)) { tests_failed++; printf("  FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__); } \
	else printf("  ok:   %s\n", (msg)); \
} while (0)

/* ---- the hardware edge ---- */
volatile bool EA = 1, ES = 1, TI, RI;
volatile uint8_t PCON, SP = 0x60, SBUF;
volatile uint8_t TL2, TH2, T2CON, RCAP2L = 0xad, RCAP2H = 0x34, PSBANK = 2;
volatile uint32_t ticks;
volatile bool link_irq, rx_irq;
uint8_t host_idata[256];
uint16_t hw_timeouts, hw_timeout_reg;	/* rtlplayground.c's poll bounds (E4) */
uint8_t hw_stuck;
static jmp_buf reset_jmp;
static int resets;
void host_reset(void) { resets++; longjmp(reset_jmp, 1); }

/* ---- console output ---- */
static char out[16384];
static size_t out_len;
void write_char(char c) { if (out_len < sizeof(out) - 1) { out[out_len++] = c; out[out_len] = 0; } }
void print_string(const char *p) { while (*p) write_char(*p++); }
void print_short(uint16_t a) { char b[8]; snprintf(b, sizeof(b), "0x%04x", a); print_string(b); }
void print_byte(uint8_t a) { char b[4]; snprintf(b, sizeof(b), "%02x", a); print_string(b); }
void itoa(uint8_t v) { char b[4]; snprintf(b, sizeof(b), "%u", v); print_string(b); }
void itoa_short(uint16_t v) { char b[6]; snprintf(b, sizeof(b), "%u", v); print_string(b); }
static void out_reset(void) { out_len = 0; out[0] = 0; }

/* ---- the steps ---- */
enum { A_NONE, A_SLEEP, A_WAIT, A_END, A_WAKE, A_NEXT, A_BURN, A_CLAIM, A_UNTIL, A_TIMER,
       M_NEXT, M_SLEEP, M_WAIT, M_ARG, M_END, A_ARGINC, A_CLAIMARG };
static struct { int kind; uint16_t a, b, c; } act[S_COUNT];
static uint16_t seen_arg;
static char trace[256];
static int ntrace;
static uint8_t claimed;

/* Every step below returns what this says: the test's act[] chooses the
 * state, so the steps are marked as returning a helper's (sched_sweep.py). */
static uint8_t step(uint8_t s, char tag)
{
	if (ntrace < (int)sizeof(trace) - 1)
		trace[ntrace++] = tag;
	switch (act[s].kind) {
	case A_SLEEP: task_sleep(act[s].a); break;
	case A_WAIT:  task_wait(act[s].a, act[s].b); break;
	case A_END:   return STEP_END;
	case A_WAKE:  task_wake(act[s].a); break;
	case A_NEXT:  return act[s].a;
	case A_BURN:  ticks += act[s].a; break;
	case A_CLAIM: claimed = task_claim(act[s].a); break;
	case A_UNTIL: task_until(act[s].a); break;
	case A_TIMER: TH2 = act[s].a >> 8; TL2 = act[s].a; break;
	case M_NEXT:  return act[s].a;		/* return S_X */
	case M_SLEEP: return STEP_SLEEP(act[s].a, act[s].b);
	case M_WAIT:  return STEP_WAIT(act[s].a, act[s].b, act[s].c);
	case M_ARG:   return STEP_ARG(act[s].a, act[s].b);
	case M_END:   return STEP_END;
	case A_ARGINC: seen_arg = task_arg(); task_set_arg(task_arg() + 1); break;
	case A_CLAIMARG: claimed = task_claim_arg(act[s].a, act[s].b); break;
	}
	return STEP_STAY;
}
TASK(a, fixed, once)
STEP(a, step) { return step(S_A_STEP, 'a'); }	/* helper returns state: step */
TASK(b, fixed, once, entry=alt)
STEP(b, step) { return step(S_B_STEP, 'b'); }	/* helper returns state: step */
STEP(b, alt)  { return step(S_B_ALT, 'B'); }	/* helper returns state: step */
TASK(w, fixed, once)
STEP(w, step) { return step(S_W_STEP, 'w'); }	/* helper returns state: step */
TASK(j, job, once, entry=two)
STEP(j, step) { return step(S_J_STEP, 'j'); }	/* helper returns state: step */
STEP(j, two)  { return step(S_J_TWO, 'J'); }	/* helper returns state: step */

static void reset_all(uint32_t t)
{
	memset(act, 0, sizeof(act));
	ticks = t;
	TH2 = 0x34; TL2 = 0xad; T2CON = 0;
	link_irq = rx_irq = 0;
	con_ring_init();
	sched_init();
	ntrace = 0;
	trace[0] = 0;
}

/* one pass, its trace as a string */
static const char *pass(void)
{
	ntrace = 0;
	sched_pass();
	trace[ntrace] = 0;
	return trace;
}

static void test_init_and_yield(void)
{
	printf("init, default yield, several positions\n");
	reset_all(100);
	CHECK(sched_task[T_A].state == S_A_STEP && sched_task[T_W].state == S_W_STEP, "fixed slots start in their first state");
	CHECK(sched_task[T_JOB0].state == 0 && sched_task[T_JOB4].state == 0, "dynamic slots start free");
	CHECK(!strcmp(pass(), "abaw"), "a step that says nothing runs again at its next position (a twice per pass)");
	CHECK(!strcmp(pass(), "abaw"), "and again next pass");
	CHECK(sched_st_runs[S_A_STEP] == 4 && sched_st_runs[S_B_STEP] == 2, "runs counted per state");
	CHECK(sched_passes_lo == 2, "passes counted");
}

static void test_sleep(void)
{
	printf("ms -> ticks, never early\n");
	int early = 0, wrong = 0;
	for (uint32_t ms = 1; ms < 65535; ms++) {
		uint16_t t = sched_ms_to_ticks(ms);
		if ((uint32_t)(t - 1) * 5 < ms) early++;		/* the call may come just before a tick */
		if (t != (ms + 4) / 5 + 1) wrong++;
	}
	CHECK(early == 0, "ticks - 1 whole ticks cover ms, for every ms");
	CHECK(wrong == 0, "ceil(ms / 5) + 1");
	CHECK(sched_ms_to_ticks(0) == 0, "0 ms is a yield");
	CHECK(sched_ms_to_ticks(65534) <= SCHED_MAX_WAIT, "the longest sleep stays within the horizon");

	reset_all(100);
	act[S_A_STEP].kind = A_SLEEP; act[S_A_STEP].a = 5;
	CHECK(!strcmp(pass(), "abw"), "a sleeps after its first position");
	CHECK(sched_task[T_A].wake == 102, "5 ms from tick 100: due at 102");
	ticks = 101;
	CHECK(!strcmp(pass(), "bw"), "not due at 101");
	ticks = 102;
	CHECK(strchr(pass(), 'a') != NULL, "due at 102");
}

static void test_wrap(void)
{
	printf("16-bit wrap, both ends\n");
	reset_all(0xfffd);
	act[S_A_STEP].kind = A_SLEEP; act[S_A_STEP].a = 10;	/* 3 ticks */
	pass();
	CHECK(sched_task[T_A].wake == 0x0000, "wake wraps to 0x0000");
	ticks = 0xffff;
	CHECK(strchr(pass(), 'a') == NULL, "0xffff: not yet (wake 0x0000 is ahead)");
	ticks = 0x10000;
	CHECK(strchr(pass(), 'a') != NULL, "0x0000: due");

	reset_all(0x10005);
	act[S_A_STEP].kind = A_UNTIL; act[S_A_STEP].a = 0xfff0;
	sched_task[T_A].wake = 0xfff0;
	CHECK(strchr(pass(), 'a') != NULL, "wake 0xfff0 at now 0x0005: late, due");
	reset_all(0xfff0);
	act[S_A_STEP].kind = A_UNTIL; act[S_A_STEP].a = 0x0005;
	sched_task[T_A].wake = 0x0005;
	CHECK(strchr(pass(), 'a') == NULL, "wake 0x0005 at now 0xfff0: ahead across the wrap");
	ticks = 0x10005;
	CHECK(strchr(pass(), 'a') != NULL, "and due when the tick gets there");
}

static void test_events(void)
{
	printf("events: snapshot, task_wake, forever\n");
	reset_all(1000);
	act[S_W_STEP].kind = A_WAIT; act[S_W_STEP].a = HW_LINK; act[S_W_STEP].b = SCHED_FOREVER;
	act[S_A_STEP].kind = A_WAIT; act[S_A_STEP].a = 0; act[S_A_STEP].b = SCHED_FOREVER;
	act[S_B_STEP].kind = A_WAIT; act[S_B_STEP].a = 0; act[S_B_STEP].b = SCHED_FOREVER;
	pass();
	CHECK(sched_task[T_W].mask == (HW_LINK | EV_FOREVER), "a wait without timeout marks the mask");
	CHECK(sched_task[T_W].wake == (uint16_t)(1000 + SCHED_MAX_WAIT), "its wake is the horizon");
	ticks = 1000 + SCHED_MAX_WAIT;
	CHECK(!strcmp(pass(), ""), "at the horizon it does not run");
	CHECK(sched_task[T_W].wake == (uint16_t)(1000 + 2 * SCHED_MAX_WAIT), "the horizon is pushed on");
	CHECK(sched_task[T_W].mask & EV_FOREVER, "still waiting");

	/* a (positions 0 and 2) wakes w (position 3), waiting without timeout:
	 * w runs at its next position, this pass */
	act[S_A_STEP].kind = A_WAKE; act[S_A_STEP].a = T_W;
	sched_task[T_A].mask = 0;
	sched_task[T_A].wake = sched_now;
	CHECK(!strcmp(pass(), "aaw"), "task_wake: w runs at its next position, this pass");
	CHECK(sched_task[T_W].mask == (HW_LINK | EV_FOREVER), "and waits again");
	act[S_A_STEP].kind = A_WAIT; act[S_A_STEP].a = 0; act[S_A_STEP].b = SCHED_FOREVER;
	CHECK(!strcmp(pass(), "a"), "a wake is not kept: w does not run again");
	/* w (position 3) wakes a (0 and 2): after a's positions, the next pass */
	act[S_W_STEP].kind = A_WAKE; act[S_W_STEP].a = T_A;
	sched_task[T_W].mask = 0;
	sched_task[T_W].wake = sched_now;
	CHECK(!strcmp(pass(), "w"), "a woken slot whose positions have passed waits");
	act[S_W_STEP].kind = A_WAIT; act[S_W_STEP].a = HW_LINK; act[S_W_STEP].b = SCHED_FOREVER;
	CHECK(!strcmp(pass(), "aw"), "and runs the next pass, once (a waits again at its first position)");
	CHECK(!strcmp(pass(), ""), "nothing more");
	task_wake(T_JOB0);
	CHECK(sched_task[T_JOB0].state == 0 && !strcmp(pass(), ""), "waking a free slot does nothing");

	link_irq = 1;
	act[S_W_STEP].a = HW_LINK;
	sched_task[T_W].mask = HW_LINK | EV_FOREVER;
	CHECK(!strcmp(pass(), "w"), "ISR bit -> event -> task");
	CHECK(!link_irq, "the snapshot cleared the ISR bit");
	rx_irq = 1;
	pass();
	CHECK((sched_ev & HW_RX) && !rx_irq, "rx_irq -> HW_RX");

	CON_ISR_RX('x');
	act[S_W_STEP].a = HW_CON_RX;
	sched_task[T_W].mask = HW_CON_RX | EV_FOREVER;
	CHECK(!strcmp(pass(), "w") && (sched_ev & HW_CON_RX), "a console byte wakes on HW_CON_RX");
	CHECK(sched_ev & HW_CON_TX, "empty TX ring: HW_CON_TX");
	CHECK(!(sched_ev & 0x10), "bit 4 (was EV_FLASH_IDLE) stays clear");
}

static void test_claim(void)
{
	printf("jobs: claim, free, full\n");
	reset_all(500);
	act[S_A_STEP].kind = A_SLEEP; act[S_A_STEP].a = 60000;
	act[S_B_STEP].kind = A_SLEEP; act[S_B_STEP].a = 60000;
	act[S_W_STEP].kind = A_SLEEP; act[S_W_STEP].a = 60000;
	act[S_J_STEP].kind = A_SLEEP; act[S_J_STEP].a = 60000;
	pass();
	uint8_t s[6];
	for (int k = 0; k < 6; k++)
		s[k] = task_claim(S_J_STEP);
	CHECK(s[0] == T_JOB0 && s[4] == T_JOB4, "claims take the dynamic slots in order");
	CHECK(s[5] == SCHED_NONE, "a full table returns SCHED_NONE");
	CHECK(sched_task[T_A].state == S_A_STEP, "fixed slots untouched");
	CHECK(!strcmp(pass(), "jjjjj"), "each claimed job runs at its position");
	act[S_J_STEP].kind = A_END;
	sched_task[T_JOB2].wake = sched_now;
	CHECK(!strcmp(pass(), "j") && sched_task[T_JOB2].state == 0, "STEP_END frees the slot");
	CHECK(task_claim(S_J_STEP) == T_JOB2, "and it can be claimed again");

	reset_all(500);
	act[S_A_STEP].kind = A_CLAIM; act[S_A_STEP].a = S_J_STEP;
	act[S_J_STEP].kind = A_NEXT; act[S_J_STEP].a = S_J_TWO;
	act[S_J_TWO].kind = A_END;
	CHECK(!strcmp(pass(), "abawjj"), "claimed from a step: runs at its positions later in the same pass");
	CHECK(claimed == T_JOB1, "a claimed twice: the second claim got the next slot");
	act[S_A_STEP].kind = A_SLEEP; act[S_A_STEP].a = 60000;
	CHECK(!strcmp(pass(), "abwJJ"), "a returned state: the jobs go on in J_TWO");
	CHECK(sched_task[T_JOB0].state == 0 && sched_task[T_JOB1].state == 0, "and end");
}

static void test_next_and_overrun(void)
{
	printf("a returned state, measurement, overrun\n");
	reset_all(10);
	act[S_B_STEP].kind = A_NEXT; act[S_B_STEP].a = S_B_ALT;
	CHECK(!strcmp(pass(), "abaw"), "b runs B_STEP");
	CHECK(!strcmp(pass(), "aBaw"), "then B_ALT");

	reset_all(10);
	act[S_B_STEP].kind = A_BURN; act[S_B_STEP].a = 11;	/* 55 ms: over the 50 ms limit (decision 25) */
	pass();
	CHECK(sched_overruns == 1 && sched_overrun_state == S_B_STEP, "a 55 ms step is an overrun, its state named");
	CHECK(sched_st_max[S_B_STEP] >= 11 * 813 && sched_st_max[S_B_STEP] < 12 * 813, "its length in fine units");
	CHECK(sched_st_max[S_A_STEP] < SCHED_OVERRUN_FINE, "the others are not");
	CHECK(sched_pass_max >= sched_st_max[S_B_STEP], "the pass is at least as long");
	CHECK(sched_now == 21, "the tick is renewed after the step");

	reset_all(0);
	TH2 = 0x33; TL2 = 0x69;		/* just started: counting up from 0, below the reload */
	act[S_A_STEP].kind = A_TIMER; act[S_A_STEP].a = 0x356d;	/* and past it during the step */
	pass();
	CHECK(sched_st_max[S_A_STEP] < 100 && sched_pass_max < 100, "a count below the reload is the start of the tick, not a wrap");

	reset_all(10);
	act[S_B_STEP].kind = A_BURN; act[S_B_STEP].a = 200;	/* 1 s */
	pass();
	CHECK(sched_st_max[S_B_STEP] == 0xffff, "a step over ~400 ms saturates");
	sched_clear_stats();
	CHECK(sched_overruns == 0 && sched_st_max[S_B_STEP] == 0 && sched_passes_lo == 0, "clear");
}

static void test_step_macros(void)
{
	printf("step macros and the slot's arg\n");
	reset_all(300);
	int zero = 1;
	for (int i = 0; i < SCHED_SLOTS; i++)
		zero &= sched_task[i].arg == 0;
	CHECK(zero, "every slot starts with arg 0");

	act[S_B_STEP].kind = M_NEXT; act[S_B_STEP].a = S_B_ALT;
	CHECK(!strcmp(pass(), "abaw"), "return S_X: the step ends");
	CHECK(sched_task[T_B].state == S_B_ALT && sched_task[T_B].wake == 300 && sched_task[T_B].mask == 0,
	      "return S_X: the next state, and it yields");

	reset_all(300);
	act[S_B_STEP].kind = M_SLEEP; act[S_B_STEP].a = S_B_ALT; act[S_B_STEP].b = 20;
	pass();
	CHECK(sched_task[T_B].state == S_B_ALT && sched_task[T_B].wake == 300 + 5, "STEP_SLEEP: next state, wake in ceil(20/5)+1 ticks");

	reset_all(300);
	act[S_B_STEP].kind = M_WAIT; act[S_B_STEP].a = S_B_ALT; act[S_B_STEP].b = HW_LINK; act[S_B_STEP].c = SCHED_FOREVER;
	pass();
	CHECK(sched_task[T_B].state == S_B_ALT && sched_task[T_B].mask == (HW_LINK | EV_FOREVER)
	      && sched_task[T_B].wake == (uint16_t)(300 + SCHED_MAX_WAIT), "STEP_WAIT: next state, events, no timeout");

	reset_all(300);
	act[S_B_STEP].kind = M_ARG; act[S_B_STEP].a = S_B_ALT; act[S_B_STEP].b = 0xbeef;
	pass();
	CHECK(sched_task[T_B].state == S_B_ALT && sched_task[T_B].arg == 0xbeef, "STEP_ARG: next state and arg");

	reset_all(300);
	act[S_B_STEP].kind = M_END;
	pass();
	CHECK(sched_task[T_B].state == 0, "STEP_END: the slot is free");

	reset_all(300);
	act[S_A_STEP].kind = A_ARGINC;
	pass();
	pass();
	CHECK(sched_task[T_A].arg == 4 && seen_arg == 3, "arg is kept across steps (a runs twice a pass: 4 after two passes)");

	reset_all(300);
	CHECK(task_claim_arg(S_J_STEP, 1234) == T_JOB0 && sched_task[T_JOB0].arg == 1234, "task_claim_arg: the job starts with its arg");
	CHECK(task_claim(S_J_STEP) == T_JOB1 && sched_task[T_JOB1].arg == 0, "a plain claim after it starts with 0");
	act[S_J_STEP].kind = M_END;
	sched_task[T_JOB0].arg = 999;		/* as the job's own steps would leave it */
	pass();
	CHECK(sched_task[T_JOB0].state == 0, "the job ends");
	CHECK(task_claim(S_J_STEP) == T_JOB0 && sched_task[T_JOB0].arg == 0, "a freed slot claimed again starts with arg 0");
	act[S_A_STEP].kind = A_CLAIMARG; act[S_A_STEP].a = S_J_TWO; act[S_A_STEP].b = 77;
	act[S_J_STEP].kind = A_NONE;
	pass();
	CHECK(claimed == T_JOB2 && sched_task[T_JOB2].arg == 77 && sched_task[T_JOB2].state == S_J_TWO, "task_claim_arg from inside a step");
	CHECK(sched_claim_arg == 0, "the claim consumed the arg");
}

static void test_idle(void)
{
	printf("idle\n");
	reset_all(50);
	pass();
	PCON = 0;
	sched_idle();
	CHECK(PCON & 1, "nothing pending: sleep");
	CHECK(EA, "interrupts on again");
	PCON = 0; link_irq = 1;
	sched_idle();
	CHECK(!(PCON & 1), "link_irq pending: no sleep");
	link_irq = 0; ticks++;
	sched_idle();
	CHECK(!(PCON & 1), "a tick since the pass: no sleep");
	CHECK(sched_idle_lo == 3, "idle passes counted");
}

static void test_rings(void)
{
	printf("console rings\n");
	con_ring_init();
	CHECK(sizeof(con_rx_buf) == 256, "the RX ring is 256 B");
	for (int k = 0; k < 262; k++)
		CON_ISR_RX((uint8_t)k);
	CHECK(con_rx_overruns == 7, "256-byte RX ring keeps 255, counts 7 overruns");
	int ok = 1;
	for (int k = 0; k < 255; k++)
		ok &= con_rx_ready() && con_getc() == (uint8_t)k;
	CHECK(ok && !con_rx_ready(), "RX bytes come out in order, then empty");
	ok = 1;
	for (int round = 0; round < 10; round++) {	/* 2000 bytes: the 8-bit indexes wrap 7 times */
		for (int k = 0; k < 200; k++)
			CON_ISR_RX((uint8_t)(k * 3 + round));
		for (int k = 0; k < 200; k++)
			ok &= con_rx_ready() && con_getc() == (uint8_t)(k * 3 + round);
	}
	CHECK(ok && !con_rx_ready() && con_rx_overruns == 7, "across the wraps: in order, no overrun, then empty");
	for (int k = 0; k < 300; k++)
		CON_ISR_RX('z');
	CHECK(con_rx_overruns == 7 + 45, "full again at any index: 255 kept, the rest counted");

	CHECK(sizeof(con_tx_buf) == 2048, "the TX ring is 2 KiB (decision 21)");
	CHECK(con_tx_room() == CON_TX_SIZE - 1, "empty TX ring: 2047 free");
	con_putc('H');
	CHECK(SBUF == 'H' && con_tx_busy, "the first byte starts the transmitter");
	for (int k = 0; k < 40; k++)
		con_putc('a' + k % 26);
	CHECK(con_tx_room() == CON_TX_SIZE - 1 - 40, "40 queued");
	CHECK(ES, "serial interrupt back on");
	reset_all(1); con_putc('H');
	for (int k = 0; k < CON_TX_SIZE - 1 - 99; k++) con_putc('x');
	pass();
	CHECK(con_tx_room() == 99 && !(sched_ev & HW_CON_TX), "under 100 free: no HW_CON_TX");
	int sent = 0;
	while (con_tx_busy && sent < 3000) { CON_ISR_TX(); sent++; }
	CHECK(sent == CON_TX_SIZE - 99 && con_tx_room() == CON_TX_SIZE - 1,
	      "TI drains the ring, then the transmitter goes idle");
	pass();
	CHECK(sched_ev & HW_CON_TX, "room for a line: HW_CON_TX");

	/* E1: 16-bit indexes, across the wrap and full */
	ok = 1;
	int out = 0;
	for (int round = 0; round < 7; round++) {	/* 7 x 1500 B: the indexes wrap 5 times */
		for (int k = 0; k < 1500; k++) {
			con_putc((uint8_t)(k + round));
			if (k == 0)
				ok &= SBUF == (uint8_t)round;	/* the idle transmitter takes the first */
		}
		out = 1;
		while (con_tx_busy) {
			CON_ISR_TX();
			if (con_tx_busy) {
				ok &= SBUF == (uint8_t)(out + round);
				out++;
			}
		}
		ok &= out == 1500;
	}
	CHECK(ok && con_tx_room() == CON_TX_SIZE - 1, "across the wraps: every byte out, in order");
	con_putc('F');
	for (int k = 0; k < CON_TX_SIZE - 1; k++) con_putc('f');
	CHECK(con_tx_room() == 0, "full: 2047 queued behind the one sending");
	ES = 0; TI = 1;				/* the ISR can't run: con_putc does its part */
	con_putc('g');
	CHECK(con_tx_room() == 0 && SBUF == 'f' && con_tx_buf[(con_tx_head - 1) & CON_TX_MASK] == 'g',
	      "a byte into a full ring waits for one to go, polling TI with ES off");
	ES = 1;
	while (con_tx_busy) CON_ISR_TX();
}

static void fake_isr_timer2(void) { ticks++; SWDT_ISR(5); }

union om_st om_st;
#define R	om_st.sys_reset

static void test_crash(void)
{
	size_t n;

	printf("watchdog and crash record\n");
	memset((void *)&crash_rec, 0xa5, sizeof(crash_rec));	/* power-on garbage */
	out_reset();
	crash_boot();
	CHECK(crash_prior == 0 && out_len == 0, "garbage at boot: ignored, nothing printed");

	reset_all(7000);
	act[S_A_STEP].kind = A_SLEEP; act[S_A_STEP].a = 60000;
	act[S_B_STEP].kind = A_SLEEP; act[S_B_STEP].a = 60000;
	pass();					/* a, b, w run: w is the last noted */
	swdt_on = 0;
	swdt_kick();
	for (int k = 0; k < 2000; k++) fake_isr_timer2();
	CHECK(resets == 0, "off until the first pass");
	ticks = 7000;
	swdt_on = 1;
	swdt_kick();
	host_idata[0x60 - 5] = 0x12;		/* PC high, under the ISR's 5 pushes */
	host_idata[0x60 - 6] = 0x34;
	volatile int k = 0;
	if (!setjmp(reset_jmp)) {
		for (k = 0; k < 2000; k++) fake_isr_timer2();
	}
	CHECK(resets == 1 && k == SWDT_TICKS - 1, "no kick for 1000 ticks (5 s): reset on the 1000th");
	CHECK(crash_rec.magic == CRASH_MAGIC && crash_rec.cause == CRASH_SWDT, "record: magic, cause");
	CHECK(crash_rec.sum == crash_sum(), "record: sum");
	CHECK(crash_rec.slot == T_W && crash_rec.state == S_W_STEP, "record: the last slot and state noted");
	CHECK(crash_rec.pc == 0x1234 && crash_rec.sp == 0x60 - 5 - 2 && crash_rec.bank == 2, "record: PC from the ISR's stack, SP, bank");
	CHECK(crash_rec.tick == (uint16_t)(7000 + SWDT_TICKS) && crash_rec.step_tick == 7000, "record: ticks");
	CHECK(crash_rec.ring[(crash_rec.ring_idx - 1) & 7] == S_W_STEP && crash_rec.ring[(crash_rec.ring_idx - 2) & 7] == S_B_STEP, "record: the last states, newest last");
	CHECK(sizeof(struct crash_rec) == 32, "the record is 32 bytes");
	CHECK(crash_rec.image_lo == CRASH_IMAGE_LO && crash_rec.image_hi == CRASH_IMAGE_HI, "record: this image's identity");

	out_reset();
	crash_boot();
	CHECK(out_len == 0 && crash_last.magic == CRASH_MAGIC, "boot keeps it, printing nothing (state.sys.reset shows it)");
	CHECK(crash_rec.magic != CRASH_MAGIC && crash_prior == 1, "and consumes it; one recorded reset before this boot");
	om_get_sys_reset(0);
	CHECK(R.cause == CRASH_SWDT && !strcmp(R.slot, "w") && !strcmp(R.state, "W_STEP") &&
	      R.step_ms == 5000 && R.pc == 0x1234 && R.bank == 2,
	      "state.sys.reset: the record kept, as get prints it");
	n = strlen(R.states);
	CHECK(n > 14 && !strcmp(R.states + n - 13, "B_STEP W_STEP") && R.states[0] != ' ',
	      "state.sys.reset.states: eight, oldest first");
	out_reset();
	crash_boot();
	CHECK(out_len == 0, "a second boot prints nothing");
	om_get_sys_reset(0);
	CHECK(R.cause == E_RCAUSE_POWER && !R.slot[0] && !R.states[0], "no record: cause power, the rest empty");

	crash_prior = 1;
	if (!setjmp(reset_jmp))
		crash_reset(CRASH_RESET_CMD);
	CHECK(resets == 2 && crash_rec.cause == CRASH_RESET_CMD && crash_rec.pc == 0 && crash_rec.resets == 1, "the reset command: no PC, counts the earlier reset");
	crash_rec.sum ^= 1;
	out_reset();
	crash_boot();
	CHECK(out_len == 0 && crash_prior == 0, "a bad sum is garbage");

	/* E0: a valid record another image left (schedtest's, an older build's) */
	crash_prior = 0;
	if (!setjmp(reset_jmp))
		crash_reset(CRASH_RESET_CMD);
	crash_rec.image_hi ^= 0x0100;		/* another build */
	crash_rec.sum = crash_sum();		/* still a valid record */
	out_reset();
	crash_boot();
	CHECK(out_len == 0 && crash_prior == 0 && crash_last.magic != CRASH_MAGIC,
	      "a record from another image: ignored, nothing printed or kept");
	CHECK(crash_rec.magic != CRASH_MAGIC, "and dropped");

	/* an update's record: the image it boots isn't the one that wrote it */
	if (!setjmp(reset_jmp))
		crash_reset(CRASH_UPDATE);
	crash_rec.image_hi ^= 0x0100;
	crash_rec.sum = crash_sum();
	out_reset();
	crash_boot();
	CHECK(crash_last.magic == CRASH_MAGIC && crash_last.cause == CRASH_UPDATE && crash_last.slot == 0xff,
	      "an update's record from another image: kept, without its slot and states");
	om_get_sys_reset(0);
	CHECK(R.cause == E_RCAUSE_UPDATE && !strcmp(R.slot, "-") && !strcmp(R.state, "-") && R.pc == 0 &&
	      !strcmp(R.states, "- - - - - - - -") && R.step_ms == 0 && R.bank == 0,
	      "state.sys.reset: cause update; no slot, states, step time, PC or bank");
	CHECK(crash_rec.slot == 0xff && crash_rec.state == 0 && crash_rec.ring_idx == 0 && crash_rec.step_tick == 0,
	      "after the boot the live notes are blank: a reset before the scheduler records no step");
}

static void test_health(void)
{
	int n;

	printf("health tasks\n");
	reset_all(100);
	act[S_B_STEP].kind = A_BURN; act[S_B_STEP].a = 11;	/* 55 ms: an overrun (decision 25) */
	act[S_W_STEP].kind = A_WAIT; act[S_W_STEP].a = HW_LINK; act[S_W_STEP].b = SCHED_FOREVER;
	pass();
	out_reset();
	sched_health_tasks();
	CHECK(strstr(out, "slot\ttask\tstate\tmask\twake\n0\ta\tA_STEP\t0x0000\tnow\n") != NULL, "a slot line");
	CHECK(strstr(out, "2\tw\tW_STEP\t0x0082\t+32767\n") != NULL, "a waiting slot: mask and horizon");
	CHECK(strstr(out, "1\tb\tB_STEP\t0x0000\tnow\n") != NULL, "a slot that is due");
	CHECK(strstr(out, "7\tjob4\tfree\n") != NULL, "a free job slot");
	CHECK(strstr(out, "passes 0x000000001 idle 0x000000000") == NULL && strstr(out, "passes 0x00000001 idle 0x00000000") != NULL, "32-bit counts as one hex number");
	CHECK(strstr(out, "B_STEP\t1\t") != NULL && strstr(out, "overruns 1 last B_STEP") != NULL, "per-state runs, the overrun");
	CHECK(strstr(out, "A_STEP\t2\t0us") != NULL, "a short step in us");
	CHECK(strstr(out, "last reset: reset command in") == NULL, "no reset record after a bad sum");

	/* state.sched: the getters the object model prints */
	om_get_sched_slot(T_W);
	CHECK(om_st.sched_slot.state == E_SSTATE_W_STEP && om_st.sched_slot.task == E_STASK_W &&
	      om_st.sched_slot.mask == (HW_LINK | EV_FOREVER) && om_st.sched_slot.wake > 30000,
	      "state.sched.slot.w: task, state, mask, ticks to its horizon");
	om_get_sched_slot(T_A);
	CHECK(om_st.sched_slot.state == E_SSTATE_A_STEP && om_st.sched_slot.wake == 0, "state.sched.slot.a: due, wake 0");
	om_get_sched_slot(T_JOB4);
	CHECK(om_st.sched_slot.state == E_SSTATE_NONE && om_st.sched_slot.task == E_STASK_NONE &&
	      om_st.sched_slot.mask == 0, "state.sched.slot.job4: free, none");
	om_get_sched_state(S_B_STEP - 1);
	CHECK(om_st.sched_state.runs == 1 && om_st.sched_state.max_us > 50000 &&
	      om_st.sched_state.max_us <= sched_st_max[S_B_STEP] * 6.144 + 3 &&
	      om_st.sched_state.max_us + 1 >= sched_st_max[S_B_STEP] * 6.144,
	      "state.sched.state.B_STEP: runs, max in us");
	om_get_sched_task(E_STASK_B - 1);
	CHECK(om_st.sched_task.max_us == om_st.sched_state.max_us && om_st.sched_task.max_us > 50000,
	      "state.sched.task.b: its state's longest");
	om_get_sched(0);
	CHECK(om_st.sched.passes == 1 && om_st.sched.idle == 0 && om_st.sched.overruns == 1 &&
	      om_st.sched.overrun_last == E_SSTATE_B_STEP && om_st.sched.slots == SCHED_SLOTS &&
	      om_st.sched.fixed == T_FIXED, "state.sched: passes, idle, the overrun");
	n = 0;
	for (uint32_t f = 0; f <= 0xffff; f++) {	/* max_us: x 6.144, 3 us at the top, under 1 to 7281 */
		sched_st_max[S_A_STEP] = f;
		om_get_sched_state(S_A_STEP - 1);
		double us = f * 6.144;
		n += om_st.sched_state.max_us > us + (f <= 7281 ? 1 : 3) || om_st.sched_state.max_us + 1 < us;
	}
	CHECK(n == 0, "max_us: fine x 6.144, within 1 us to 7281 units (44.7 ms), 3 us above");
	hw_timeouts = 3;
	hw_timeout_reg = 0x1234;
	om_do_sched_clear(0);
	CHECK(hw_timeouts == 0 && hw_timeout_reg == 0 && sched_overruns == 0 && sched_st_max[S_B_STEP] == 0,
	      "do sched.clear: the stats and the hw timeouts");
	sched_health_clear();
	out_reset();
	sched_health_tasks();
	CHECK(strstr(out, "overruns 0") != NULL, "health clear");
	printf("%s", out);
}

int main(void)
{
	test_init_and_yield();
	test_sleep();
	test_wrap();
	test_events();
	test_claim();
	test_next_and_overrun();
	test_step_macros();
	test_idle();
	test_rings();
	test_crash();
	test_health();
	printf("\n%d checks, %d failed\n", tests_run, tests_failed);
	return tests_failed ? 1 : 0;
}
