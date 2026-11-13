/*
 * test_update_apply.c - run the real check_and_flash_update_image() against the
 * modelled flash device, for whichever flash layout it is built with.
 *
 * Built twice: standard (needs >=1 MiB) and -DLAYOUT_COMPACT (fits 512 KiB).
 * Covers: the too-small refusal; a valid image copied over the running one with
 * the config preserved and the staging area cleared, on the minimum flash and
 * a larger one, WITHOUT the clean-up wrapping past the end of the chip; and a
 * power cut during the copy that a re-run completes from the intact staged
 * image; and a power cut at every single erase and write in turn, each
 * followed by power-ons until the switch is up (on the new image) or dead
 * (only while the home bank, copied last, is torn). A real built .bin of the
 * layout can be passed as arg 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtl837x_common.h"   /* IMAGE_SIZE, CONFIG_START, IMAGE_COPY_LEN, ... */
#include "flash_dev.h"
#include "update_shim.h"

static int failures;
#define CHECK(cond, msg) do { \
	if (cond) printf("PASS  %s\n", msg); \
	else { printf("FAIL  %s\n", msg); failures++; } \
} while (0)

/* Build an IMAGE_SIZE image: starts 00 40, arbitrary body, last two bytes set
 * so the CRC-16 over the whole image is 0xB001 - exactly as a real build ends. */
static void make_valid_image(uint8_t *img)
{
	memset(img, 0, IMAGE_SIZE);
	img[0] = 0x00; img[1] = 0x40;
	for (uint32_t i = 2; i < IMAGE_SIZE - 2; i++)
		img[i] = (uint8_t)((i * 2654435761u) >> 24);
	uint16_t s = crc16_host_buf(img, IMAGE_SIZE - 2);
	for (int b0 = 0; b0 < 256; b0++)
		for (int b1 = 0; b1 < 256; b1++)
			if (crc16_host_step(crc16_host_step(s, (uint8_t)b0), (uint8_t)b1) == 0xB001) {
				img[IMAGE_SIZE - 2] = (uint8_t)b0;
				img[IMAGE_SIZE - 1] = (uint8_t)b1;
				return;
			}
	fprintf(stderr, "make_valid_image: no CRC solution?!\n");
	exit(2);
}

static uint8_t running[IMAGE_COPY_LEN];

static void setup(flashdev_t *fd, uint32_t size, const uint8_t *img)
{
	memset(running, 0x11, sizeof running);
	fd_init(fd, size);
	fd_load(fd, 0, running, IMAGE_COPY_LEN);
	fd_load(fd, CONFIG_START, (const uint8_t *)"CONFIG-KEEP", 11);
	if (img)
		fd_load(fd, FIRMWARE_UPLOAD_START, img, IMAGE_SIZE);
}

static int copied_ok(flashdev_t *fd, const uint8_t *img)
{
	for (uint32_t a = 0; a < IMAGE_COPY_LEN; a++)
		if (fd->mem[a] != img[a]) return 0;
	return memcmp(fd->mem + CONFIG_START, "CONFIG-KEEP", 11) == 0;
}

static void run_valid_cycle(uint32_t size, const char *label, const uint8_t *img)
{
	char msg[96];
	setup(&g_flash, size, img);
	int reset = update_run();
	snprintf(msg, sizeof msg, "%s: applied, copied, config kept, staging cleared, no wrap/NOR fault", label);
	CHECK(reset && copied_ok(&g_flash, img)
	      && g_flash.mem[FIRMWARE_UPLOAD_START] == 0xFF
	      && g_flash.wrote_without_erase == 0 && g_flash.wrapped_ops == 0, msg);
	fd_free(&g_flash);
}

/* Is flash [from, to) the old image, the new one, or torn between them? */
enum { OLD, NEW, TORN };
static int image_state(const uint8_t *img, uint32_t from, uint32_t to)
{
	int old = 1, new = 1;
	for (uint32_t a = from; a < to; a++) {
		old &= g_flash.mem[a] == running[a];
		new &= g_flash.mem[a] == img[a];
	}
	return new ? NEW : old ? OLD : TORN;
}

/* One power-on, as far as the update is concerned. The resident code up to
 * update_resume() must be one whole home bank; past it, main() calls into the
 * other banks, so they must belong to the same image as the home bank.
 * Returns 1 if the switch comes up, 0 if it resets to try again, -1 dead. */
static int boot(const uint8_t *img)
{
	int home = image_state(img, 0, CODE0_SIZE);
	if (home == TORN)
		return -1;
	if (resume_run())
		return 0;
	if (home != image_state(img, CODE0_SIZE, IMAGE_COPY_LEN))
		return -1;
	return update_run() ? 0 : 1;
}

