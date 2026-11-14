#ifndef _LAYOUT_H_
#define _LAYOUT_H_

#include <stdint.h>

/*
 * Flash layout - the one place it is defined, for the firmware and the loader.
 * The firmware and loader include this; the Makefile (layout_val) and
 * tools/layout.py read the same values through the C preprocessor, so nothing
 * else needs to be kept in sync. Plain numbers for the values the Makefile
 * reads (it evaluates them with shell arithmetic).
 *
 * Standard (512 KiB image; a web update stages a second copy, so needs >=1 MiB):
 *   0x00000 code, 0x40000 web UI, 0x6f000 default config, 0x70000 saved config,
 *   0x7fffe CRC; an upload is staged at 0x80000. With the loader (layout 1) the
 *   image keeps 0x3a000-0x40000 free for the loader's data.
 * Compact, -DLAYOUT_COMPACT (232 KiB image; web updates fit a 512 KiB flash,
 * with or without the loader - loader layout 2):
 *   0x00000 code (<=3 banks; with the loader, the loader is here and the app's
 *   home bank sits in the slot), 0x28000 web UI, 0x39000 default config,
 *   0x39ffe CRC; 0x3a000 the loader's data; an upload (web or XMODEM) is
 *   staged at 0x40000, a loader update at 0x7a000; the saved config is at
 *   0x7f000.
 */
#ifdef LAYOUT_COMPACT
#define IMAGE_SIZE           0x3a000
#define HTML_START           0x28000
#define DEFAULT_CONFIG_START 0x39000
#define UPLOAD_START         0x40000
#define CONFIG_START         0x7f000
#define LDR_STAGE            0x7a000UL
#define LDR_APP_B            0x3a000UL	/* no second application range */
#define LDR_APP_B_END        0x3a000UL
#define LDR_LAYOUT           2
#else
#define IMAGE_SIZE           0x80000
#define HTML_START           0x40000
#define DEFAULT_CONFIG_START 0x6f000
#define UPLOAD_START         0x80000
#define CONFIG_START         0x70000
#define LDR_STAGE            0x71000UL
#define LDR_APP_B            0x40000UL
#define LDR_APP_B_END        0x70000UL
#define LDR_LAYOUT           1
#endif
#define CONFIG_LEN 0x1000
/* Machine data (compact layout, chips of 1 MiB or more): a region no update
 * ever writes, for a board's own blobs - the POE-2G040210GSM's PoE controller
 * firmware. "MDT1", the payload length (uint32, little endian), the payload,
 * and a CRC-16 that makes the whole come to 0xb001. Written once, separately;
 * a bridge image from the standard layout carries a copy at MDATA_XFER_START
 * (inside what the old firmware copies, and inside the new staging area),
 * which the first boot moves here. On a 512 KiB chip there is no region: its
 * address would wrap onto the start of the flash. */
#define MDATA_START          0x80000
#define MDATA_LEN            0xc000
#define MDATA_XFER_START     0x60000
#define MDATA_HDR_LEN        8

/* Where the saved config lived in the standard layout. A compact firmware
 * moves a config found there to CONFIG_START once, at its first boot. */
#define LEGACY_CONFIG_START  0x70000
#define CODE0_SIZE 0x4000
#define CODE_BANK_SIZE 0xc000

/* The loader (optional, #852): it occupies the first CODE0_SIZE bytes, keeps
 * its boot counter, metadata and a copy of the application's home bank in a
 * reserved area, installs an image staged at UPLOAD_START, and is itself
 * updated through LDR_STAGE. */
#define LDR_LOADER_LEN       0x4000
#define LDR_DATA             0x3a000UL
#define LDR_DATA_END         0x40000UL
#define LDR_BOOTCNT          0x3a000UL
#define LDR_META             0x3b000UL
#define LDR_SLOT             0x3c000UL
#define LDR_HOME_LEN         0x3ffe
#define LDR_APP_A            0x04000UL
#define LDR_APP_A_END        LDR_DATA
#define LDR_UPLOAD           ((uint32_t)UPLOAD_START)
#define LDR_IMAGE_LEN        ((uint32_t)IMAGE_SIZE)

// A web upload is staged here; the loader installs from the same place.
#define FIRMWARE_UPLOAD_START UPLOAD_START
#define FIRMWARE_UPLOAD_END   (UPLOAD_START + IMAGE_SIZE)

