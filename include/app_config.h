#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_NAME "Marten"
#define APP_DEVICE_NAME "Marten Player"

/* Child UX */
#define PARENT_PIN_DEFAULT "2468"
#define VOLUME_MAX_CHILD 70 /* percent */
#define SEARCH_MIN_CHARS 2

/* Spotify / cspot — credentials live in NVS after parent setup */
#define SPOTIFY_CACHE_PARTITION "sp_cache"

typedef enum {
    APP_SCREEN_NOW_PLAYING = 0,
    APP_SCREEN_BROWSE,
    APP_SCREEN_FAVOURITES,
    APP_SCREEN_SEARCH,
    APP_SCREEN_SETTINGS,
    APP_SCREEN_COUNT
} app_screen_t;

#ifdef __cplusplus
}
#endif