/* Cut the power at every erase and write of an update in turn, then power on
 * until the switch is up or dead. Only a cut that tears the home bank, the
 * last sectors copied, may leave it dead; every other one must come back up
 * on the new image, the config kept and the staging area and record cleared. */
static void powercut_sweep(uint32_t size, const char *label, const uint8_t *img)
{
	char msg[160];
	int ops, up = 0, dead = 0, bad = 0, first_dead = -1, last_dead = -1;

	setup(&g_flash, size, img);
	update_run();
	ops = g_flash.erases + g_flash.writes;
	fd_free(&g_flash);

	for (int n = 0; n < ops; n++) {
		int r = 0, boots = 0, torn_home;
		setup(&g_flash, size, img);
		fd_fail_after(&g_flash, n);
		update_run();
		fd_fail_after(&g_flash, -1);
		torn_home = image_state(img, 0, CODE0_SIZE) == TORN;
		while (!r && boots++ < 4)
			r = boot(img);
		if (r < 0) {
			dead++;
			if (first_dead < 0) first_dead = n;
			last_dead = n;
			bad += !torn_home;
		} else if (r == 1 && copied_ok(&g_flash, img)
			   && g_flash.mem[FIRMWARE_UPLOAD_START] == 0xFF
			   && g_flash.mem[UPDATE_PROGRESS] == 0xFF
			   && g_flash.wrote_without_erase == 0 && g_flash.wrapped_ops == 0) {
			up++;
		} else {
			bad++;
		}
		fd_free(&g_flash);
	}
	printf("      %s: %d cut points, %d come up on the new image, %d dead", label, ops, up, dead);
	if (dead)
		printf(" (cuts %d-%d)", first_dead, last_dead);
	printf("\n");
	snprintf(msg, sizeof msg, "%s: a power cut at any point boots, unless it tears the home bank", label);
	CHECK(bad == 0 && up + dead == ops && dead <= CODE0_SIZE / 0x1000 * (2 + 0x1000 / FLASH_BUF_SIZE), msg);
}

int main(int argc, char **argv)
{
	static uint8_t img[IMAGE_SIZE];
	uint32_t need = FLASH_NEEDED_FOR_UPDATE;   /* power of two: 1 MiB / 512 KiB */
	make_valid_image(img);

	printf("layout: IMAGE_SIZE=0x%x CONFIG_START=0x%x needs 0x%x flash\n",
	       IMAGE_SIZE, CONFIG_START, need);
	CHECK(crc16_host_buf(img, IMAGE_SIZE) == 0xB001, "host CRC over a valid image == 0xB001");

	/* too small: a device one power-of-two below the requirement refuses */
	setup(&g_flash, need / 2, img);
	int reset = update_run();
	CHECK(!reset && g_flash.writes == 0 && g_flash.erases == 0,
	      "below the flash requirement: update refused, flash untouched");
	fd_free(&g_flash);

	/* valid image applied on the minimum flash and on twice that */
	run_valid_cycle(need, "min flash", img);
	run_valid_cycle(need * 2, "2x flash", img);

	/* power cut partway through the copy; the staged image is intact */
	setup(&g_flash, need, img);
	fd_fail_after(&g_flash, 20);
	reset = update_run();
	CHECK(!reset && g_flash.powercut && !copied_ok(&g_flash, img),
	      "power cut mid-copy: not applied, copy incomplete");
	fd_fail_after(&g_flash, -1);
	reset = update_run();
	CHECK(reset && copied_ok(&g_flash, img),
	      "re-run after power cut completes from the intact staged image");
	fd_free(&g_flash);

	powercut_sweep(need, "power cuts", img);

	/* a real built .bin of this layout: standard on 2 MiB, compact on the
	 * 512 KiB it is meant for */
	if (argc > 1) {
		FILE *f = fopen(argv[1], "rb");
		if (!f) { perror(argv[1]); return 2; }
		static uint8_t real[IMAGE_SIZE + 1];
		size_t n = fread(real, 1, sizeof real, f); fclose(f);
		if (n != IMAGE_SIZE) { fprintf(stderr, "%s: %zu bytes, not 0x%x\n", argv[1], n, IMAGE_SIZE); return 2; }
		CHECK(crc16_host_buf(real, IMAGE_SIZE) == 0xB001, "real image: CRC == 0xB001 (matches crc_calculator)");
		run_valid_cycle(IMAGE_SIZE == 0x80000 ? 0x200000 : need,
				IMAGE_SIZE == 0x80000 ? "2M real image" : "512K real image", real);
		powercut_sweep(need, "real image, power cuts", real);
	}

	printf(failures ? "\ntest_update_apply: %d FAILURE(S)\n" : "\ntest_update_apply: ALL PASS\n", failures);
	return failures ? 1 : 0;
}
