#pragma once

/**
 * Future cspot glue — implement against feelfreelinux/cspot AudioSink.
 * Feed PCM via board_audio_feed_pcm(); push metadata into player_* state.
 */
#include "board/audio.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool cspot_bridge_start(const char *username, const char *credentials_blob);
void cspot_bridge_stop(void);
bool cspot_bridge_is_running(void);

#ifdef __cplusplus
}
#endif
