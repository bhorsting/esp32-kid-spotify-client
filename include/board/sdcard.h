#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Mount onboard TF card (FAT) at /sdcard. Safe to call once after EXIO init. */
bool board_sd_init(void);
bool board_sd_is_mounted(void);

/** Cover cache helpers — RGB565 disc images keyed by URL hash. */
bool board_sd_cover_load(const char *url, uint8_t **pixels, int *w, int *h);
bool board_sd_cover_save(const char *url, const uint8_t *pixels, int w, int h);

#ifdef __cplusplus
}
#endif
