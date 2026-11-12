/*
 * test_config_migrate.c - the one-time move of the saved config from the
 * standard layout's sector (0x70000) to the compact one (0x7f000), run with the
 * real config_migrate() against the modelled 512 KiB flash. Built compact.
 *
 * Covers: a config is moved and the old sector erased; staging leftovers that
 * aren't config text are left alone; a later factory reset can't bring the old
 * config back; an interrupted clean-up is finished; a half-copied config is
 * redone from the old sector; and a power cut at every single flash operation
 * of the move (with a config spanning several writes) settles, over the
 * following boots, to the config moved exactly once.
 */
#include <stdio.h>
#include <string.h>
#include "rtl837x_common.h"
#include "flash_dev.h"
#include "update_shim.h"
#include "update_apply.h"

static int failures;
#define CHECK(cond, msg) do { \
	if (cond) printf("PASS  %s\n", msg); \
	else { printf("FAIL  %s\n", msg); failures++; } \
} while (0)

static char cfg[1500];	/* longer than one 512-byte write, so a cut can leave it half-copied */

static void make_cfg(void)
{
	size_t n = 0;
	n += snprintf(cfg + n, sizeof cfg - n, "ip 192.168.10.241\ngw 192.168.10.1\nhostname switch-denlounge\n");
	for (int v = 2; n < sizeof cfg - 40; v++)
		n += snprintf(cfg + n, sizeof cfg - n, "vlan %d 1t 2t 3t 9t\n", v);
}

static int sector_is(uint32_t a, const char *text)
{
	return !memcmp(g_flash.mem + a, text, strlen(text)) && g_flash.mem[a + strlen(text)] != text[0]
	       && (g_flash.mem[a + strlen(text)] == 0xff || g_flash.mem[a + strlen(text)] == 0x00);
}

static int sector_blank(uint32_t a)
{
	for (uint32_t i = 0; i < CONFIG_LEN; i++)
		if (g_flash.mem[a + i] != 0xff) return 0;
	return 1;
}

/* a 512 KiB chip as the old firmware leaves it after installing the bridge
 * image: its config at 0x70000, the new sector erased */
static void old_switch(void)
{
	fd_init(&g_flash, 0x80000);
	fd_load(&g_flash, LEGACY_CONFIG_START, (const uint8_t *)cfg, sizeof cfg);	/* incl. NUL */
}

int main(void)
{
	make_cfg();
	old_switch();
	CHECK(strlen(cfg) > 2 * FLASH_BUF_SIZE, "the test config spans several flash writes");
	CHECK(migrate_run() == CONFIG_MOVED, "an old config is moved");
	CHECK(sector_is(CONFIG_START, cfg), "it is at 0x7f000, unchanged");
	CHECK(sector_blank(LEGACY_CONFIG_START), "the old sector is erased");
	CHECK(migrate_run() == CONFIG_STAYED && sector_is(CONFIG_START, cfg), "the next boot does nothing");

	/* factory reset afterwards: the config sector erased, nothing to resurrect */
	fd_erase_sector(&g_flash, CONFIG_START);
	CHECK(migrate_run() == CONFIG_STAYED && sector_blank(CONFIG_START), "a factory reset doesn't bring the old config back");
	fd_free(&g_flash);

	/* staging leftovers at 0x70000 (a gzipped web UI) are not a config */
	fd_init(&g_flash, 0x80000);
	{ static const uint8_t gz[] = { 0x1f, 0x8b, 0x08, 0x00, 'i', 'p', ' ' };
	  fd_load(&g_flash, LEGACY_CONFIG_START, gz, sizeof gz); }
	CHECK(migrate_run() == CONFIG_STAYED && sector_blank(CONFIG_START) && g_flash.writes == 0 && g_flash.erases == 0,
	      "binary data at the old address is left alone");
	/* text without an end inside the sector is not one either */
	{ static uint8_t t[CONFIG_LEN]; memset(t, 'a', sizeof t);
	  fd_load(&g_flash, LEGACY_CONFIG_START, t, sizeof t); }
	CHECK(migrate_run() == CONFIG_STAYED && sector_blank(CONFIG_START), "text that doesn't end within the sector is left alone");
	fd_free(&g_flash);

	/* clean-up cut short earlier: new and old identical -> old erased */
	old_switch();
	fd_load(&g_flash, CONFIG_START, (const uint8_t *)cfg, sizeof cfg);
	CHECK(migrate_run() != CONFIG_MOVE_FAILED && sector_blank(LEGACY_CONFIG_START) && sector_is(CONFIG_START, cfg),
	      "an interrupted clean-up is finished");
	fd_free(&g_flash);

	/* a half-copied config (cut mid-copy): redone from the old sector */
	old_switch();
	fd_load(&g_flash, CONFIG_START, (const uint8_t *)cfg, FLASH_BUF_SIZE);
	CHECK(migrate_run() == CONFIG_MOVED && sector_is(CONFIG_START, cfg) && sector_blank(LEGACY_CONFIG_START),
	      "a half-copied config is redone from the old sector");
	fd_free(&g_flash);

	/* the standard layout's own config, on a 512K-image switch, is the same
	 * address - covered by the compact build only; here a power cut at every
	 * erase/write of the move, then boots until it settles */
	int worst = 0, all_ok = 1;
	for (int n = 0; n < 40; n++) {
		old_switch();
		fd_fail_after(&g_flash, n);
		int r = migrate_run(), boots = 1;
		fd_fail_after(&g_flash, -1);
		while (r == -1 || r == CONFIG_MOVE_FAILED) { r = migrate_run(); boots++; }
		for (int k = 0; k < 3; k++, boots++)	/* a few more boots: must stay settled */
			migrate_run();
		int ok = sector_is(CONFIG_START, cfg) && sector_blank(LEGACY_CONFIG_START);
		if (!ok) { printf("  cut after %d ops: not settled\n", n); all_ok = 0; }
		if (boots > worst) worst = boots;
		fd_free(&g_flash);
	}
	CHECK(all_ok, "a power cut at any point of the move settles to the config moved once");

	printf(failures ? "\ntest_config_migrate: %d FAILURE(S)\n" : "\ntest_config_migrate: ALL PASS\n", failures);
	return failures ? 1 : 0;
}
