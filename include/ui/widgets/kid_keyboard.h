#pragma once

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*kid_kb_change_cb_t)(const char *text, void *user);
typedef void (*kid_kb_submit_cb_t)(const char *text, void *user);

/**
 * Large-key, paged alphabet keyboard for 360×360 round displays.
 * Pages: A–I | J–R | S–Z | 0–9
 */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *field;
    lv_obj_t *keys[9];
    lv_obj_t *page_btn;
    lv_obj_t *space_btn;
    lv_obj_t *back_btn;
    lv_obj_t *go_btn;
    char buffer[64];
    int page; /* 0..3 */
    kid_kb_change_cb_t on_change;
    kid_kb_submit_cb_t on_submit;
    void *user;
} kid_keyboard_t;

kid_keyboard_t *kid_keyboard_create(lv_obj_t *parent);
void kid_keyboard_set_callbacks(kid_keyboard_t *kb, kid_kb_change_cb_t on_change,
                                kid_kb_submit_cb_t on_submit, void *user);
void kid_keyboard_set_text(kid_keyboard_t *kb, const char *text);
const char *kid_keyboard_get_text(const kid_keyboard_t *kb);
void kid_keyboard_clear(kid_keyboard_t *kb);

#ifdef __cplusplus
}
#endif
