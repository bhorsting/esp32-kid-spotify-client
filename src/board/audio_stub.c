/**
 * PCM5101 I2S stub. Real impl: i2s_std on DIN=47 LRCK=38 BCK=48 @ 44.1 kHz.
 */
#include "board/audio.h"
#include "board_pins.h"

#include <stdio.h>

bool board_audio_init(void)
{
    printf("[board] audio stub — PCM5101 I2S DIN=%d LRCK=%d BCK=%d\n",
           PIN_I2S_DOUT, PIN_I2S_LRCK, PIN_I2S_BCK);
    return true;
}

void board_audio_feed_pcm(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
}

void board_audio_set_volume(uint8_t percent)
{
    (void)percent;
}
