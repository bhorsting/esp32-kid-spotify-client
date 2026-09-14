#pragma once

/**
 * Thin wrappers — prefer spotify_bridge.h (cspot Connect).
 */
#include "spotify_bridge.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline bool cspot_bridge_start(const char *username, const char *credentials_blob)
{
    (void)username;
    (void)credentials_blob;
    spotify_connect_start();
    return true;
}

static inline void cspot_bridge_stop(void) {}

static inline bool cspot_bridge_is_running(void)
{
    return spotify_connect_is_ready();
}

#ifdef __cplusplus
}
#endif
