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
    /** Absolute Connect queue index when snapshotted (-1 if unknown). */
    int connect_index;
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

#define PLAYER_QUEUE_MAX 40
#define PLAYER_RING_NEXT 5

void player_init(void);
void player_set_state_callback(player_state_cb_t cb, void *user);
const player_state_t *player_get_state(void);

void player_play_pause(void);
void player_next(void);
void player_previous(void);
void player_set_volume(uint8_t volume);

/** Curated playlist (parent-shared). ID bare or spotify:playlist:… */
bool player_set_curated_playlist(const char *playlist_id);
void player_get_curated_playlist_id(char *buf, size_t buflen);
bool player_reload_curated(void);

/** Ring model: current + up to 5 following tracks (wrap). */
size_t player_queue_count(void);
int player_queue_index(void);
bool player_queue_get(int index, spotify_track_t *out);
/** Jump to ring queue offset (1=next…) and start playback. */
void player_play_queue_index(int index);
/** Play the track shown on a wedge: prefer `track_id` inside the live ring window. */
void player_play_ring_choice(int offset, const char *track_id);

bool player_is_ready(void);

#ifdef __cplusplus
}
#endif
