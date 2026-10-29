/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared by the target flash driver and host NOR tests. No register access. */
#pragma once
#include <stdint.h>

#define FL_DATA_LO      0x00097000u                /* Felucca main store (the region; what is written in it: below) */
#define FL_DATA_HI      0x000E0000u
#define FL_PROJ_LO      0x00097000u                /* .. the four PROJECT slots, A/B each (storage.c) */
#define FL_PROJ_HI      0x0009F000u
#define FL_SONG_LO      0x000A0000u                /* .. WaveLoop: the set list's twelve songs, A/B each (storage.c OBJ_SONG0), */
#define FL_SONG_HI      0x000B8000u                /*    in the retired user sample slots */
#define FL_EXT_LO       0x000B8000u                /* FOR5/FOR6 project, autosave and setlist A/B extensions */
#define FL_EXT_HI       0x000DA000u                /* DA000..DBFFF remain excluded */
#define FL_UPRE_LO      0x000DC000u                /* .. the user preset banks (upreset.c) */
#define FL_UPRE_HI      0x000E0000u
#define FL_GLOB_LO      0x000FC000u                /* Felucca superblock / globals */
#define FL_GLOB_HI      0x000FF000u
#define FL_OTA_LO       0x000E0000u                /* M-UPGRADE loader staging, ota.c */
#define FL_OTA_HI       0x000E5000u
#define FL_AUTO_LO      0x000E5000u                /* 1.2: the autosave (storage.c OBJ_AUTOSAVE), A/B, just above */
#define FL_AUTO_HI      0x000E7000u                /* the OTA staging */
#define FL_VBANK_LO     0x000E7000u                /* 1.4.1: the FM6 voice bank (fm6_vbank.c), the sector above */
#define FL_VBANK_HI     0x000E8000u                /* (below the stock OTA's VM backup, 0xE8000: never written) */
/* [off, off + n) inside [lo, hi), without wrapping: off + n can overflow, and
 * the 1 MiB part ignores the high address bits, so a wrapped range lands low. */
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
/* Felucca's own store: what the app writes (the projects, WaveLoop's set list, the user presets; the settings; the
 * autosave and the retired FM6 voice bank: one range). The retired parts of FL_DATA (0x9F000, the user sample slots past
 * the extension range) are no longer writable; the OTA staging is FL_RANGE_OK's alone */
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_PROJ_LO, FL_PROJ_HI) || FL_IN(off, n, FL_SONG_LO, FL_SONG_HI) || \
                             FL_IN(off, n, FL_EXT_LO, FL_EXT_HI) || FL_IN(off, n, FL_UPRE_LO, FL_UPRE_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI) || \
                             FL_IN(off, n, FL_AUTO_LO, FL_VBANK_HI))
/* Where the RAM driver may erase / program. The app build allows only its own
 * data regions; the update loader (firmware/loader) defines its own window. */
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#endif

