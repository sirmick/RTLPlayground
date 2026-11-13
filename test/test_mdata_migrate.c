/*
 * test_mdata_migrate.c - machine data (layout.h, MDATA_*): the region check and
 * the one-time move of a bridge image's transfer copy to MDATA_START, with the
 * real mdata_len()/mdata_migrate() against the modelled flash. Built compact.
 *
 * Covers: a 2 MiB chip moves it once and the transfer copy is spent; a 512 KiB
 * chip (no region: its address would wrap onto the start of the flash) never
 * writes anything; a corrupt copy is ignored; a damaged region is rejected; and
 * a power cut after every flash operation of the move settles to the region
 * moved exactly once.
 */
#include <stdio.h>
#include <string.h>
#include "rtl837x_common.h"
#include "flash_dev.h"
#include "update_shim.h"
#include "update_apply.h"

extern uint32_t flash_size;
static int failures;
#define CHECK(cond, msg) do { \
	if (cond) printf("PASS  %s\n", msg); \
	else { printf("FAIL  %s\n", msg); failures++; } \
} while (0)

static uint8_t md[29000];		/* a region the size of the PoE images */
static uint32_t md_len;

static void make_md(void)
{
	uint32_t n = sizeof md - MDATA_HDR_LEN - 2;
	memcpy(md, "MDT1", 4);
	md[4] = n; md[5] = n >> 8; md[6] = n >> 16; md[7] = n >> 24;
	for (uint32_t i = 0; i < n; i++)
		md[MDATA_HDR_LEN + i] = (uint8_t)((i * 2654435761u) >> 24);
	uint16_t c = crc16_host_buf(md, sizeof md - 2) ^ 0xffff;
	md[sizeof md - 2] = c; md[sizeof md - 1] = c >> 8;
	md_len = sizeof md;
}

/* a chip as a bridge image leaves it: the transfer copy at MDATA_XFER_START */
static void bridged(uint32_t size)
{
	fd_init(&g_flash, size);
	flash_size = 0;			/* the boot hasn't read the chip id yet */
	fd_load(&g_flash, MDATA_XFER_START, md, md_len);
}

static int region_ok(void)  { return !memcmp(g_flash.mem + MDATA_START, md, md_len); }
static int xfer_spent(void) { return g_flash.mem[MDATA_XFER_START] == 0xff; }

int main(void)
{
	make_md();
	CHECK(crc16_host_buf(md, md_len) == 0xb001, "the test region carries a good CRC");

	bridged(0x200000);
	CHECK(mdata_len(MDATA_XFER_START) == md_len, "the transfer copy is a valid region");
	CHECK(mdata_migrate() == CONFIG_MOVED && region_ok() && xfer_spent(), "a 2 MiB chip moves it to MDATA_START");
	CHECK(mdata_len(MDATA_START) == md_len, "the moved region is valid");
	CHECK(mdata_migrate() == CONFIG_STAYED && region_ok(), "the next boot does nothing");
	g_flash.mem[MDATA_START + 1000] ^= 0x01;
	CHECK(mdata_len(MDATA_START) == 0, "a damaged region is rejected (PoE stays off)");
	fd_free(&g_flash);

	bridged(0x80000);
	CHECK(mdata_migrate() == CONFIG_STAYED && g_flash.writes == 0 && g_flash.erases == 0,
	      "a 512 KiB chip has no region and writes nothing");
	CHECK(mdata_len(MDATA_START) == 0, "nor reads one through the address wrap");
	fd_free(&g_flash);

	bridged(0x200000);
	g_flash.mem[MDATA_XFER_START + 5000] ^= 0x80;
	CHECK(mdata_migrate() == CONFIG_STAYED && g_flash.writes == 0 && g_flash.mem[MDATA_START] == 0xff,
	      "a corrupt transfer copy is ignored");
	fd_free(&g_flash);

	bridged(0x200000);			/* moved, but the final erase was cut */
	fd_load(&g_flash, MDATA_START, md, md_len);
	CHECK(mdata_migrate() == CONFIG_MOVED && region_ok() && xfer_spent(), "an interrupted move is finished");
	fd_free(&g_flash);

	int all_ok = 1;
	for (int n = 0; n < 90; n++) {
		bridged(0x200000);
		fd_fail_after(&g_flash, n);
		int r = migrate_mdata_run();
		fd_fail_after(&g_flash, -1);
		while (r == -1 || r == CONFIG_MOVE_FAILED)
			r = migrate_mdata_run();
		for (int k = 0; k < 3; k++)
			migrate_mdata_run();
		if (!(region_ok() && xfer_spent() && mdata_len(MDATA_START) == md_len)) {
			printf("  cut after %d ops: not settled\n", n);
			all_ok = 0;
		}
		fd_free(&g_flash);
	}
	CHECK(all_ok, "a power cut at any point of the move settles to the region moved once");

	printf(failures ? "\ntest_mdata_migrate: %d FAILURE(S)\n" : "\ntest_mdata_migrate: ALL PASS\n", failures);
	return failures ? 1 : 0;
}
