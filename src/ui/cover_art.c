/**
 * Download Spotify JPEG covers and decode to RGB565 for LVGL.
 * Texture cache is keyed by URL so ring reshuffles don't re-download.
 * Jobs are URL-based; finished downloads only bind to slots that still want that URL.
 */
#include "ui/cover_art.h"
#include "board/sdcard.h"
#include "board_pins.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "jpeg_decoder.h"

static const char *TAG = "cover";

#define COVER_SLOTS 6
#define COVER_CACHE 8
#define COVER_MAX_JPEG (96 * 1024)
#define COVER_MAX_SIDE 320
/* Pre-scale to disc size on the worker so LVGL can 1:1 blit (no UI zoom). */
#define COVER_DISC_SIDE DISPLAY_WIDTH

typedef struct {
    char url[192];
    uint8_t *pixels;
    int w;
    int h;
    int refs;
    bool ready;
    bool loading;
} cover_tex_t;

typedef struct {
    char url[192];
} cover_job_t;

typedef struct {
    char url[192];
    bool ok;
} cover_done_t;

static cover_art_t s_slots[COVER_SLOTS];
static cover_tex_t s_tex[COVER_CACHE];
static cover_tex_t *s_slot_tex[COVER_SLOTS];
static QueueHandle_t s_jobs;
static QueueHandle_t s_done;
static cover_art_ready_cb_t s_ready_cb;
static void *s_ready_user;
static bool s_worker_started;

static void bind_slot_to_tex(int slot, cover_tex_t *tex)
{
    cover_art_t *art = &s_slots[slot];
    if (s_slot_tex[slot] != tex) {
        if (s_slot_tex[slot] && s_slot_tex[slot]->refs > 0) {
            s_slot_tex[slot]->refs--;
        }
        s_slot_tex[slot] = tex;
        if (tex) {
            tex->refs++;
        }
    }
    if (!tex || !tex->ready || !tex->pixels) {
        memset(&art->dsc, 0, sizeof(art->dsc));
        art->pixels = NULL;
        if (tex && tex->url[0]) {
            strncpy(art->url, tex->url, sizeof(art->url) - 1);
            art->url[sizeof(art->url) - 1] = '\0';
        } else {
            art->url[0] = '\0';
        }
        art->ready = false;
        return;
    }
    strncpy(art->url, tex->url, sizeof(art->url) - 1);
    art->url[sizeof(art->url) - 1] = '\0';
    art->pixels = tex->pixels;
    art->dsc.header.always_zero = 0;
    art->dsc.header.w = tex->w;
    art->dsc.header.h = tex->h;
    art->dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    art->dsc.data_size = (uint32_t)tex->w * (uint32_t)tex->h * 2;
    art->dsc.data = tex->pixels;
    art->ready = true;
}

static cover_tex_t *find_tex(const char *url)
{
    if (!url || !url[0]) {
        return NULL;
    }
    for (int i = 0; i < COVER_CACHE; i++) {
        if (s_tex[i].url[0] && strncmp(s_tex[i].url, url, sizeof(s_tex[i].url)) == 0) {
            return &s_tex[i];
        }
    }
    return NULL;
}

static cover_tex_t *alloc_tex(const char *url)
{
    cover_tex_t *existing = find_tex(url);
    if (existing) {
        return existing;
    }
    cover_tex_t *free_slot = NULL;
    cover_tex_t *victim = NULL;
    for (int i = 0; i < COVER_CACHE; i++) {
        if (!s_tex[i].url[0] && !s_tex[i].loading) {
            free_slot = &s_tex[i];
            break;
        }
        if (!s_tex[i].loading && s_tex[i].refs == 0) {
            victim = &s_tex[i];
        }
    }
    cover_tex_t *t = free_slot ? free_slot : victim;
    if (!t) {
        return NULL;
    }
    if (t->pixels) {
        heap_caps_free(t->pixels);
        t->pixels = NULL;
    }
    memset(t, 0, sizeof(*t));
    strncpy(t->url, url, sizeof(t->url) - 1);
    return t;
}

