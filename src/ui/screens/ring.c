#include "ui/screens/screens.h"
#include "ui/ui.h"
#include "ui/cover_art.h"
#include "spotify/player.h"
#include "board_pins.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CENTER_SIZE 128
#define CENTER_R (CENTER_SIZE / 2)
#define OUTER_R (DISPLAY_WIDTH / 2)
/* Match the ~6px ring gap around the center disc (inner pie starts at CENTER_R+6). */
#define SLICE_GAP_PX 6
#define SLICE_SPAN (360 / PLAYER_RING_NEXT)
#define PIE_SIDE DISPLAY_WIDTH
#define PIE_PIXELS ((size_t)PIE_SIDE * PIE_SIDE)

static const char *TAG = "ring";

static lv_obj_t *s_root;
static lv_obj_t *s_pie_img;
static lv_obj_t *s_center;
static lv_obj_t *s_center_img;
static lv_obj_t *s_center_ph;
static int s_slice_queue_idx[PLAYER_RING_NEXT];
static cover_art_t *s_slice_art[PLAYER_RING_NEXT];
static char s_slice_want_url[PLAYER_RING_NEXT][192];
/** Track id/qi frozen when the pie was last painted — tap plays THIS, not a later sync. */
static char s_slice_baked_id[PLAYER_RING_NEXT][64];
static int s_slice_baked_qi[PLAYER_RING_NEXT];
static int64_t s_center_taps[3];
static int s_center_tap_n;
static lv_timer_t *s_tap_timer;

static uint16_t *s_pie_pixels;
static lv_img_dsc_t s_pie_dsc;
static bool s_pie_dirty;
static bool s_wedges_active;
/** -1 = outside ring / gap; 0..4 = wedge. Built once in SPIRAM. */
static int8_t *s_wedge_lut;

/* Placeholder only while that wedge's cover is still downloading. */
static const uint32_t s_slice_tint[PLAYER_RING_NEXT] = {
    0x1B5F8A, 0x2278A8, 0x2A90C0, 0xC45E2C, 0xE07A40,
};

static uint16_t tint16(uint32_t rgb)
{
    /* lv_color_hex already matches LV_COLOR_16_SWAP framebuffer layout. */
    return lv_color_hex(rgb).full;
}

static double slice_gap_deg(void)
{
    /* Match the ~6px annulus around the center: same arc length at the pie inner edge. */
    const double inner_r = (double)(CENTER_R + 6);
    return (SLICE_GAP_PX * 180.0) / (M_PI * inner_r);
}

static void build_wedge_lut(void);

/** Returns wedge 0..4, or -1 if in the inter-slice gap / outside ring.
 * Uses the same LUT as painting so tap and cover always agree. */
static int point_to_wedge(int dx, int dy)
{
    if (!s_wedge_lut) {
        build_wedge_lut();
    }
    if (!s_wedge_lut) {
        return -1;
    }
    int x = dx + PIE_SIDE / 2;
    int y = dy + PIE_SIDE / 2;
    if (x < 0 || y < 0 || x >= PIE_SIDE || y >= PIE_SIDE) {
        return -1;
    }
    return (int)s_wedge_lut[y * PIE_SIDE + x];
}

static void build_wedge_lut(void)
{
    if (s_wedge_lut) {
        return;
    }
    s_wedge_lut = heap_caps_malloc(PIE_PIXELS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_wedge_lut) {
        ESP_LOGE(TAG, "wedge lut alloc failed");
        return;
    }
    const int cx = PIE_SIDE / 2;
    const int cy = PIE_SIDE / 2;
    const int inner2 = (CENTER_R + 6) * (CENTER_R + 6);
    const int outer2 = OUTER_R * OUTER_R;
    const double gap = slice_gap_deg();
    ESP_LOGI(TAG, "slice gap=%.1f deg (~%d px), wedges CCW from top", gap, SLICE_GAP_PX);
    for (int y = 0; y < PIE_SIDE; y++) {
        for (int x = 0; x < PIE_SIDE; x++) {
            int dx = x - cx;
            int dy = y - cy;
            int r2 = dx * dx + dy * dy;
            int8_t w = -1;
            if (r2 >= inner2 && r2 <= outer2) {
                double deg = atan2((double)dy, (double)dx) * 180.0 / M_PI;
                if (deg < 0) {
                    deg += 360.0;
                }
                /* Clockwise degrees from top → convert to counter-clockwise so
                 * wedge 0 (next track) sits in the top-LEFT sector. */
                double from_top_cw = deg - 270.0;
                if (from_top_cw < 0) {
                    from_top_cw += 360.0;
                }
                double from_top = fmod(360.0 - from_top_cw, 360.0);
                double in_slice = fmod(from_top, (double)SLICE_SPAN);
                if (in_slice >= (gap * 0.5) && in_slice <= (SLICE_SPAN - gap * 0.5)) {
                    w = (int8_t)((int)(from_top / (double)SLICE_SPAN) % PLAYER_RING_NEXT);
                }
            }
            s_wedge_lut[y * PIE_SIDE + x] = w;
        }
        if ((y & 15) == 0) {
            vTaskDelay(1);
        }
    }
    ESP_LOGI(TAG, "wedge lut ready");
}

