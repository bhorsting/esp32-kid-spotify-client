/**
 * Spotify player — curated playlist queue + Connect playback.
 */
#include "spotify/player.h"

#include "app_config.h"
#include "board/audio.h"
#include "spotify_bridge.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "player";
#define NVS_NS "marten_pl"
#define NVS_KEY "playlist"

static player_state_t s_state;
static player_state_cb_t s_cb;
static void *s_cb_user;

static spotify_track_t s_queue[PLAYER_QUEUE_MAX];
static size_t s_queue_n;
static int s_queue_i;
static char s_playlist_id[64];

static void notify(void)
{
    if (s_cb) {
        s_cb(&s_state, s_cb_user);
    }
}

static void request_queue_sync(void);

static void apply_track_to_state(const spotify_track_t *t)
{
    s_state.track = *t;
    s_state.has_track = t->id[0] != '\0' || t->title[0] != '\0';
}

static void on_cspot_track(const char *title, const char *artist, const char *album,
                           const char *track_id, const char *image_url, uint32_t duration_ms,
                           bool playing, void *user)
{
    (void)user;
    bool track_changed = false;
    if (title && title[0]) {
        if (strcmp(s_state.track.title, title) != 0) {
            track_changed = true;
        }
        strncpy(s_state.track.title, title, sizeof(s_state.track.title) - 1);
        s_state.has_track = true;
    }
    if (artist && artist[0]) {
        strncpy(s_state.track.artist, artist, sizeof(s_state.track.artist) - 1);
    }
    if (album && album[0]) {
        strncpy(s_state.track.album, album, sizeof(s_state.track.album) - 1);
    }
    if (track_id && track_id[0]) {
        if (strcmp(s_state.track.id, track_id) != 0) {
            track_changed = true;
        }
        strncpy(s_state.track.id, track_id, sizeof(s_state.track.id) - 1);
    }
    if (image_url && image_url[0]) {
        strncpy(s_state.track.image_url, image_url, sizeof(s_state.track.image_url) - 1);
    } else if (s_queue_n > 0 && s_queue[0].image_url[0] &&
               ((title && title[0] && strcmp(s_queue[0].title, title) == 0) ||
                (track_id && track_id[0] && strcmp(s_queue[0].id, track_id) == 0))) {
        strncpy(s_state.track.image_url, s_queue[0].image_url, sizeof(s_state.track.image_url) - 1);
    }
    if (duration_ms > 0) {
        s_state.track.duration_ms = duration_ms;
    }
    s_state.playing = playing;
    /* Ring snapshot is always [current, next…]; keep index 0 = now. */
    s_queue_i = 0;
    notify();
    if (track_changed || s_queue_n == 0) {
        request_queue_sync();
    }
}

static bool nvs_load_playlist(char *buf, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t l = len;
    esp_err_t e = nvs_get_str(h, NVS_KEY, buf, &l);
    nvs_close(h);
    return e == ESP_OK && buf[0];
}

static bool nvs_save_playlist(const char *id)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    nvs_set_str(h, NVS_KEY, id ? id : "");
    nvs_commit(h);
    nvs_close(h);
    return true;
}

typedef struct {
    char playlist_id[64];
    char track_id[64];
    int offset;
    bool use_track;
} play_job_t;

static void play_job_task(void *arg)
{
    play_job_t *job = (play_job_t *)arg;
    bool ok = false;
    if (job->use_track) {
        ok = spotify_connect_play_track_id(job->track_id);
    } else {
        ok = spotify_connect_play_playlist_offset(job->playlist_id, job->offset);
    }
    if (!ok) {
        strncpy(s_state.track.title, "Couldn't play", sizeof(s_state.track.title) - 1);
        strncpy(s_state.track.artist, "Check playlist / Spotify", sizeof(s_state.track.artist) - 1);
        s_state.playing = false;
        notify();
    }
    free(job);
    vTaskDelete(NULL);
}

static void start_play_offset(int offset)
{
    if (!spotify_connect_is_ready() || !s_playlist_id[0]) {
        return;
    }
    play_job_t *job = (play_job_t *)calloc(1, sizeof(*job));
    if (!job) {
        return;
    }
    strncpy(job->playlist_id, s_playlist_id, sizeof(job->playlist_id) - 1);
    job->offset = offset;
    job->use_track = false;
    if (xTaskCreate(play_job_task, "pl_play", 24 * 1024, job, 4, NULL) != pdPASS) {
        free(job);
    }
}

