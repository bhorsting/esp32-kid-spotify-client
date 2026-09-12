#include "ui/ui.h"
#include "ui/screens/screens.h"
#include "spotify/player.h"
#include "board_pins.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const ui_theme_t s_theme = {
    .bg = LV_COLOR_MAKE(0x1A, 0x24, 0x32),
    .surface = LV_COLOR_MAKE(0x2A, 0x3A, 0x4E),
    .accent = LV_COLOR_MAKE(0xFF, 0x8A, 0x4C),
    .accent_dim = LV_COLOR_MAKE(0xC4, 0x5E, 0x2C),
    .text = LV_COLOR_MAKE(0xF5, 0xF0, 0xE8),
    .text_muted = LV_COLOR_MAKE(0xA8, 0xB4, 0xC4),
    .danger = LV_COLOR_MAKE(0xE8, 0x5D, 0x5D),
    .like = LV_COLOR_MAKE(0xFF, 0x6B, 0x8A),
};

static lv_obj_t *s_screens[APP_SCREEN_COUNT];
static lv_obj_t *s_nav;
static app_screen_t s_current = APP_SCREEN_NOW_PLAYING;

const ui_theme_t *ui_theme(void)
{
    return &s_theme;
}

void ui_apply_theme(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, s_theme.bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

static void style_nav_btn(lv_obj_t *btn, bool active)
{
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_bg_color(btn, active ? s_theme.accent : s_theme.surface, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_t *lab = lv_obj_get_child(btn, 0);
    if (lab) {
        lv_obj_set_style_text_color(lab, active ? s_theme.bg : s_theme.text_muted, 0);
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);
    }
}

static void on_nav(lv_event_t *e)
{
    app_screen_t screen = (app_screen_t)(uintptr_t)lv_event_get_user_data(e);
    ui_show_screen(screen);
}

static void build_nav(lv_obj_t *parent)
{
    static const char *labels[] = {"Play", "Lists", "♥", "Find", "⚙"};
    s_nav = lv_obj_create(parent);
    lv_obj_set_size(s_nav, 300, 44);
    lv_obj_align(s_nav, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(s_nav, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_nav, 0, 0);
    lv_obj_set_style_pad_all(s_nav, 0, 0);
    lv_obj_set_flex_flow(s_nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_nav, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_nav, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < APP_SCREEN_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(s_nav);
        lv_obj_set_size(btn, 52, 40);
        lv_obj_add_event_cb(btn, on_nav, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *lab = lv_label_create(btn);
        lv_label_set_text(lab, labels[i]);
        lv_obj_center(lab);
        style_nav_btn(btn, i == (int)s_current);
    }
}

static void refresh_nav(void)
{
    if (!s_nav) {
        return;
    }
    for (int i = 0; i < APP_SCREEN_COUNT; i++) {
        lv_obj_t *btn = lv_obj_get_child(s_nav, i);
        if (btn) {
            style_nav_btn(btn, i == (int)s_current);
        }
    }
}

static void on_player_state(const player_state_t *state, void *user)
{
    (void)state;
    (void)user;
    if (s_current == APP_SCREEN_NOW_PLAYING) {
        screen_now_playing_refresh();
    }
}

void ui_init(void)
{
    ui_apply_theme();

    lv_obj_t *root = lv_scr_act();
    lv_obj_set_size(root, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* Circular mask hint — dark ring outside safe area via full bg */
    lv_obj_t *content = lv_obj_create(root);
    lv_obj_set_size(content, DISPLAY_SAFE_DIAMETER, DISPLAY_SAFE_DIAMETER - 50);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    s_screens[APP_SCREEN_NOW_PLAYING] = screen_now_playing_create(content);
    s_screens[APP_SCREEN_BROWSE] = screen_browse_create(content);
    s_screens[APP_SCREEN_FAVOURITES] = screen_favourites_create(content);
    s_screens[APP_SCREEN_SEARCH] = screen_search_create(content);
    s_screens[APP_SCREEN_SETTINGS] = screen_settings_create(content);

    for (int i = 0; i < APP_SCREEN_COUNT; i++) {
        lv_obj_add_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
    }

    build_nav(root);
    player_set_state_callback(on_player_state, NULL);
    ui_show_screen(APP_SCREEN_NOW_PLAYING);
}

void ui_show_screen(app_screen_t screen)
{
    if (screen >= APP_SCREEN_COUNT) {
        return;
    }
    for (int i = 0; i < APP_SCREEN_COUNT; i++) {
        if (i == (int)screen) {
            lv_obj_clear_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    s_current = screen;
    refresh_nav();
    if (screen == APP_SCREEN_NOW_PLAYING) {
        screen_now_playing_refresh();
    }
}

app_screen_t ui_current_screen(void)
{
    return s_current;
}
