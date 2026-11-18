/*
 * st_report.c - what schedtest/silicon.py needs on silicon, where there are
 * no simulator peeks (C). The report itself is silicon.py's: it reads the
 * objects the simulator's tests read, by their .map addresses, through
 * `peek`, and decodes them the same way. Banked; place_banks.py picks the
 * bank.
 *
 *   peek A N .. N bytes of xdata from A (decimal), as `x <hex>`, a line
 *               each pair; read 64 at a time with the interrupts off, so an
 *               object of up to 64 bytes is read whole, and all of a line's
 *               pairs in one step: a snapshot
 *   clear       health clear, and the timestamp logs start again (but the
 *               poster's: the waiter's latency is counted from it)
 *   flashtime   a sector erase, a 4-byte and a 256-byte program in the
 *               upload area's second sector: st_ft holds the fine clock
 *               (tick, units) before and after each, st_ft_bad the bytes
 *               that read back wrong
 *   erasepeek   flash_peek: 64 bytes of the bank window read during a sector
 *               erase of the upload area's third sector, then the same 64
 *               once it is done (0x4000 + 0x300 i, bank st_pk_bank);
 *               st_ft[0..3]: the clock around the erase
 *
 * flashtime and erasepeek are long CLI_RUN steps (an overrun each).
 */
#include <8051.h>
#include <stdint.h>
#include "rtl837x_common.h"
#include "rtl837x_sfr.h"
#include "layout.h"
#include "rtl837x_flash.h"
#include "sched.h"
#include "schedtest/schedtest.h"

extern __xdata struct flash_region_t flash_region;

__xdata uint16_t st_pk_addr, st_pk_len;
__xdata uint16_t st_ft[12], st_ft_bad;
__xdata uint8_t st_pk_bank;
static __xdata uint8_t pk_buf[64];
static __xdata uint8_t ri, rn;

void st_peek(void) __banked
{
	print_string("x ");
	while (st_pk_len) {
		rn = st_pk_len > 64 ? 64 : st_pk_len;
		EA = 0;
		for (ri = 0; ri < rn; ri++)
			pk_buf[ri] = ((__xdata uint8_t *)st_pk_addr)[ri];
		EA = 1;
		for (ri = 0; ri < rn; ri++)
			print_byte(pk_buf[ri]);
		st_pk_addr += rn;
		st_pk_len -= rn;
	}
	write_char('\n');
}

void st_clear(void) __banked
{
	sched_health_clear();
	st_log_sleep_a.n = 0;
	st_log_sleep_b.n = 0;
	st_log_sleep_c.n = 0;
	st_log_waiter.n = 0;
	st_log_isr_ev.n = 0;
	st_log_hot.n = 0;
}

/* ---- the flash probes ---- */

#define FT_SECTOR ((uint32_t)UPLOAD_START + 0x1000)
static __xdata uint8_t ft_buf[256];
static __xdata uint16_t fn;

static void ft_stamp(void)		/* the clock into st_ft[ri], [ri + 1] */
{
	sched_fine_read();
	st_ft[ri++] = sched_f_tick;
	st_ft[ri++] = sched_f_cnt;
}

static void ft_check(void)		/* fn bytes against the pattern */
{
	flash_region.len = fn;
	flash_read_bulk(ft_buf);
	while (fn--)
		if (ft_buf[fn] != (uint8_t)(fn ^ 0x5a))
			st_ft_bad++;
}

void st_flashtime(void) __banked
{
	ri = 0;
	do {
		ft_buf[ri] = ri ^ 0x5a;
	} while (++ri);
	flash_region.addr = FT_SECTOR;
	ft_stamp();
	flash_sector_erase();
	ft_stamp();
	flash_region.addr = FT_SECTOR;
	flash_region.len = 4;
	ft_stamp();
	flash_write_bytes(ft_buf);
	ft_stamp();
	flash_region.addr = FT_SECTOR + 0x100;
	flash_region.len = 256;
	ft_stamp();
	flash_write_bytes(ft_buf);
	ft_stamp();
	st_ft_bad = 0;
	flash_region.addr = FT_SECTOR;
	fn = 4;
	ft_check();
	flash_region.addr = FT_SECTOR + 0x100;
	fn = 256;
	ft_check();
}

void st_erasepeek(void) __banked
{
	st_pk_bank = PSBANK;		/* the window flash_erase_peek reads: ours */
	flash_region.addr = (uint32_t)UPLOAD_START + 0x2000;
	ri = 0;
	ft_stamp();
	flash_erase_peek();
	ft_stamp();
	fn = 0x4000;			/* idle now: the same addresses again */
	for (ri = 64; ri < 128; ri++) {
		flash_peek[ri] = *(__code const uint8_t *)fn;
		fn += 0x300;
	}
}