static void start_play_track(const char *track_id)
{
    if (!spotify_connect_is_ready() || !track_id || !track_id[0]) {
        return;
    }
    play_job_t *job = (play_job_t *)calloc(1, sizeof(*job));
    if (!job) {
        return;
    }
    strncpy(job->track_id, track_id, sizeof(job->track_id) - 1);
    job->use_track = true;
    if (xTaskCreate(play_job_task, "tr_play", 24 * 1024, job, 4, NULL) != pdPASS) {
        free(job);
    }
}

static void copy_web_into_queue_slot(size_t i, const spotify_web_track_t *web)
{
    memset(&s_queue[i], 0, sizeof(s_queue[i]));
    strncpy(s_queue[i].id, web->id, sizeof(s_queue[i].id) - 1);
    strncpy(s_queue[i].title, web->title, sizeof(s_queue[i].title) - 1);
    strncpy(s_queue[i].artist, web->artist, sizeof(s_queue[i].artist) - 1);
    strncpy(s_queue[i].album, web->album, sizeof(s_queue[i].album) - 1);
    strncpy(s_queue[i].image_url, web->image_url, sizeof(s_queue[i].image_url) - 1);
    s_queue[i].duration_ms = web->duration_ms;
    s_queue[i].connect_index = web->connect_index;
}

/** Keep cover/title from a previous sync when Mercury returns id-only again. */
static void merge_prev_queue_meta(const spotify_track_t *prev, size_t prev_n)
{
    for (size_t i = 0; i < s_queue_n; i++) {
        if (!s_queue[i].id[0]) {
            continue;
        }
        if (s_queue[i].image_url[0] && s_queue[i].title[0]) {
            continue;
        }
        for (size_t j = 0; j < prev_n; j++) {
            if (strcmp(prev[j].id, s_queue[i].id) != 0) {
                continue;
            }
            if (!s_queue[i].title[0] && prev[j].title[0]) {
                strncpy(s_queue[i].title, prev[j].title, sizeof(s_queue[i].title) - 1);
            }
            if (!s_queue[i].artist[0] && prev[j].artist[0]) {
                strncpy(s_queue[i].artist, prev[j].artist, sizeof(s_queue[i].artist) - 1);
            }
            if (!s_queue[i].album[0] && prev[j].album[0]) {
                strncpy(s_queue[i].album, prev[j].album, sizeof(s_queue[i].album) - 1);
            }
            if (!s_queue[i].image_url[0] && prev[j].image_url[0]) {
                strncpy(s_queue[i].image_url, prev[j].image_url,
                        sizeof(s_queue[i].image_url) - 1);
            }
            if (s_queue[i].duration_ms == 0) {
                s_queue[i].duration_ms = prev[j].duration_ms;
            }
            break;
        }
    }
}

static volatile bool s_queue_sync_busy;
static TickType_t s_sync_busy_since;
static char s_last_sync_track[96];
static uint8_t s_sync_retries;

static bool queue_signature(char *buf, size_t buflen)
{
    if (!buf || buflen < 8) {
        return false;
    }
    size_t off = 0;
    for (size_t i = 0; i < s_queue_n && off + 80 < buflen; i++) {
        int n = snprintf(buf + off, buflen - off, "%s|%s;", s_queue[i].id, s_queue[i].image_url);
        if (n < 0) {
            break;
        }
        off += (size_t)n;
    }
    buf[buflen - 1] = '\0';
    return true;
}

typedef struct {
    int delay_ms;
    bool allow_enrich;
} q_sync_job_t;

static bool spawn_q_sync(int delay_ms, bool allow_enrich);
static void sync_player_queue_task(void *arg);

static bool spawn_q_sync(int delay_ms, bool allow_enrich)
{
    q_sync_job_t *job = (q_sync_job_t *)malloc(sizeof(*job));
    if (!job) {
        return false;
    }
    job->delay_ms = delay_ms;
    job->allow_enrich = allow_enrich;

    /* 32KB internal stacks fail under CDN audio — put q_sync in PSRAM. */
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    BaseType_t ok = xTaskCreateWithCaps(sync_player_queue_task, "q_sync", 24 * 1024, job, 2,
                                         NULL, caps);
    if (ok != pdPASS) {
        /* Smaller SPIRAM stack, skip enrich (JSON is the stack hog). */
        job->allow_enrich = false;
        ok = xTaskCreateWithCaps(sync_player_queue_task, "q_sync", 10 * 1024, job, 2, NULL, caps);
    }
    if (ok != pdPASS) {
        free(job);
        ESP_LOGW(TAG, "q_sync task create failed (SPIRAM stack)");
        return false;
    }
    return true;
}