// Bytes copied from a staged image over the running one: the whole image,
// unless the saved config sits inside it (standard), which the copy stops short of.
#if CONFIG_START < IMAGE_SIZE
#define IMAGE_COPY_LEN CONFIG_START
#else
#define IMAGE_COPY_LEN IMAGE_SIZE
#endif

// Progress of a loaderless install: "UPD1" once the staged image has verified,
// then one byte per copied sector, cleared to 0 when that sector is done. A
// sector the copy never touches, so a reset mid-copy resumes from it at boot.
#define UPDATE_PROGRESS       0x7e000UL

// Smallest flash that holds the running image, a staged upload and the config.
#if CONFIG_START + CONFIG_LEN > FIRMWARE_UPLOAD_END
#define FLASH_NEEDED_FOR_UPDATE (CONFIG_START + CONFIG_LEN)
#else
#define FLASH_NEEDED_FOR_UPDATE FIRMWARE_UPLOAD_END
#endif

#if (IMAGE_SIZE % 0x1000) || (CONFIG_START % 0x1000) || (DEFAULT_CONFIG_START % 0x1000) || (UPLOAD_START % 0x1000)
#error "flash layout: image, upload and config sectors must be 4 KiB aligned"
#endif
#if IMAGE_COPY_LEN % 0x1000
#error "flash layout: the copied part of an update must be whole sectors"
#endif
#if !(HTML_START < DEFAULT_CONFIG_START && DEFAULT_CONFIG_START < IMAGE_SIZE)
#error "flash layout: the web UI, then the default config, must lie inside the image"
#endif
#if UPLOAD_START < IMAGE_SIZE
#error "flash layout: the upload must be staged past the running image"
#endif
#if CONFIG_START >= UPLOAD_START && CONFIG_START < FIRMWARE_UPLOAD_END
#error "flash layout: the saved config must not lie in the upload staging area"
#endif
#if LDR_LAYOUT == 2 && (IMAGE_SIZE > 0x3a000 || UPLOAD_START < 0x40000)
#error "flash layout: a compact image must leave the loader's data area free"
#endif
#if !(LDR_STAGE + LDR_LOADER_LEN <= UPLOAD_START || LDR_STAGE >= FIRMWARE_UPLOAD_END)
#error "flash layout: the loader update must not lie in the upload staging area"
#endif
/* A compact install image is the application with the loader after it: its
 * upload stages the loader where /loader would (boot.c loader_convert()). */
#if defined(LAYOUT_COMPACT) && LDR_STAGE != FIRMWARE_UPLOAD_END
#error "flash layout: the loader stage must follow the upload staging area"
#endif
#if defined(LAYOUT_COMPACT) && CONFIG_START != 0x7f000
#error "flash layout: the saved config stays at 0x7f000 - switches moved there once must never move again"
#endif
#if defined(LAYOUT_COMPACT) && (MDATA_XFER_START < UPLOAD_START || MDATA_XFER_START + MDATA_LEN > LEGACY_CONFIG_START)
#error "flash layout: the machine data transfer copy must lie in the staging area and below the old config"
#endif
#if defined(LAYOUT_COMPACT) && (MDATA_START % 0x1000 || MDATA_START < 0x80000)
#error "flash layout: machine data lies past the first 512 KiB, sector aligned"
#endif
#if UPDATE_PROGRESS < IMAGE_COPY_LEN || UPDATE_PROGRESS + 0x1000 > 0x80000 || \
    (UPDATE_PROGRESS >= UPLOAD_START && UPDATE_PROGRESS < FIRMWARE_UPLOAD_END) || \
    UPDATE_PROGRESS == CONFIG_START || \
    (UPDATE_PROGRESS >= LDR_STAGE && UPDATE_PROGRESS < LDR_STAGE + LDR_LOADER_LEN) || \
    4 + IMAGE_COPY_LEN / 0x1000 > 0x1000
#error "flash layout: the update progress record needs a sector of its own in the first 512 KiB"
#endif
#if LDR_STAGE < CONFIG_START + CONFIG_LEN && LDR_STAGE + LDR_LOADER_LEN > CONFIG_START
#error "flash layout: the loader update must not overlap the saved config"
#endif

#endif
