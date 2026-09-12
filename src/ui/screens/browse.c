#include "ui/screens/screens.h"
#include "ui/ui.h"
#include "spotify/player.h"

#include <string.h>

static char s_playlist_ids[8][64];

static void on_playlist(lv_event_t *e)
{
    const char *id = (const char *)lv_event_get_user_data(e);
    player_play_playlist(id);
    ui_show_screen(APP_SCREEN_NOW_PLAYING);
}

lv_obj_t *screen_browse_create(lv_obj_t *parent)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_set_size(scr, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 4, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Playlists");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, t->text, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_set_size(list, 300, 220);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    spotify_playlist_t playlists[8];
    size_t count = 0;
    player_get_browse_playlists(playlists, 8, &count);

    for (size_t i = 0; i < count; i++) {
        strncpy(s_playlist_ids[i], playlists[i].id, sizeof(s_playlist_ids[i]) - 1);
        s_playlist_ids[i][sizeof(s_playlist_ids[i]) - 1] = '\0';

        lv_obj_t *btn = lv_btn_create(list);
        lv_obj_set_size(btn, 280, 48);
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_bg_color(btn, t->surface, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, on_playlist, LV_EVENT_CLICKED, s_playlist_ids[i]);

        lv_obj_t *lab = lv_label_create(btn);
        lv_label_set_text(lab, playlists[i].name);
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lab, t->text, 0);
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 12, 0);
    }

    return scr;
}