static void sync_player_queue_task(void *arg)
{
    q_sync_job_t *job = (q_sync_job_t *)arg;
    int delay_ms = job ? job->delay_ms : 800;
    bool allow_enrich = job ? job->allow_enrich : false;
    free(job);

    if (delay_ms < 200) {
        delay_ms = 200;
    }

    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    const size_t max_up = PLAYER_RING_NEXT + 1;
    spotify_web_track_t *tracks =
        (spotify_web_track_t *)calloc(max_up, sizeof(spotify_web_track_t));
    if (!tracks) {
        s_queue_sync_busy = false;
        vTaskDeleteWithCaps(NULL);
        return;
    }

    size_t n = 0;
    (void)spotify_connect_ring_tracks(tracks, max_up, &n);
    ESP_LOGI(TAG, "q_sync snapshot n=%u enrich=%d", (unsigned)n, (int)allow_enrich);

    char *prev_sig = (char *)malloc(512);
    char *new_sig = (char *)malloc(512);
    if (prev_sig) {
        queue_signature(prev_sig, 512);
    }

    /* Must not live on the task stack — spotify_track_t × 40 ≈ 22KB. */
    spotify_track_t *prev_q = NULL;
    size_t prev_n = s_queue_n < PLAYER_QUEUE_MAX ? s_queue_n : PLAYER_QUEUE_MAX;
    if (prev_n > 0) {
        prev_q = (spotify_track_t *)malloc(prev_n * sizeof(spotify_track_t));
        if (prev_q) {
            memcpy(prev_q, s_queue, prev_n * sizeof(spotify_track_t));
        } else {
            prev_n = 0;
        }
    }

    bool published = false;
    /* Publish Connect snapshot first so the ring shows wedges immediately.
     * Web enrich can stall while CDN audio holds Wi‑Fi — never gate UI on it. */
    if (n > 0) {
        for (size_t i = 0; i < n && i < PLAYER_QUEUE_MAX; i++) {
            copy_web_into_queue_slot(i, &tracks[i]);
        }
        s_queue_n = n;
        if (prev_q && prev_n > 0) {
            merge_prev_queue_meta(prev_q, prev_n);
        }
        s_queue_i = 0;
        if (!s_state.track.image_url[0] && s_queue[0].image_url[0]) {
            strncpy(s_state.track.image_url, s_queue[0].image_url,
                    sizeof(s_state.track.image_url) - 1);
        }
        if (s_queue[0].title[0] && !s_state.track.title[0]) {
            apply_track_to_state(&s_queue[0]);
        }
        published = true;
        notify();
        if (prev_sig) {
            queue_signature(prev_sig, 512);
        }
    }

    size_t with_art = 0;
    for (size_t i = 0; i < s_queue_n; i++) {
        if (s_queue[i].image_url[0]) {
            with_art++;
        }
    }

    if (allow_enrich && n > 0 && with_art < n) {
        if (spotify_connect_enrich_tracks(tracks, n)) {
            for (size_t i = 0; i < n && i < PLAYER_QUEUE_MAX; i++) {
                copy_web_into_queue_slot(i, &tracks[i]);
            }
            s_queue_n = n;
            if (prev_q && prev_n > 0) {
                merge_prev_queue_meta(prev_q, prev_n);
            }
        }
    }

    free(prev_q);
    free(tracks);

    bool changed = false;
    if (prev_sig && new_sig) {
        queue_signature(new_sig, 512);
        changed = (strcmp(prev_sig, new_sig) != 0);
    } else {
        changed = published;
    }
    free(prev_sig);
    free(new_sig);

    with_art = 0;
    for (size_t i = 0; i < s_queue_n; i++) {
        if (s_queue[i].image_url[0]) {
            with_art++;
        }
    }
    ESP_LOGI(TAG, "Synced play queue (%u tracks, %u with art, idx=%d, changed=%d)",
             (unsigned)s_queue_n, (unsigned)with_art, s_queue_i, (int)changed);
    for (size_t i = 0; i < s_queue_n && i < 8; i++) {
        ESP_LOGI(TAG, "  q[%u] art=%d id=%.22s title=%.28s url=%.40s", (unsigned)i,
                 s_queue[i].image_url[0] ? 1 : 0, s_queue[i].id, s_queue[i].title,
                 s_queue[i].image_url);
    }

    s_queue_sync_busy = false;
    if (changed) {
        notify();
    }

    /* Follow-ups if we still lack cover URLs (Web API can fail while audio runs). */
    if (n > 0 && with_art < n && s_sync_retries < 3) {
        s_sync_retries++;
        int retry_ms = 1500 + (int)s_sync_retries * 1500;
        s_queue_sync_busy = true;
        s_sync_busy_since = xTaskGetTickCount();
        if (!spawn_q_sync(retry_ms, true)) {
            s_queue_sync_busy = false;
        }
    } else if (n == 0 && s_sync_retries < 3) {
        /* Snapshot empty right after Load — retry once mercury preloads. */
        s_sync_retries++;
        s_queue_sync_busy = true;
        s_sync_busy_since = xTaskGetTickCount();
        if (!spawn_q_sync(1200, true)) {
            s_queue_sync_busy = false;
        }
    }
    vTaskDeleteWithCaps(NULL);
}