static bool http_get_bytes(const char *url, uint8_t **out, int *out_len)
{
    *out = NULL;
    *out_len = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 10000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return false;
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP open fail %s: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
    (void)esp_http_client_fetch_headers(client);
    int http_status = esp_http_client_get_status_code(client);
    if (http_status > 0 && (http_status < 200 || http_status >= 300)) {
        ESP_LOGW(TAG, "HTTP status %d for cover", http_status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    uint8_t *buf = heap_caps_malloc(COVER_MAX_JPEG, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    int total = 0;
    while (total < COVER_MAX_JPEG) {
        int n = esp_http_client_read(client, (char *)buf + total, COVER_MAX_JPEG - total);
        if (n <= 0) {
            break;
        }
        total += n;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (total < 64) {
        heap_caps_free(buf);
        return false;
    }
    *out = buf;
    *out_len = total;
    return true;
}

static bool decode_jpeg_rgb565(const uint8_t *jpeg, int jpeg_len, uint8_t **pixels, int *w, int *h)
{
    *pixels = NULL;
    *w = *h = 0;
    size_t out_size = (size_t)COVER_MAX_SIDE * COVER_MAX_SIDE * 2;
    uint8_t *out = heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out) {
        return false;
    }

    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)jpeg,
        .indata_size = (uint32_t)jpeg_len,
        .outbuf = out,
        .outbuf_size = (uint32_t)out_size,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = {.swap_color_bytes = 1},
    };
    esp_jpeg_image_output_t info = {0};
    esp_err_t err = esp_jpeg_decode(&cfg, &info);
    if (err != ESP_OK || info.width == 0 || info.height == 0 ||
        info.width > COVER_MAX_SIDE || info.height > COVER_MAX_SIDE) {
        cfg.out_scale = JPEG_IMAGE_SCALE_1_2;
        memset(&info, 0, sizeof(info));
        err = esp_jpeg_decode(&cfg, &info);
    }
    if (err != ESP_OK || info.width == 0 || info.height == 0 ||
        info.width > COVER_MAX_SIDE || info.height > COVER_MAX_SIDE) {
        cfg.out_scale = JPEG_IMAGE_SCALE_1_4;
        memset(&info, 0, sizeof(info));
        err = esp_jpeg_decode(&cfg, &info);
    }
    if (err != ESP_OK || info.width == 0 || info.height == 0) {
        heap_caps_free(out);
        return false;
    }
    *pixels = out;
    *w = (int)info.width;
    *h = (int)info.height;
    return true;
}

/** object-fit: cover into a COVER_DISC_SIDE square (nearest neighbour). */
static bool scale_to_disc(uint8_t *src, int sw, int sh, uint8_t **out_pixels, int *out_w, int *out_h)
{
    if (!src || sw < 1 || sh < 1) {
        return false;
    }
    const int side = COVER_DISC_SIDE;
    size_t bytes = (size_t)side * side * 2;
    uint8_t *dst = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!dst) {
        return false;
    }

    /* Scale so the shorter side fills; crop the overflow. */
    int scale_num = side;
    int scale_den = sw < sh ? sw : sh;
    int rw = (sw * scale_num) / scale_den;
    int rh = (sh * scale_num) / scale_den;
    int x0 = (rw - side) / 2;
    int y0 = (rh - side) / 2;

    uint16_t *d16 = (uint16_t *)dst;
    const uint16_t *s16 = (const uint16_t *)src;
    for (int y = 0; y < side; y++) {
        int sy = ((y + y0) * scale_den) / scale_num;
        if (sy < 0) {
            sy = 0;
        }
        if (sy >= sh) {
            sy = sh - 1;
        }
        for (int x = 0; x < side; x++) {
            int sx = ((x + x0) * scale_den) / scale_num;
            if (sx < 0) {
                sx = 0;
            }
            if (sx >= sw) {
                sx = sw - 1;
            }
            d16[y * side + x] = s16[sy * sw + sx];
        }
    }
    *out_pixels = dst;
    *out_w = side;
    *out_h = side;
    return true;
}

