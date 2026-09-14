#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** PCM5101 I2S sink for cspot (44.1 kHz stereo 16-bit). */
bool board_audio_init(void);
void board_audio_feed_pcm(const uint8_t *data, size_t len);
void board_audio_set_volume(uint8_t percent);

#ifdef __cplusplus
}
#endif