static void request_queue_sync(void)
{
    if (!spotify_connect_is_ready()) {
        return;
    }
    if (s_queue_sync_busy) {
        if ((xTaskGetTickCount() - s_sync_busy_since) > pdMS_TO_TICKS(20000)) {
            ESP_LOGW(TAG, "q_sync busy stuck >20s — unlocking");
            s_queue_sync_busy = false;
        } else {
            return;
        }
    }
    const char *key = s_state.track.title[0] ? s_state.track.title : s_state.track.id;
    if (key[0] && strcmp(s_last_sync_track, key) == 0 && s_queue_n > 1) {
        /* Already synced this track — only re-run if covers are incomplete. */
        size_t with_art = 0;
        for (size_t i = 0; i < s_queue_n; i++) {
            if (s_queue[i].image_url[0]) {
                with_art++;
            }
        }
        if (with_art >= s_queue_n || with_art >= (size_t)(PLAYER_RING_NEXT + 1)) {
            return;
        }
    }
    if (key[0]) {
        strncpy(s_last_sync_track, key, sizeof(s_last_sync_track) - 1);
    }
    s_sync_retries = 0;
    s_queue_sync_busy = true;
    s_sync_busy_since = xTaskGetTickCount();
    ESP_LOGI(TAG, "q_sync start (queue_n=%u key=%.24s)", (unsigned)s_queue_n, key);
    if (!spawn_q_sync(800, true)) {
        s_queue_sync_busy = false;
    }
}

void player_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.volume = 40;
    s_state.has_track = false;
    strncpy(s_state.track.title, "Nothing playing", sizeof(s_state.track.title) - 1);
    strncpy(s_state.track.artist, "Add a playlist in Settings", sizeof(s_state.track.artist) - 1);
    board_audio_set_volume(s_state.volume);
    spotify_connect_set_track_callback(on_cspot_track, NULL);
    spotify_connect_set_volume(s_state.volume);

    if (nvs_load_playlist(s_playlist_id, sizeof(s_playlist_id))) {
        ESP_LOGI(TAG, "Curated playlist id=%s", s_playlist_id);
    }
}

void player_set_state_callback(player_state_cb_t cb, void *user)
{
    s_cb = cb;
    s_cb_user = user;
}

const player_state_t *player_get_state(void)
{
    return &s_state;
}

void player_play_pause(void)
{
    if (spotify_connect_is_ready()) {
        spotify_connect_play_pause();
    }
}

void player_next(void)
{
    if (s_queue_n > 1) {
        player_play_queue_index(1);
    } else if (spotify_connect_is_ready()) {
        spotify_connect_next();
    }
}

void player_previous(void)
{
    if (spotify_connect_is_ready()) {
        spotify_connect_previous();
    }
}

void player_set_volume(uint8_t volume)
{
    if (volume > VOLUME_MAX_CHILD) {
        volume = VOLUME_MAX_CHILD;
    }
    s_state.volume = volume;
    board_audio_set_volume(volume);
    spotify_connect_set_volume(volume);
    notify();
}

