#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Decoded RGB565 cover suitable for lv_img. Owned by cover_art cache. */
typedef struct {
    lv_img_dsc_t dsc;
    uint8_t *pixels; /* SPIRAM */
    char url[192];
    bool ready;
} cover_art_t;

void cover_art_init(void);

/**
 * Request art for url into slot (0=center, 1..5=ring).
 * Returns cached pointer immediately if ready; otherwise starts async fetch
 * and returns NULL (call cover_art_poll / wait for callback).
 */
cover_art_t *cover_art_request(int slot, const char *url);

/** Non-blocking: process finished downloads and invoke on_ready for UI refresh. */
typedef void (*cover_art_ready_cb_t)(int slot, cover_art_t *art, void *user);
void cover_art_set_ready_callback(cover_art_ready_cb_t cb, void *user);
void cover_art_poll(void);

void cover_art_clear_slot(int slot);

#ifdef __cplusplus
}
#endif