/**
 * Bake wedges into one RGB565 image. Uses precomputed lut (no per-pixel atan2).
 */
static void rebuild_pie_cache(void)
{
    if (!s_pie_pixels || !s_pie_dirty) {
        return;
    }
    if (!s_wedge_lut) {
        build_wedge_lut();
        if (!s_wedge_lut) {
            return;
        }
    }

    uint16_t bg_px = tint16(0x0B3D6E);
    uint16_t idle_px = tint16(0x144A78);
    uint16_t tints[PLAYER_RING_NEXT];
    for (int i = 0; i < PLAYER_RING_NEXT; i++) {
        tints[i] = tint16(s_slice_tint[i]);
    }

    const uint16_t *art_px[PLAYER_RING_NEXT];
    int art_w[PLAYER_RING_NEXT];
    int art_h[PLAYER_RING_NEXT];
    int art_n = 0;
    for (int i = 0; i < PLAYER_RING_NEXT; i++) {
        art_px[i] = NULL;
        art_w[i] = art_h[i] = 0;
        if (!s_wedges_active || !s_slice_art[i] || !s_slice_art[i]->ready || !s_slice_art[i]->dsc.data) {
            continue;
        }
        int aw = s_slice_art[i]->dsc.header.w;
        int ah = s_slice_art[i]->dsc.header.h;
        if (aw < 1 || ah < 1) {
            continue;
        }
        art_px[i] = (const uint16_t *)s_slice_art[i]->dsc.data;
        art_w[i] = aw;
        art_h[i] = ah;
        art_n++;
    }

    for (int y = 0; y < PIE_SIDE; y++) {
        if ((y & 7) == 0) {
            vTaskDelay(1);
        }
        for (int x = 0; x < PIE_SIDE; x++) {
            size_t idx = (size_t)y * PIE_SIDE + (size_t)x;
            uint16_t *dst = &s_pie_pixels[idx];
            int8_t w = s_wedge_lut[idx];
            if (w < 0) {
                *dst = bg_px;
                continue;
            }
            if (!s_wedges_active) {
                *dst = idle_px;
                continue;
            }
            if (art_px[w]) {
                int ax = (x * art_w[w]) / PIE_SIDE;
                int ay = (y * art_h[w]) / PIE_SIDE;
                if (ax >= art_w[w]) {
                    ax = art_w[w] - 1;
                }
                if (ay >= art_h[w]) {
                    ay = art_h[w] - 1;
                }
                *dst = art_px[w][ay * art_w[w] + ax];
            } else {
                *dst = tints[w];
            }
        }
    }

    s_pie_dirty = false;
    if (s_pie_img) {
        lv_obj_invalidate(s_pie_img);
    }
    /* Freeze which track each painted wedge represents. */
    for (int i = 0; i < PLAYER_RING_NEXT; i++) {
        s_slice_baked_qi[i] = s_slice_queue_idx[i];
        s_slice_baked_id[i][0] = '\0';
        if (s_slice_queue_idx[i] < 0) {
            continue;
        }
        spotify_track_t tr = {0};
        if (player_queue_get(s_slice_queue_idx[i], &tr) && tr.id[0]) {
            strncpy(s_slice_baked_id[i], tr.id, sizeof(s_slice_baked_id[i]) - 1);
            s_slice_baked_id[i][sizeof(s_slice_baked_id[i]) - 1] = '\0';
        }
    }
    ESP_LOGI(TAG, "pie cache rebuilt (active=%d covers_used=%d)", (int)s_wedges_active, art_n);
}