static bool load_url_into_tex(cover_tex_t *tex, const char *url)
{
    if (!tex || !url || !url[0]) {
        return false;
    }
    if (tex->ready && strncmp(tex->url, url, sizeof(tex->url)) == 0) {
        tex->loading = false;
        return true;
    }

    /* Prefer TF cache — skip Wi‑Fi when we already have a disc-scaled RGB565. */
    uint8_t *pixels = NULL;
    int w = 0, h = 0;
    if (board_sd_cover_load(url, &pixels, &w, &h)) {
        if (tex->pixels) {
            heap_caps_free(tex->pixels);
        }
        strncpy(tex->url, url, sizeof(tex->url) - 1);
        tex->url[sizeof(tex->url) - 1] = '\0';
        tex->pixels = pixels;
        tex->w = w;
        tex->h = h;
        tex->ready = true;
        tex->loading = false;
        ESP_LOGI(TAG, "cache ready (SD) %dx%d %.40s", w, h, url);
        return true;
    }

    uint8_t *jpeg = NULL;
    int jpeg_len = 0;
    if (!http_get_bytes(url, &jpeg, &jpeg_len)) {
        ESP_LOGW(TAG, "download failed url=%.60s", url);
        tex->loading = false;
        return false;
    }
    ESP_LOGI(TAG, "downloaded %d bytes", jpeg_len);

    pixels = NULL;
    w = 0;
    h = 0;
    bool ok = decode_jpeg_rgb565(jpeg, jpeg_len, &pixels, &w, &h);
    heap_caps_free(jpeg);
    if (!ok) {
        ESP_LOGW(TAG, "decode failed");
        tex->loading = false;
        return false;
    }

    uint8_t *disc = NULL;
    int dw = 0, dh = 0;
    if (scale_to_disc(pixels, w, h, &disc, &dw, &dh)) {
        heap_caps_free(pixels);
        pixels = disc;
        w = dw;
        h = dh;
    }

    if (tex->pixels) {
        heap_caps_free(tex->pixels);
    }
    strncpy(tex->url, url, sizeof(tex->url) - 1);
    tex->url[sizeof(tex->url) - 1] = '\0';
    tex->pixels = pixels;
    tex->w = w;
    tex->h = h;
    tex->ready = true;
    tex->loading = false;
    (void)board_sd_cover_save(url, pixels, w, h);
    ESP_LOGI(TAG, "cache ready %dx%d %.40s", w, h, url);
    return true;
}

