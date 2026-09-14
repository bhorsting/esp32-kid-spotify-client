#include "ui/widgets/kid_keyboard.h"
#include "ui/ui.h"

#include <stdlib.h>
#include <string.h>

/* Pages: upper / lower / digits / symbols — enough for Wi‑Fi SSIDs & passwords. */
static const char *const k_pages[6][9] = {
    {"A", "B", "C", "D", "E", "F", "G", "H", "I"},
    {"J", "K", "L", "M", "N", "O", "P", "Q", "R"},
    {"S", "T", "U", "V", "W", "X", "Y", "Z", "'"},
    {"a", "b", "c", "d", "e", "f", "g", "h", "i"},
    {"1", "2", "3", "4", "5", "6", "7", "8", "9"},
    {"0", "-", "_", ".", "@", "!", "#", "*", "?"},
};

static const char *const k_page_labels[6] = {"ABC", "JKL", "STU", "abc", "123", "#?!"};

static void refresh_keys(kid_keyboard_t *kb)
{
    for (int i = 0; i < 9; i++) {
        lv_label_set_text(lv_obj_get_child(kb->keys[i], 0), k_pages[kb->page][i]);
    }
    lv_label_set_text(lv_obj_get_child(kb->page_btn, 0), k_page_labels[kb->page]);
}

static void emit_change(kid_keyboard_t *kb)
{
    lv_label_set_text(kb->field, kb->buffer[0] ? kb->buffer : "Type here...");
    if (kb->on_change) {
        kb->on_change(kb->buffer, kb->user);
    }
}

static void append_char(kid_keyboard_t *kb, const char *ch)
{
    size_t len = strlen(kb->buffer);
    size_t add = strlen(ch);
    if (len + add >= sizeof(kb->buffer)) {
        return;
    }
    memcpy(kb->buffer + len, ch, add + 1);
    emit_change(kb);
}

static void on_letter(lv_event_t *e)
{
    kid_keyboard_t *kb = (kid_keyboard_t *)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    const char *ch = lv_label_get_text(lv_obj_get_child(btn, 0));
    if (ch && ch[0]) {
        append_char(kb, ch);
    }
}

static void on_page(lv_event_t *e)
{
    kid_keyboard_t *kb = (kid_keyboard_t *)lv_event_get_user_data(e);
    kb->page = (kb->page + 1) % 6;
    refresh_keys(kb);
}

static void on_space(lv_event_t *e)
{
    kid_keyboard_t *kb = (kid_keyboard_t *)lv_event_get_user_data(e);
    append_char(kb, " ");
}

static void on_back(lv_event_t *e)
{
    kid_keyboard_t *kb = (kid_keyboard_t *)lv_event_get_user_data(e);
    size_t len = strlen(kb->buffer);
    if (len > 0) {
        kb->buffer[len - 1] = '\0';
        emit_change(kb);
    }
}

static void on_go(lv_event_t *e)
{
    kid_keyboard_t *kb = (kid_keyboard_t *)lv_event_get_user_data(e);
    if (kb->on_submit) {
        kb->on_submit(kb->buffer, kb->user);
    }
}

static lv_obj_t *make_key(lv_obj_t *parent, kid_keyboard_t *kb, lv_event_cb_t cb, int w, int h)
{
    const ui_theme_t *t = ui_theme();
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_bg_color(btn, t->surface, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, kb);

    lv_obj_t *lab = lv_label_create(btn);
    lv_obj_set_style_text_color(lab, t->text, 0);
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);
    lv_label_set_text(lab, "");
    lv_obj_center(lab);
    return btn;
}

kid_keyboard_t *kid_keyboard_create(lv_obj_t *parent)
{
    kid_keyboard_t *kb = (kid_keyboard_t *)calloc(1, sizeof(kid_keyboard_t));
    if (!kb) {
        return NULL;
    }

    const ui_theme_t *t = ui_theme();
    const int kb_w = 240;
    const int key_w = 72;
    const int key_h = 34;
    const int gap = 6;
    const int grid_w = 3 * key_w + 2 * gap;
    const int start_x = (kb_w - grid_w) / 2;

    kb->root = lv_obj_create(parent);
    lv_obj_set_size(kb->root, kb_w, 168);
    lv_obj_set_style_bg_opa(kb->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(kb->root, 0, 0);
    lv_obj_set_style_pad_all(kb->root, 0, 0);
    lv_obj_clear_flag(kb->root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    kb->field = lv_label_create(kb->root);
    lv_obj_set_width(kb->field, kb_w - 8);
    lv_obj_align(kb->field, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_color(kb->field, t->text_muted, 0);
    lv_obj_set_style_text_font(kb->field, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(kb->field, LV_LABEL_LONG_CLIP);
    lv_label_set_text(kb->field, "Type here...");

    for (int i = 0; i < 9; i++) {
        int col = i % 3;
        int row = i / 3;
        kb->keys[i] = make_key(kb->root, kb, on_letter, key_w, key_h);
        lv_obj_set_pos(kb->keys[i], start_x + col * (key_w + gap), 20 + row * (key_h + gap));
    }

    int y2 = 20 + 3 * (key_h + gap);
    kb->page_btn = make_key(kb->root, kb, on_page, 52, key_h);
    lv_obj_set_pos(kb->page_btn, start_x, y2);

    kb->space_btn = make_key(kb->root, kb, on_space, 72, key_h);
    lv_obj_set_pos(kb->space_btn, start_x + 52 + gap, y2);
    lv_label_set_text(lv_obj_get_child(kb->space_btn, 0), "spc");

    kb->back_btn = make_key(kb->root, kb, on_back, 44, key_h);
    lv_obj_set_pos(kb->back_btn, start_x + 52 + gap + 72 + gap, y2);
    lv_label_set_text(lv_obj_get_child(kb->back_btn, 0), LV_SYMBOL_BACKSPACE);

    kb->go_btn = make_key(kb->root, kb, on_go, 48, key_h);
    lv_obj_set_pos(kb->go_btn, start_x + 52 + gap + 72 + gap + 44 + gap, y2);
    lv_obj_set_style_bg_color(kb->go_btn, t->accent, 0);
    lv_label_set_text(lv_obj_get_child(kb->go_btn, 0), "GO");
    lv_obj_set_style_text_color(lv_obj_get_child(kb->go_btn, 0), t->bg, 0);

    refresh_keys(kb);
    return kb;
}

void kid_keyboard_set_callbacks(kid_keyboard_t *kb, kid_kb_change_cb_t on_change,
                                kid_kb_submit_cb_t on_submit, void *user)
{
    if (!kb) {
        return;
    }
    kb->on_change = on_change;
    kb->on_submit = on_submit;
    kb->user = user;
}

void kid_keyboard_set_text(kid_keyboard_t *kb, const char *text)
{
    if (!kb) {
        return;
    }
    if (!text) {
        kb->buffer[0] = '\0';
    } else {
        strncpy(kb->buffer, text, sizeof(kb->buffer) - 1);
        kb->buffer[sizeof(kb->buffer) - 1] = '\0';
    }
    emit_change(kb);
}

const char *kid_keyboard_get_text(const kid_keyboard_t *kb)
{
    return kb ? kb->buffer : "";
}

void kid_keyboard_clear(kid_keyboard_t *kb)
{
    kid_keyboard_set_text(kb, "");
}
