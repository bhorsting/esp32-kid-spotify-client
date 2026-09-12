#include "ui/screens/screens.h"
#include "ui/ui.h"
#include "spotify/player.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_vol_lab;
static lv_obj_t *s_status;
static char s_pin_buf[8];
static bool s_unlocked;

static void refresh_vol(void)
{
    const player_state_t *st = player_get_state();
    char buf[32];
    snprintf(buf, sizeof(buf), "Volume %u%%", (unsigned)st->volume);
    lv_label_set_text(s_vol_lab, buf);
}

static void on_vol_down(lv_event_t *e)
{
    (void)e;
    const player_state_t *st = player_get_state();
    uint8_t v = st->volume > 5 ? st->volume - 5 : 0;
    player_set_volume(v);
    refresh_vol();
}

static void on_vol_up(lv_event_t *e)
{
    (void)e;
    const player_state_t *st = player_get_state();
    uint8_t v = st->volume + 5;
    player_set_volume(v);
    refresh_vol();
}

static void on_digit(lv_event_t *e)
{
    const char *d = (const char *)lv_event_get_user_data(e);
    size_t len = strlen(s_pin_buf);
    if (len < 4) {
        s_pin_buf[len] = d[0];
        s_pin_buf[len + 1] = '\0';
    }
    if (strlen(s_pin_buf) == 4) {
        if (strcmp(s_pin_buf, PARENT_PIN_DEFAULT) == 0) {
            s_unlocked = true;
            lv_label_set_text(s_status, "Parent OK — Wi‑Fi & Spotify here later");
        } else {
            lv_label_set_text(s_status, "Wrong PIN");
        }
        s_pin_buf[0] = '\0';
    } else {
        char dots[8] = {0};
        for (size_t i = 0; i < strlen(s_pin_buf); i++) {
            dots[i] = '•';
        }
        lv_label_set_text(s_status, dots[0] ? dots : "Parent PIN");
    }
}

lv_obj_t *screen_settings_create(lv_obj_t *parent)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 4, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, APP_NAME);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, t->text, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    s_vol_lab = lv_label_create(scr);
    lv_obj_set_style_text_color(s_vol_lab, t->text_muted, 0);
    lv_obj_align(s_vol_lab, LV_ALIGN_TOP_MID, 0, 32);
    refresh_vol();

    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_set_size(row, 200, 48);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *down = lv_btn_create(row);
    lv_obj_set_size(down, 72, 44);
    lv_obj_set_style_radius(down, 14, 0);
    lv_obj_set_style_bg_color(down, t->surface, 0);
    lv_obj_add_event_cb(down, on_vol_down, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(down);
    lv_label_set_text(dl, "−");
    lv_obj_center(dl);

    lv_obj_t *up = lv_btn_create(row);
    lv_obj_set_size(up, 72, 44);
    lv_obj_set_style_radius(up, 14, 0);
    lv_obj_set_style_bg_color(up, t->accent, 0);
    lv_obj_add_event_cb(up, on_vol_up, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ul = lv_label_create(up);
    lv_label_set_text(ul, "+");
    lv_obj_set_style_text_color(ul, t->bg, 0);
    lv_obj_center(ul);

    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "Parent PIN");
    lv_obj_set_style_text_color(s_status, t->text_muted, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 120);

    lv_obj_t *pad = lv_obj_create(scr);
    lv_obj_set_size(pad, 220, 100);
    lv_obj_align(pad, LV_ALIGN_TOP_MID, 0, 148);
    lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad, 0, 0);
    lv_obj_set_style_pad_all(pad, 0, 0);
    lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(pad, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE);

    static const char *digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
    for (int i = 0; i < 10; i++) {
        lv_obj_t *b = lv_btn_create(pad);
        lv_obj_set_size(b, 40, 36);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_bg_color(b, t->surface, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, on_digit, LV_EVENT_CLICKED, (void *)digits[i]);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, digits[i]);
        lv_obj_center(l);
    }

    (void)s_unlocked;
    return scr;
}
