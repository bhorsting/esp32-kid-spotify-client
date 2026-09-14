#pragma once

#include "app_config.h"
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);
void ui_show_screen(app_screen_t screen);
app_screen_t ui_current_screen(void);

/** Safe from any task — marks UI dirty; applied on the LVGL/ui task. */
void ui_request_refresh(void);
/** Call from LVGL/ui task only. */
void ui_poll_refresh(void);

typedef struct {
    lv_color_t bg;
    lv_color_t surface;
    lv_color_t accent;
    lv_color_t accent_dim;
    lv_color_t text;
    lv_color_t text_muted;
    lv_color_t danger;
    lv_color_t like;
} ui_theme_t;

const ui_theme_t *ui_theme(void);
void ui_apply_theme(void);

#ifdef __cplusplus
}
#endif
