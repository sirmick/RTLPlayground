/*
 * test_update_apply.c - run the real check_and_flash_update_image() against the
 * modelled flash device, for whichever flash layout it is built with.
 *
 * Built twice: standard (needs >=1 MiB) and -DLAYOUT_COMPACT (fits 512 KiB).
 * Covers: the too-small refusal; a valid image copied over the running one with
 * the config preserved and the staging area cleared, on the minimum flash and
 * a larger one, WITHOUT the clean-up wrapping past the end of the chip; and a
 * power cut during the copy that a re-run completes from the intact staged
 * image. With the standard layout a real built .bin can be passed as arg 1.
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

	/* a real built .bin is a standard-layout image */
	if (argc > 1 && IMAGE_SIZE == 0x80000) {
		FILE *f = fopen(argv[1], "rb");
		if (!f) { perror(argv[1]); return 2; }
		static uint8_t real[0x80000];
		size_t n = fread(real, 1, sizeof real, f); fclose(f);
		if (n != sizeof real) { fprintf(stderr, "%s: short read\n", argv[1]); return 2; }
		CHECK(crc16_host_buf(real, 0x80000) == 0xB001, "real image: CRC == 0xB001 (matches crc_calculator)");
		run_valid_cycle(0x200000, "2M real image", real);
	}

	printf(failures ? "\ntest_update_apply: %d FAILURE(S)\n" : "\ntest_update_apply: ALL PASS\n", failures);
	return failures ? 1 : 0;
}