static void mark_pie_dirty(void)
{
    s_pie_dirty = true;
}

static void on_circle_hit_test(lv_event_t *e)
{
    lv_hit_test_info_t *info = lv_event_get_hit_test_info(e);
    if (!info || !info->point) {
        return;
    }
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    lv_coord_t cx = (a.x1 + a.x2) / 2;
    lv_coord_t cy = (a.y1 + a.y2) / 2;
    lv_coord_t r = lv_obj_get_width(obj) / 2;
    lv_coord_t dx = info->point->x - cx;
    lv_coord_t dy = info->point->y - cy;
    info->res = (dx * dx + dy * dy) <= (r * r);
}

static void set_center_art(cover_art_t *art)
{
    if (!s_center_img || !s_center_ph) {
        return;
    }
    if (art && art->ready && art->dsc.data) {
        lv_img_set_src(s_center_img, &art->dsc);
        int zw = (CENTER_SIZE * 256) / (art->dsc.header.w ? art->dsc.header.w : 1);
        int zh = (CENTER_SIZE * 256) / (art->dsc.header.h ? art->dsc.header.h : 1);
        uint16_t zoom = (uint16_t)(zw > zh ? zw : zh);
        lv_img_set_zoom(s_center_img, zoom);
        lv_obj_set_size(s_center_img, art->dsc.header.w, art->dsc.header.h);
        lv_obj_center(s_center_img);
        lv_obj_clear_flag(s_center_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_center_ph, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_center_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_center_ph, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_cover_ready(int slot, cover_art_t *art, void *user)
{
    (void)user;
    ESP_LOGI(TAG, "cover_ready slot=%d ready=%d %dx%d url=%.50s", slot,
             art ? (int)art->ready : -1, art ? (int)art->dsc.header.w : 0,
             art ? (int)art->dsc.header.h : 0, art ? art->url : "");
    if (slot == 0) {
        set_center_art(art);
        return;
    }
    if (slot >= 1 && slot <= PLAYER_RING_NEXT) {
        s_slice_art[slot - 1] = art;
        /* Debounce: only mark dirty — rebuild once from the UI poll loop. */
        mark_pie_dirty();
    }
}

static void play_slice(int i)
{
    if (i < 0 || i >= PLAYER_RING_NEXT) {
        return;
    }
    int q = s_slice_baked_qi[i] >= 0 ? s_slice_baked_qi[i] : s_slice_queue_idx[i];
    const char *baked = s_slice_baked_id[i][0] ? s_slice_baked_id[i] : NULL;
    spotify_track_t tr = {0};
    if (q >= 0) {
        player_queue_get(q, &tr);
        const char *id = baked ? baked : tr.id;
        ESP_LOGI(TAG, "tap wedge=%d → qi=%d id=%.22s title=%.28s", i, q, id, tr.title);
        player_play_ring_choice(q, id);
        screen_ring_refresh();
    }
}

static void tap_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_center_tap_n == 1) {
        player_play_pause();
    }
    s_center_tap_n = 0;
    if (s_tap_timer) {
        lv_timer_del(s_tap_timer);
        s_tap_timer = NULL;
    }
}

static void handle_center_tap(void)
{
    int64_t now = esp_timer_get_time();
    if (s_center_tap_n > 0 && (now - s_center_taps[0]) > 900000) {
        s_center_tap_n = 0;
    }
    if (s_center_tap_n < 3) {
        s_center_taps[s_center_tap_n++] = now;
    }
    if (s_tap_timer) {
        lv_timer_del(s_tap_timer);
        s_tap_timer = NULL;
    }
    if (s_center_tap_n >= 3) {
        s_center_tap_n = 0;
        ui_show_screen(APP_SCREEN_SETTINGS);
        return;
    }
    s_tap_timer = lv_timer_create(tap_timer_cb, 450, NULL);
    lv_timer_set_repeat_count(s_tap_timer, 1);
}

static void on_center(lv_event_t *e)
{
    (void)e;
    handle_center_tap();
}

static int point_to_slice(lv_coord_t x, lv_coord_t y)
{
    lv_coord_t cx = DISPLAY_WIDTH / 2;
    lv_coord_t cy = DISPLAY_HEIGHT / 2;
    lv_coord_t dx = x - cx;
    lv_coord_t dy = y - cy;
    int r2 = dx * dx + dy * dy;
    int inner = (CENTER_R + 4) * (CENTER_R + 4);
    int outer = OUTER_R * OUTER_R;
    if (r2 < inner || r2 > outer) {
        return -1;
    }
    return point_to_wedge(dx, dy);
}

static void on_root_click(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(s_root, &a);
    lv_coord_t x = p.x - a.x1;
    lv_coord_t y = p.y - a.y1;

    lv_coord_t cx = DISPLAY_WIDTH / 2;
    lv_coord_t cy = DISPLAY_HEIGHT / 2;
    lv_coord_t dx = x - cx;
    lv_coord_t dy = y - cy;
    if (dx * dx + dy * dy <= (CENTER_R + 4) * (CENTER_R + 4)) {
        return;
    }
    int slice = point_to_slice(x, y);
    ESP_LOGI(TAG, "root tap xy=%d,%d r2=%d slice=%d", (int)x, (int)y, (int)(dx * dx + dy * dy),
             slice);
    if (slice >= 0) {
        play_slice(slice);
    }
    (void)e;
}

void screen_ring_poll(void)
{
    if (!s_root) {
        return;
    }
    if (s_pie_dirty) {
        rebuild_pie_cache();
        if (s_center) {
            lv_obj_move_foreground(s_center);
        }
    }
}

void screen_ring_refresh(void)
{
    if (!s_root) {
        return;
    }
    const player_state_t *st = player_get_state();
    size_t n = player_queue_count();

    spotify_track_t cur_tr = {0};
    const char *center_url = NULL;
    if (st && st->track.image_url[0]) {
        center_url = st->track.image_url;
    } else if (n > 0 && player_queue_get(0, &cur_tr)) {
        center_url = cur_tr.image_url;
    }
    cover_art_t *cart = cover_art_request(0, center_url);
    if (cart && cart->ready) {
        set_center_art(cart);
    } else if (!center_url || !center_url[0]) {
        set_center_art(NULL);
    }

    bool changed = false;
    bool was_active = s_wedges_active;
    s_wedges_active = (n > 1);
    if (was_active != s_wedges_active) {
        changed = true;
    }

    int with_art = 0;

    for (int i = 0; i < PLAYER_RING_NEXT; i++) {
        s_slice_queue_idx[i] = -1;
        if (!s_wedges_active) {
            if (s_slice_art[i]) {
                changed = true;
            }
            s_slice_art[i] = NULL;
            cover_art_clear_slot(i + 1);
            s_slice_want_url[i][0] = '\0';
            continue;
        }
    }

    if (s_wedges_active && n > 1) {
        /* Strict order: wedge i → queue[i+1] (current+1 …). Do not reshuffle by art —
         * that made covers disagree with Connect jump offsets. */
        for (int i = 0; i < PLAYER_RING_NEXT; i++) {
            int qi = i + 1;
            if (qi >= (int)n) {
                qi = 1 + (i % ((int)n - 1));
            }
            s_slice_queue_idx[i] = qi;
        }
    }

    for (int i = 0; i < PLAYER_RING_NEXT; i++) {
        if (!s_wedges_active) {
            continue;
        }

        int qi = s_slice_queue_idx[i];
        if (qi < 0) {
            continue;
        }

        spotify_track_t tr = {0};
        player_queue_get(qi, &tr);
        const char *url = tr.image_url[0] ? tr.image_url : NULL;
        cover_art_t *art = cover_art_request(i + 1, url);

        cover_art_t *prev = s_slice_art[i];
        if (!url) {
            s_slice_art[i] = NULL;
            s_slice_want_url[i][0] = '\0';
            ESP_LOGI(TAG, "wedge%d qi=%d title=%.24s NO image_url", i, qi, tr.title);
        } else if (art && art->ready) {
            s_slice_art[i] = art;
            strncpy(s_slice_want_url[i], url, sizeof(s_slice_want_url[i]) - 1);
            s_slice_want_url[i][sizeof(s_slice_want_url[i]) - 1] = '\0';
            with_art++;
            ESP_LOGI(TAG, "wedge%d qi=%d READY title=%.20s", i, qi, tr.title);
        } else if (strncmp(s_slice_want_url[i], url, sizeof(s_slice_want_url[i])) != 0) {
            s_slice_art[i] = NULL;
            strncpy(s_slice_want_url[i], url, sizeof(s_slice_want_url[i]) - 1);
            s_slice_want_url[i][sizeof(s_slice_want_url[i]) - 1] = '\0';
            ESP_LOGI(TAG, "wedge%d qi=%d LOADING title=%.20s", i, qi, tr.title);
        } else if (s_slice_art[i] && s_slice_art[i]->ready) {
            with_art++;
        }
        if (prev != s_slice_art[i]) {
            changed = true;
        }
    }

    ESP_LOGI(TAG, "refresh n=%u active=%d art_ready=%d/%d center_url=%d", (unsigned)n,
             (int)s_wedges_active, with_art, PLAYER_RING_NEXT,
             center_url && center_url[0] ? 1 : 0);

    if (changed || s_pie_dirty) {
        mark_pie_dirty();
        rebuild_pie_cache();
    }
    if (s_center) {
        lv_obj_move_foreground(s_center);
    }
}

lv_obj_t *screen_ring_create(lv_obj_t *parent)
{
    const ui_theme_t *t = ui_theme();

    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_style_bg_color(s_root, t->bg, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(s_root, true, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(s_root, on_root_click, LV_EVENT_CLICKED, NULL);

    cover_art_set_ready_callback(on_cover_ready, NULL);
    memset(s_slice_want_url, 0, sizeof(s_slice_want_url));
    memset(s_slice_art, 0, sizeof(s_slice_art));

    s_pie_pixels = heap_caps_malloc(PIE_PIXELS * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pie_pixels) {
        ESP_LOGE(TAG, "pie buffer alloc failed");
    } else {
        memset(s_pie_pixels, 0, PIE_PIXELS * sizeof(uint16_t));
        s_pie_dsc.header.always_zero = 0;
        s_pie_dsc.header.w = PIE_SIDE;
        s_pie_dsc.header.h = PIE_SIDE;
        s_pie_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        s_pie_dsc.data_size = (uint32_t)(PIE_PIXELS * sizeof(uint16_t));
        s_pie_dsc.data = (const uint8_t *)s_pie_pixels;
    }
    build_wedge_lut();

    s_pie_img = lv_img_create(s_root);
    lv_obj_set_size(s_pie_img, PIE_SIDE, PIE_SIDE);
    lv_obj_align(s_pie_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_pie_img, LV_OBJ_FLAG_CLICKABLE);
    if (s_pie_pixels) {
        lv_img_set_src(s_pie_img, &s_pie_dsc);
    }

    /* Full circle now-playing on top of the pie. */
    s_center = lv_obj_create(s_root);
    lv_obj_set_size(s_center, CENTER_SIZE, CENTER_SIZE);
    lv_obj_set_style_radius(s_center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_center, t->accent, 0);
    lv_obj_set_style_bg_opa(s_center, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_center, 3, 0);
    lv_obj_set_style_border_color(s_center, t->bg, 0);
    lv_obj_set_style_pad_all(s_center, 0, 0);
    lv_obj_set_style_clip_corner(s_center, true, 0);
    lv_obj_clear_flag(s_center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_center, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_center, on_circle_hit_test, LV_EVENT_HIT_TEST, NULL);
    lv_obj_add_event_cb(s_center, on_center, LV_EVENT_CLICKED, NULL);
    lv_obj_align(s_center, LV_ALIGN_CENTER, 0, 0);

    s_center_img = lv_img_create(s_center);
    lv_obj_clear_flag(s_center_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_center_img, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_HIDDEN);

    s_center_ph = lv_label_create(s_center);
    lv_label_set_text(s_center_ph, LV_SYMBOL_PLAY);
    lv_obj_clear_flag(s_center_ph, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_center_ph, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_text_color(s_center_ph, t->bg, 0);
    lv_obj_set_style_text_font(s_center_ph, &lv_font_montserrat_28, 0);
    lv_obj_center(s_center_ph);

    lv_obj_move_foreground(s_center);

    mark_pie_dirty();
    screen_ring_refresh();
    return s_root;
}

void screen_ring_set_visible(bool visible)
{
    if (!s_root) {
        return;
    }
    if (visible) {
        lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        screen_ring_refresh();
    } else {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    }
}
