#include "ui/screens/screens.h"
#include "ui/ui.h"
#include "ui/widgets/kid_keyboard.h"
#include "spotify/player.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t *results;
    kid_keyboard_t *kb;
    char ids[8][64];
} search_ctx_t;

static search_ctx_t s_search;

static void on_result(lv_event_t *e)
{
    const char *id = (const char *)lv_event_get_user_data(e);
    player_play_track(id);
    ui_show_screen(APP_SCREEN_NOW_PLAYING);
}

static void show_results(const spotify_track_t *results, size_t count, void *user)
{
    search_ctx_t *ctx = (search_ctx_t *)user;
    const ui_theme_t *t = ui_theme();
    lv_obj_clean(ctx->results);

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(ctx->results);
        lv_label_set_text(empty, "No songs yet — keep typing");
        lv_obj_set_style_text_color(empty, t->text_muted, 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_12, 0);
        return;
    }

    size_t n = count > 8 ? 8 : count;
    for (size_t i = 0; i < n; i++) {
        strncpy(ctx->ids[i], results[i].id, sizeof(ctx->ids[i]) - 1);
        ctx->ids[i][sizeof(ctx->ids[i]) - 1] = '\0';

        lv_obj_t *btn = lv_btn_create(ctx->results);
        lv_obj_set_size(btn, 300, 36);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_bg_color(btn, t->surface, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_hor(btn, 8, 0);
        lv_obj_add_event_cb(btn, on_result, LV_EVENT_CLICKED, ctx->ids[i]);

        lv_obj_t *lab = lv_label_create(btn);
        char line[128];
        snprintf(line, sizeof(line), "%s", results[i].title);
        lv_label_set_text(lab, line);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(lab, 280);
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lab, t->text, 0);
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

static void on_change(const char *text, void *user)
{
    search_ctx_t *ctx = (search_ctx_t *)user;
    if (!text || strlen(text) < SEARCH_MIN_CHARS) {
        lv_obj_clean(ctx->results);
        const ui_theme_t *t = ui_theme();
        lv_obj_t *hint = lv_label_create(ctx->results);
        lv_label_set_text(hint, "Type 2+ letters");
        lv_obj_set_style_text_color(hint, t->text_muted, 0);
        return;
    }
    player_search(text, show_results, ctx);
}

static void on_submit(const char *text, void *user)
{
    on_change(text, user);
}

lv_obj_t *screen_search_create(lv_obj_t *parent)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Find a song");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, t->accent, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    s_search.results = lv_obj_create(scr);
    lv_obj_set_size(s_search.results, 310, 88);
    lv_obj_align(s_search.results, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_bg_opa(s_search.results, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_search.results, 0, 0);
    lv_obj_set_style_pad_row(s_search.results, 4, 0);
    lv_obj_set_style_pad_all(s_search.results, 0, 0);
    lv_obj_set_flex_flow(s_search.results, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_search.results, LV_DIR_VER);

    lv_obj_t *hint = lv_label_create(s_search.results);
    lv_label_set_text(hint, "Type 2+ letters");
    lv_obj_set_style_text_color(hint, t->text_muted, 0);

    s_search.kb = kid_keyboard_create(scr);
    if (s_search.kb) {
        lv_obj_align(s_search.kb->root, LV_ALIGN_BOTTOM_MID, 0, 0);
        kid_keyboard_set_callbacks(s_search.kb, on_change, on_submit, &s_search);
    }

    return scr;
}
