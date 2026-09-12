#include "ui/screens/screens.h"
#include "ui/ui.h"
#include "spotify/player.h"

#include <stdio.h>

static lv_obj_t *s_title;
static lv_obj_t *s_artist;
static lv_obj_t *s_art;
static lv_obj_t *s_play_lab;
static lv_obj_t *s_like_lab;

static void on_prev(lv_event_t *e)
{
    (void)e;
    player_previous();
}

static void on_play(lv_event_t *e)
{
    (void)e;
    player_play_pause();
}

static void on_next(lv_event_t *e)
{
    (void)e;
    player_next();
}

static void on_like(lv_event_t *e)
{
    (void)e;
    player_toggle_like();
}

static lv_obj_t *big_btn(lv_obj_t *parent, const char *txt, lv_event_cb_t cb, lv_color_t bg, int w)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, 56);
    lv_obj_set_style_radius(btn, 28, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, txt);
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(lab, t->text, 0);
    lv_obj_center(lab);
    return btn;
}

lv_obj_t *screen_now_playing_create(lv_obj_t *parent)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 8, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_art = lv_obj_create(scr);
    lv_obj_set_size(s_art, 110, 110);
    lv_obj_align(s_art, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_radius(s_art, 20, 0);
    lv_obj_set_style_bg_color(s_art, t->surface, 0);
    lv_obj_set_style_border_width(s_art, 0, 0);
    lv_obj_clear_flag(s_art, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *note = lv_label_create(s_art);
    lv_label_set_text(note, "♪");
    lv_obj_set_style_text_font(note, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(note, t->accent, 0);
    lv_obj_center(note);

    s_title = lv_label_create(scr);
    lv_obj_set_width(s_title, 280);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 124);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_title, t->text, 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL_CIRCULAR);

    s_artist = lv_label_create(scr);
    lv_obj_set_width(s_artist, 280);
    lv_obj_align(s_artist, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_style_text_align(s_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_artist, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_artist, t->text_muted, 0);
    lv_label_set_long_mode(s_artist, LV_LABEL_LONG_CLIP);

    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_set_size(row, 300, 64);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    big_btn(row, "⏮", on_prev, t->surface, 56);
    lv_obj_t *play = big_btn(row, "▶", on_play, t->accent, 72);
    s_play_lab = lv_obj_get_child(play, 0);
    lv_obj_set_style_text_color(s_play_lab, t->bg, 0);
    big_btn(row, "⏭", on_next, t->surface, 56);
    lv_obj_t *like = big_btn(row, "♡", on_like, t->surface, 56);
    s_like_lab = lv_obj_get_child(like, 0);

    screen_now_playing_refresh();
    return scr;
}

void screen_now_playing_refresh(void)
{
    const player_state_t *st = player_get_state();
    if (!st || !s_title) {
        return;
    }
    if (st->has_track) {
        lv_label_set_text(s_title, st->track.title);
        lv_label_set_text(s_artist, st->track.artist);
        lv_label_set_text(s_like_lab, st->track.liked ? "♥" : "♡");
        lv_obj_set_style_text_color(s_like_lab, st->track.liked ? ui_theme()->like : ui_theme()->text, 0);
    } else {
        lv_label_set_text(s_title, "Nothing playing");
        lv_label_set_text(s_artist, "Pick a song");
    }
    lv_label_set_text(s_play_lab, st->playing ? "⏸" : "▶");
}
