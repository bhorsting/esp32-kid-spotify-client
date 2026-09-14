#include "ui/screens/screens.h"
#include "ui/ui.h"
#include "spotify/player.h"
#include "net/wifi_manager.h"
#include "spotify_bridge.h"
#include "board_pins.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_status;
static lv_obj_t *s_pl_lab;

static void refresh(void)
{
    if (!s_status) {
        return;
    }
    char line[160];
    wifi_status_t st = wifi_manager_get_status();
    char ssid[33] = {0};
    wifi_manager_get_ssid(ssid, sizeof(ssid));
    switch (st) {
    case WIFI_STATUS_CONNECTED:
        snprintf(line, sizeof(line), "WiFi: %s", ssid[0] ? ssid : "ok");
        break;
    case WIFI_STATUS_CONNECTING:
        snprintf(line, sizeof(line), "WiFi: connecting…");
        break;
    case WIFI_STATUS_AP_PORTAL:
        snprintf(line, sizeof(line), "AP: Marten-Setup");
        break;
    case WIFI_STATUS_FAILED:
        snprintf(line, sizeof(line), "WiFi failed");
        break;
    default:
        snprintf(line, sizeof(line), "WiFi: off");
        break;
    }
    if (spotify_connect_is_ready()) {
        strncat(line, "\nSpotify ready", sizeof(line) - strlen(line) - 1);
    } else if (st == WIFI_STATUS_CONNECTED) {
        strncat(line, "\nOpen Spotify → Marten Player", sizeof(line) - strlen(line) - 1);
    }
    lv_label_set_text(s_status, line);

    char pl[64] = {0};
    player_get_curated_playlist_id(pl, sizeof(pl));
    if (pl[0]) {
        char buf[96];
        snprintf(buf, sizeof(buf), "Playlist:\n%.40s", pl);
        lv_label_set_text(s_pl_lab, buf);
    } else {
        lv_label_set_text(s_pl_lab, "No playlist yet\nUse phone portal");
    }
}

static void on_back(lv_event_t *e)
{
    (void)e;
    ui_show_screen(APP_SCREEN_RING);
}

static void on_portal(lv_event_t *e)
{
    (void)e;
    wifi_manager_start_portal();
    lv_label_set_text(s_status, "Join Marten-Setup\nhttp://192.168.4.1");
}

static void on_reload(lv_event_t *e)
{
    (void)e;
    if (player_reload_curated()) {
        lv_label_set_text(s_status, "Playlist loaded");
        ui_show_screen(APP_SCREEN_RING);
    } else {
        lv_label_set_text(s_status, "Load failed\nPair Spotify + set playlist");
    }
    refresh();
}

static lv_obj_t *big_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb, lv_color_t bg)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 220, 48);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, txt);
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lab, t->text, 0);
    lv_obj_center(lab);
    return btn;
}

lv_obj_t *screen_settings_create(lv_obj_t *parent)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_set_size(scr, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_align(scr, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, t->accent, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 36);

    s_status = lv_label_create(scr);
    lv_obj_set_width(s_status, 240);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_status, t->text, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 68);

    s_pl_lab = lv_label_create(scr);
    lv_obj_set_width(s_pl_lab, 240);
    lv_obj_set_style_text_align(s_pl_lab, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_pl_lab, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_pl_lab, t->text_muted, 0);
    lv_obj_align(s_pl_lab, LV_ALIGN_TOP_MID, 0, 120);

    lv_obj_t *col = lv_obj_create(scr);
    lv_obj_set_size(col, 240, 170);
    lv_obj_align(col, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_row(col, 8, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    big_btn(col, "Phone setup portal", on_portal, t->surface);
    big_btn(col, "Reload playlist", on_reload, t->surface);
    big_btn(col, "Back to songs", on_back, t->accent);

    refresh();
    return scr;
}

void screen_settings_on_show(void)
{
    refresh();
}
