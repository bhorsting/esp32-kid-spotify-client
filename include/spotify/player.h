#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char id[64];
    char title[96];
    char artist[96];
    char album[96];
    char image_url[192];
    bool liked;
    uint32_t duration_ms;
} spotify_track_t;

typedef struct {
    char id[64];
    char name[96];
    char image_url[192];
} spotify_playlist_t;

typedef struct {
    spotify_track_t track;
    bool playing;
    bool has_track;
    uint8_t volume; /* 0–100 */
    uint32_t position_ms;
} player_state_t;

typedef void (*player_state_cb_t)(const player_state_t *state, void *user);
typedef void (*player_search_cb_t)(const spotify_track_t *results, size_t count, void *user);

void player_init(void);
void player_set_state_callback(player_state_cb_t cb, void *user);

const player_state_t *player_get_state(void);

void player_play_pause(void);
void player_next(void);
void player_previous(void);
void player_set_volume(uint8_t volume);
void player_toggle_like(void);
void player_play_track(const char *track_id);
void player_play_playlist(const char *playlist_id);

void player_search(const char *query, player_search_cb_t cb, void *user);
void player_get_favourites(player_search_cb_t cb, void *user);
void player_get_browse_playlists(spotify_playlist_t *out, size_t max, size_t *count);

bool player_is_ready(void);

#ifdef __cplusplus
}
#endif
