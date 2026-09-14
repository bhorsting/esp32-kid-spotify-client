#include "ui/ui.h"
#include "ui/screens/screens.h"
#include "ui/cover_art.h"
#include "spotify/player.h"
#include "board_pins.h"

#include <stdint.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "ui";

static ui_theme_t s_theme = {
    .bg = LV_COLOR_MAKE(0x0B, 0x3D, 0x6E),
    .surface = LV_COLOR_MAKE(0x14, 0x4A, 0x78),
    .accent = LV_COLOR_MAKE(0xFF, 0x8A, 0x4C),
    .accent_dim = LV_COLOR_MAKE(0xC4, 0x5E, 0x2C),
    .text = LV_COLOR_MAKE(0xF5, 0xF0, 0xE8),
    .text_muted = LV_COLOR_MAKE(0xB8, 0xD0, 0xE8),
    .danger = LV_COLOR_MAKE(0xE8, 0x5D, 0x5D),
    .like = LV_COLOR_MAKE(0xFF, 0x6B, 0x8A),
};

static lv_obj_t *s_screens[APP_SCREEN_COUNT];
static app_screen_t s_current = APP_SCREEN_RING;
static volatile bool s_ui_dirty;

const ui_theme_t *ui_theme(void)
{
    return &s_theme;
}

void ui_request_refresh(void)
{
    s_ui_dirty = true;
}

void ui_poll_refresh(void)
{
    cover_art_poll();
    if (s_current == APP_SCREEN_RING) {
        screen_ring_poll();
    }
    if (!s_ui_dirty) {
        return;
    }
    s_ui_dirty = false;
    if (s_current == APP_SCREEN_RING && s_screens[APP_SCREEN_RING]) {
        screen_ring_refresh();
    }
}

void ui_apply_theme(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, s_theme.bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, 0);
}

static lv_obj_t *ensure_screen(app_screen_t screen)
{
    if (s_screens[screen]) {
        return s_screens[screen];
    }
    switch (screen) {
    case APP_SCREEN_RING:
        s_screens[screen] = screen_ring_create(lv_scr_act());
        break;
    case APP_SCREEN_SETTINGS:
        s_screens[screen] = screen_settings_create(lv_scr_act());
        break;
    default:
        return NULL;
    }
    if (s_screens[screen]) {
        lv_obj_add_flag(s_screens[screen], LV_OBJ_FLAG_HIDDEN);
    } else {
        ESP_LOGE(TAG, "screen %d create failed", (int)screen);
    }
    return s_screens[screen];
}

static void on_player_state(const player_state_t *state, void *user)
{
    (void)state;
    (void)user;
    ui_request_refresh();
}

void ui_init(void)
{
    ESP_LOGI(TAG, "ui_init start (free SPIRAM=%u internal=%u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cover_art_init();
    ui_apply_theme();

    lv_obj_t *root = lv_scr_act();
    lv_obj_set_size(root, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_bg_color(root, s_theme.bg, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    ensure_screen(APP_SCREEN_RING);
    player_set_state_callback(on_player_state, NULL);
    ui_show_screen(APP_SCREEN_RING);
    ESP_LOGI(TAG, "ui_init done (free SPIRAM=%u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void ui_show_screen(app_screen_t screen)
{
    if (screen >= APP_SCREEN_COUNT) {
        return;
    }
    ensure_screen(screen);
    for (int i = 0; i < APP_SCREEN_COUNT; i++) {
        if (!s_screens[i]) {
            continue;
        }
        if (i == (int)screen) {
            lv_obj_clear_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    /* Title overlay is a sibling of the ring root — keep it in sync. */
    screen_ring_set_visible(screen == APP_SCREEN_RING);
    s_current = screen;
    if (screen == APP_SCREEN_RING) {
        screen_ring_refresh();
    } else if (screen == APP_SCREEN_SETTINGS) {
        screen_settings_on_show();
    }
}

app_screen_t ui_current_screen(void)
{
    return s_current;
}
