#include "ui/screens/screens.h"
#include "ui/ui.h"
#include "spotify/player.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t *list;
} fav_ctx_t;

static fav_ctx_t s_fav;

static void on_track(lv_event_t *e)
{
    const char *id = (const char *)lv_event_get_user_data(e);
    player_play_track(id);
    ui_show_screen(APP_SCREEN_NOW_PLAYING);
}

static void fill_list(const spotify_track_t *results, size_t count, void *user)
{
    fav_ctx_t *ctx = (fav_ctx_t *)user;
    const ui_theme_t *t = ui_theme();
    lv_obj_clean(ctx->list);

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(ctx->list);
        lv_label_set_text(empty, "Heart songs you like!");
        lv_obj_set_style_text_color(empty, t->text_muted, 0);
        return;
    }

    for (size_t i = 0; i < count; i++) {
        lv_obj_t *btn = lv_btn_create(ctx->list);
        lv_obj_set_size(btn, 280, 44);
        lv_obj_set_style_radius(btn, 14, 0);
        lv_obj_set_style_bg_color(btn, t->surface, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);

        /* Copy id onto heap-ish static buffer per row — use track id from result;
         * stub returns stack copies; store in user_data via strdup-like static pool */
        static char ids[8][64];
        if (i < 8) {
            strncpy(ids[i], results[i].id, sizeof(ids[i]) - 1);
            lv_obj_add_event_cb(btn, on_track, LV_EVENT_CLICKED, ids[i]);
        }

        lv_obj_t *lab = lv_label_create(btn);
        char line[128];
        snprintf(line, sizeof(line), "%s — %s", results[i].title, results[i].artist);
        lv_label_set_text(lab, line);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(lab, 250);
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lab, t->text, 0);
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 10, 0);
    }
}

static void on_show(lv_event_t *e)
{
    (void)e;
    player_get_favourites(fill_list, &s_fav);
}

lv_obj_t *screen_favourites_create(lv_obj_t *parent)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 4, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_show, LV_EVENT_DRAW_MAIN_BEGIN, NULL);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Favourites");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, t->like, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    s_fav.list = lv_obj_create(scr);
    lv_obj_set_size(s_fav.list, 300, 220);
    lv_obj_align(s_fav.list, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_bg_opa(s_fav.list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_fav.list, 0, 0);
    lv_obj_set_style_pad_row(s_fav.list, 6, 0);
    lv_obj_set_flex_flow(s_fav.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_fav.list, LV_DIR_VER);

    player_get_favourites(fill_list, &s_fav);
    return scr;
}
