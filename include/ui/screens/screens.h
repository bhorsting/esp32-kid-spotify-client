#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *screen_ring_create(lv_obj_t *parent);
void screen_ring_refresh(void);
/** Rebuild deferred pie cache if dirty (call from UI poll). */
void screen_ring_poll(void);
void screen_ring_set_visible(bool visible);

lv_obj_t *screen_settings_create(lv_obj_t *parent);
void screen_settings_on_show(void);

#ifdef __cplusplus
}
#endif
