#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *screen_now_playing_create(lv_obj_t *parent);
void screen_now_playing_refresh(void);

lv_obj_t *screen_browse_create(lv_obj_t *parent);
lv_obj_t *screen_favourites_create(lv_obj_t *parent);
lv_obj_t *screen_search_create(lv_obj_t *parent);
lv_obj_t *screen_settings_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
