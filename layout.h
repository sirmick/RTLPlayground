#ifndef _LAYOUT_H_
#define _LAYOUT_H_

/*
 * Flash layout - the one place it is defined. The firmware includes this; the
 * Makefile reads the same values through the C preprocessor (see layout_val),
 * so nothing else needs to be kept in sync. Plain numbers only (no UL suffix):
 * the Makefile evaluates them with shell arithmetic.
 *
 * Standard (512 KiB image; a web update stages a second copy, so needs >=1 MiB):
 *   0x00000 code, 0x40000 web UI, 0x6f000 default config, 0x70000 saved config,
 *   0x7fffe CRC; an upload is staged at 0x80000.
 * Compact, -DLAYOUT_COMPACT (252 KiB image; web updates fit a 512 KiB flash):
 *   0x00000 code (<=3 banks), 0x28000 web UI, 0x3e000 default config, 0x3effe
 *   CRC; an upload is staged at 0x3f000, the saved config is at 0x7f000.
 */
#ifdef LAYOUT_COMPACT
#define IMAGE_SIZE           0x3f000
#define HTML_START           0x28000
#define DEFAULT_CONFIG_START 0x3e000
#define CONFIG_START         0x7f000
#else
#define IMAGE_SIZE           0x80000
#define HTML_START           0x40000
#define DEFAULT_CONFIG_START 0x6f000
#define CONFIG_START         0x70000
#endif
#define CONFIG_LEN 0x1000
#define CODE0_SIZE 0x4000
#define CODE_BANK_SIZE 0xc000

// An upload is staged just past the running image.
#define FIRMWARE_UPLOAD_START IMAGE_SIZE
#define FIRMWARE_UPLOAD_END   (FIRMWARE_UPLOAD_START + IMAGE_SIZE)

// Bytes copied from a staged image over the running one: the whole image,
// unless the saved config sits inside it (standard), which the copy stops short of.
#if CONFIG_START < IMAGE_SIZE
#define IMAGE_COPY_LEN CONFIG_START
#else
#define IMAGE_COPY_LEN IMAGE_SIZE
#endif

// Smallest flash that holds the running image, a staged upload and the config.
#if CONFIG_START + CONFIG_LEN > FIRMWARE_UPLOAD_END
#define FLASH_NEEDED_FOR_UPDATE (CONFIG_START + CONFIG_LEN)
#else
#define FLASH_NEEDED_FOR_UPDATE FIRMWARE_UPLOAD_END
#endif

#if (IMAGE_SIZE % 0x1000) || (CONFIG_START % 0x1000) || (DEFAULT_CONFIG_START % 0x1000)
#error "flash layout: image and config sectors must be 4 KiB aligned"
#endif
#if IMAGE_COPY_LEN % 0x1000
#error "flash layout: the copied part of an update must be whole sectors"
#endif
#if !(HTML_START < DEFAULT_CONFIG_START && DEFAULT_CONFIG_START < IMAGE_SIZE)
#error "flash layout: the web UI, then the default config, must lie inside the image"
#endif
#if CONFIG_START >= IMAGE_SIZE && CONFIG_START < FIRMWARE_UPLOAD_END
#error "flash layout: the saved config must not lie in the upload staging area"
#endif

#endif
