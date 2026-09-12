/**
 * Mock / stub Spotify player.
 * Swap player_stub.cpp for cspot + Web API implementation when hardware is wired.
 */
#include "spotify/player.h"
#include "app_config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool contains_ci(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle) {
        return false;
    }
    for (const char *h = hay; *h; h++) {
        const char *a = h;
        const char *b = needle;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b) {
            return true;
        }
    }
    return false;
}

static player_state_t s_state;
static player_state_cb_t s_cb;
static void *s_cb_user;

static const spotify_track_t k_demo_tracks[] = {
    {"t1", "Yellow Submarine", "The Beatles", "Revolver", "", true, 158000},
    {"t2", "Here Comes the Sun", "The Beatles", "Abbey Road", "", true, 185000},
    {"t3", "Octopus's Garden", "The Beatles", "Abbey Road", "", false, 170000},
    {"t4", "Three Little Birds", "Bob Marley", "Exodus", "", true, 180000},
    {"t5", "What a Wonderful World", "Louis Armstrong", "Single", "", false, 140000},
    {"t6", "Somewhere Over the Rainbow", "Israel Kamakawiwo'ole", "Facing Future", "", true, 213000},
    {"t7", "Baby Shark", "Pinkfong", "Animal Songs", "", false, 90000},
    {"t8", "Let It Go", "Idina Menzel", "Frozen", "", true, 223000},
};

static const spotify_playlist_t k_demo_playlists[] = {
    {"p1", "Bedtime Calm", ""},
    {"p2", "Dance Party", ""},
    {"p3", "Story Songs", ""},
    {"p4", "Mum Picked These", ""},
};

static void notify(void)
{
    if (s_cb) {
        s_cb(&s_state, s_cb_user);
    }
}

void player_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.volume = 40;
    s_state.has_track = true;
    s_state.playing = false;
    s_state.track = k_demo_tracks[0];
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
    if (!s_state.has_track) {
        return;
    }
    s_state.playing = !s_state.playing;
    notify();
}

void player_next(void)
{
    static size_t idx = 0;
    idx = (idx + 1) % (sizeof(k_demo_tracks) / sizeof(k_demo_tracks[0]));
    s_state.track = k_demo_tracks[idx];
    s_state.has_track = true;
    s_state.playing = true;
    s_state.position_ms = 0;
    notify();
}

void player_previous(void)
{
    static size_t idx = 0;
    if (idx == 0) {
        idx = (sizeof(k_demo_tracks) / sizeof(k_demo_tracks[0])) - 1;
    } else {
        idx--;
    }
    s_state.track = k_demo_tracks[idx];
    s_state.has_track = true;
    s_state.playing = true;
    s_state.position_ms = 0;
    notify();
}

void player_set_volume(uint8_t volume)
{
    if (volume > VOLUME_MAX_CHILD) {
        volume = VOLUME_MAX_CHILD;
    }
    s_state.volume = volume;
    notify();
}

void player_toggle_like(void)
{
    if (!s_state.has_track) {
        return;
    }
    s_state.track.liked = !s_state.track.liked;
    notify();
}

void player_play_track(const char *track_id)
{
    if (!track_id) {
        return;
    }
    for (size_t i = 0; i < sizeof(k_demo_tracks) / sizeof(k_demo_tracks[0]); i++) {
        if (strcmp(k_demo_tracks[i].id, track_id) == 0) {
            s_state.track = k_demo_tracks[i];
            s_state.has_track = true;
            s_state.playing = true;
            s_state.position_ms = 0;
            notify();
            return;
        }
    }
}

void player_play_playlist(const char *playlist_id)
{
    (void)playlist_id;
    s_state.track = k_demo_tracks[1];
    s_state.has_track = true;
    s_state.playing = true;
    s_state.position_ms = 0;
    notify();
}

void player_search(const char *query, player_search_cb_t cb, void *user)
{
    if (!cb) {
        return;
    }
    if (!query || strlen(query) < SEARCH_MIN_CHARS) {
        cb(NULL, 0, user);
        return;
    }

    spotify_track_t hits[8];
    size_t n = 0;
    for (size_t i = 0; i < sizeof(k_demo_tracks) / sizeof(k_demo_tracks[0]) && n < 8; i++) {
        if (contains_ci(k_demo_tracks[i].title, query) ||
            contains_ci(k_demo_tracks[i].artist, query) ||
            contains_ci(k_demo_tracks[i].album, query)) {
            hits[n++] = k_demo_tracks[i];
        }
    }
    cb(hits, n, user);
}

void player_get_favourites(player_search_cb_t cb, void *user)
{
    if (!cb) {
        return;
    }
    spotify_track_t hits[8];
    size_t n = 0;
    for (size_t i = 0; i < sizeof(k_demo_tracks) / sizeof(k_demo_tracks[0]) && n < 8; i++) {
        if (k_demo_tracks[i].liked) {
            hits[n++] = k_demo_tracks[i];
        }
    }
    cb(hits, n, user);
}

void player_get_browse_playlists(spotify_playlist_t *out, size_t max, size_t *count)
{
    size_t n = sizeof(k_demo_playlists) / sizeof(k_demo_playlists[0]);
    if (n > max) {
        n = max;
    }
    for (size_t i = 0; i < n; i++) {
        out[i] = k_demo_playlists[i];
    }
    if (count) {
        *count = n;
    }
}

bool player_is_ready(void)
{
    return true; /* stub always ready; real impl waits for cspot + Wi-Fi */
}