bool player_set_curated_playlist(const char *playlist_id)
{
    if (!playlist_id || !playlist_id[0]) {
        return false;
    }
    /* Normalize open.spotify.com URLs to bare id in NVS. */
    const char *p = strstr(playlist_id, "playlist/");
    char bare[64] = {0};
    if (p) {
        p += 9;
        size_t n = 0;
        while (p[n] && p[n] != '?' && p[n] != '/' && n < sizeof(bare) - 1) {
            bare[n] = p[n];
            n++;
        }
    } else if (strncmp(playlist_id, "spotify:playlist:", 17) == 0) {
        strncpy(bare, playlist_id + 17, sizeof(bare) - 1);
    } else {
        strncpy(bare, playlist_id, sizeof(bare) - 1);
    }
    if (!bare[0]) {
        return false;
    }
    strncpy(s_playlist_id, bare, sizeof(s_playlist_id) - 1);
    nvs_save_playlist(s_playlist_id);
    return player_reload_curated();
}

void player_get_curated_playlist_id(char *buf, size_t buflen)
{
    if (!buf || buflen == 0) {
        return;
    }
    strncpy(buf, s_playlist_id, buflen - 1);
    buf[buflen - 1] = '\0';
}

bool player_reload_curated(void)
{
    if (!s_playlist_id[0] || !spotify_connect_is_ready()) {
        return false;
    }
    spotify_web_track_t *web =
        (spotify_web_track_t *)calloc(PLAYER_QUEUE_MAX, sizeof(spotify_web_track_t));
    if (!web) {
        return false;
    }
    size_t n = 0;
    if (!spotify_connect_web_playlist_tracks(s_playlist_id, web, PLAYER_QUEUE_MAX, &n) || n == 0) {
        ESP_LOGW(TAG, "Failed to load curated playlist");
        free(web);
        return false;
    }
    s_queue_n = n;
    s_queue_i = 0;
    for (size_t i = 0; i < n; i++) {
        copy_web_into_queue_slot(i, &web[i]);
    }
    free(web);
    apply_track_to_state(&s_queue[0]);
    s_state.playing = true;
    notify();
    start_play_offset(0);
    ESP_LOGI(TAG, "Curated queue loaded (%u tracks)", (unsigned)n);
    return true;
}

size_t player_queue_count(void)
{
    return s_queue_n;
}

int player_queue_index(void)
{
    return s_queue_i;
}

bool player_queue_get(int index, spotify_track_t *out)
{
    if (!out || s_queue_n == 0 || index < 0 || index >= (int)s_queue_n) {
        return false;
    }
    *out = s_queue[index];
    return true;
}

void player_play_queue_index(int index)
{
    if (s_queue_n == 0 || index < 0 || index >= (int)s_queue_n) {
        return;
    }
    if (index == 0) {
        return;
    }
    player_play_ring_choice(index, s_queue[index].id[0] ? s_queue[index].id : NULL);
}

void player_play_ring_choice(int offset, const char *track_id)
{
    if (s_queue_n == 0 || offset < 1 || offset >= (int)s_queue_n) {
        return;
    }
    const char *id = track_id && track_id[0] ? track_id : s_queue[offset].id;
    ESP_LOGI(TAG, "play ring offset=%d id=%.22s title=%.28s", offset, id,
             s_queue[offset].title);
    /* Optimistic UI from the slot we know about (may still be the tapped cover). */
    if (id[0] && strcmp(s_queue[offset].id, id) == 0) {
        apply_track_to_state(&s_queue[offset]);
    } else {
        for (size_t i = 0; i < s_queue_n; i++) {
            if (s_queue[i].id[0] && id[0] && strcmp(s_queue[i].id, id) == 0) {
                apply_track_to_state(&s_queue[i]);
                break;
            }
        }
    }
    s_state.playing = true;
    s_state.position_ms = 0;
    s_queue_i = 0;
    notify();

    bool ok = spotify_connect_play_ring_slot(offset, id[0] ? id : NULL);
    if (!ok && id[0]) {
        ESP_LOGW(TAG, "ring slot failed — Web API play %.22s", id);
        start_play_track(id);
    }
    request_queue_sync();
}

bool player_is_ready(void)
{
    return spotify_connect_is_ready();
}
