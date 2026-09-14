#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void spotify_connect_start(void);

void spotify_connect_set_volume(uint8_t percent);
bool spotify_connect_is_playing(void);
bool spotify_connect_is_ready(void);
bool spotify_connect_is_paired(void);

void spotify_connect_play_pause(void);
void spotify_connect_next(void);
void spotify_connect_previous(void);

typedef void (*spotify_track_cb_t)(const char *title, const char *artist, const char *album,
                                   const char *track_id, const char *image_url,
                                   uint32_t duration_ms, bool playing, void *user);
void spotify_connect_set_track_callback(spotify_track_cb_t cb, void *user);

bool spotify_connect_get_device_id(char *buf, size_t buflen);
bool spotify_connect_get_access_token(char *buf, size_t buflen);

bool spotify_connect_play_track_id(const char *track_id);
/** Jump +offset tracks from the currently playing Connect queue index (wraps). */
bool spotify_connect_jump_relative(int offset);
/** Jump to absolute Connect queue index. */
bool spotify_connect_jump_to_index(int index);
/**
 * Play a ring wedge: `offset` is snapshot index (1=next…), `expected_id` is the
 * track id shown on that cover. Resolves against a FRESH ring snapshot only
 * (never searches the whole playlist — that picked wrong duplicates).
 */
bool spotify_connect_play_ring_slot(int offset, const char *expected_id);
/** Web-API fallback play (uris); prefer spotify_connect_play_track_id. */
bool spotify_connect_play_track_id_web(const char *track_id);
bool spotify_connect_play_playlist_id(const char *playlist_id);
/** Play playlist starting at zero-based track offset. */
bool spotify_connect_play_playlist_offset(const char *playlist_id, int offset);

typedef struct {
    char id[64];
    char title[96];
    char artist[96];
    char album[96];
    char image_url[192];
    bool liked;
    uint32_t duration_ms;
    /** Absolute Connect queue index (-1 if unknown / Web-API only). */
    int connect_index;
} spotify_web_track_t;

bool spotify_connect_web_playlist_tracks(const char *playlist_id, spotify_web_track_t *out,
                                         size_t max, size_t *count);

/**
 * Fetch Spotify's now-playing queue (currently_playing + upcoming).
 * currently may be NULL if unused; upcoming filled with up to max tracks.
 */
bool spotify_connect_web_player_queue(spotify_web_track_t *currently, spotify_web_track_t *upcoming,
                                      size_t max_upcoming, size_t *upcoming_count);

/** Batch-fetch track metadata by Spotify IDs (comma-separated base62, max ~20). */
bool spotify_connect_web_tracks_by_ids(const char *ids_csv, spotify_web_track_t *out, size_t max,
                                       size_t *count);

/** Fill missing title/image fields via Web API (esp_http_client). */
bool spotify_connect_enrich_tracks(spotify_web_track_t *tracks, size_t n);

/**
 * Ring queue from the active Connect session (current + following), with cover URLs.
 * Prefer this over /me/player/queue — that API often has no playlist continuation.
 */
bool spotify_connect_ring_tracks(spotify_web_track_t *out, size_t max, size_t *count);

#ifdef __cplusplus
}
#endif