static void cover_worker(void *arg)
{
    (void)arg;
    cover_job_t job;
    while (xQueueReceive(s_jobs, &job, portMAX_DELAY) == pdTRUE) {
        cover_tex_t *tex = find_tex(job.url);
        if (!tex) {
            tex = alloc_tex(job.url);
        }
        bool ok = false;
        if (tex) {
            if (tex->ready) {
                ok = true;
                tex->loading = false;
            } else {
                tex->loading = true;
                ok = load_url_into_tex(tex, job.url);
                if (!ok) {
                    vTaskDelay(pdMS_TO_TICKS(300));
                    tex->loading = true;
                    ok = load_url_into_tex(tex, job.url);
                }
            }
        }
        cover_done_t done = {.ok = ok};
        strncpy(done.url, job.url, sizeof(done.url) - 1);
        xQueueSend(s_done, &done, 0);
        /* Let IDLE / Wi‑Fi / audio breathe between JPEG downloads. */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void cover_art_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    memset(s_tex, 0, sizeof(s_tex));
    memset(s_slot_tex, 0, sizeof(s_slot_tex));
    s_jobs = xQueueCreate(12, sizeof(cover_job_t));
    s_done = xQueueCreate(12, sizeof(cover_done_t));
    if (!s_worker_started && s_jobs && s_done) {
        xTaskCreatePinnedToCore(cover_worker, "cover", 12288, NULL, 3, NULL, 0);
        s_worker_started = true;
    }
}

void cover_art_set_ready_callback(cover_art_ready_cb_t cb, void *user)
{
    s_ready_cb = cb;
    s_ready_user = user;
}

cover_art_t *cover_art_request(int slot, const char *url)
{
    if (slot < 0 || slot >= COVER_SLOTS) {
        return NULL;
    }
    if (!url || !url[0]) {
        cover_art_clear_slot(slot);
        ESP_LOGI(TAG, "request slot=%d CLEAR (no url)", slot);
        return NULL;
    }

    cover_tex_t *tex = find_tex(url);
    if (tex && tex->ready) {
        bind_slot_to_tex(slot, tex);
        ESP_LOGI(TAG, "request slot=%d HIT ready %dx%d %.40s", slot, tex->w, tex->h, url);
        return &s_slots[slot];
    }

    /* Already waiting on this URL for this slot — keep binding, don't re-queue. */
    if (s_slot_tex[slot] && strncmp(s_slot_tex[slot]->url, url, sizeof(s_slot_tex[slot]->url)) == 0) {
        if (s_slot_tex[slot]->ready) {
            bind_slot_to_tex(slot, s_slot_tex[slot]);
            ESP_LOGI(TAG, "request slot=%d HIT bound-ready %.40s", slot, url);
            return &s_slots[slot];
        }
        ESP_LOGI(TAG, "request slot=%d WAIT in-flight %.40s", slot, url);
        return NULL;
    }

    if (!tex) {
        tex = alloc_tex(url);
    }
    if (!tex) {
        ESP_LOGW(TAG, "request slot=%d FAIL alloc_tex %.40s", slot, url);
        return NULL;
    }

    bind_slot_to_tex(slot, tex);
    s_slots[slot].ready = false;

    /* Dedupe: one download per URL; loading flag means a job is in flight. */
    if (!tex->ready && !tex->loading) {
        tex->loading = true;
        cover_job_t job = {0};
        strncpy(job.url, url, sizeof(job.url) - 1);
        if (s_jobs && xQueueSend(s_jobs, &job, 0) != pdTRUE) {
            tex->loading = false;
            ESP_LOGW(TAG, "cover job queue full slot=%d", slot);
        } else {
            ESP_LOGI(TAG, "request slot=%d QUEUED %.40s", slot, url);
        }
    } else {
        ESP_LOGI(TAG, "request slot=%d JOIN loading=%d ready=%d %.40s", slot, (int)tex->loading,
                 (int)tex->ready, url);
    }
    return NULL;
}

void cover_art_poll(void)
{
    if (!s_done) {
        return;
    }
    cover_done_t done;
    while (xQueueReceive(s_done, &done, 0) == pdTRUE) {
        ESP_LOGI(TAG, "done ok=%d url=%.50s", (int)done.ok, done.url);
        if (!done.ok || !done.url[0]) {
            continue;
        }
        cover_tex_t *tex = find_tex(done.url);
        if (!tex || !tex->ready) {
            ESP_LOGW(TAG, "done but tex missing/not ready");
            continue;
        }
        int notified = 0;
        for (int slot = 0; slot < COVER_SLOTS; slot++) {
            if (!s_slot_tex[slot]) {
                continue;
            }
            if (strncmp(s_slot_tex[slot]->url, done.url, sizeof(s_slot_tex[slot]->url)) != 0) {
                continue;
            }
            bind_slot_to_tex(slot, tex);
            notified++;
            if (s_ready_cb) {
                s_ready_cb(slot, &s_slots[slot], s_ready_user);
            }
        }
        ESP_LOGI(TAG, "done notified %d slot(s) for %.40s", notified, done.url);
    }
}

void cover_art_clear_slot(int slot)
{
    if (slot < 0 || slot >= COVER_SLOTS) {
        return;
    }
    bind_slot_to_tex(slot, NULL);
    s_slots[slot].url[0] = '\0';
}
