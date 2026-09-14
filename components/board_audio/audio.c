/**
 * PCM5101 I2S output — 44.1 kHz stereo 16-bit (cspot PCM).
 */
#include "board/audio.h"
#include "board_pins.h"

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "audio";

static i2s_chan_handle_t s_tx;
static uint8_t s_volume = 70;
static SemaphoreHandle_t s_lock;

bool board_audio_init(void)
{
    if (s_tx) {
        return true;
    }
    s_lock = xSemaphoreCreateMutex();

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_FALSE(i2s_new_channel(&chan_cfg, &s_tx, NULL) == ESP_OK, false, TAG,
                        "i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = PIN_I2S_BCK,
                .ws = PIN_I2S_LRCK,
                .dout = PIN_I2S_DOUT,
                .din = I2S_GPIO_UNUSED,
                .invert_flags =
                    {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false,
                    },
            },
    };

    ESP_RETURN_ON_FALSE(i2s_channel_init_std_mode(s_tx, &std_cfg) == ESP_OK, false, TAG,
                        "i2s_channel_init_std_mode failed");
    ESP_RETURN_ON_FALSE(i2s_channel_enable(s_tx) == ESP_OK, false, TAG, "i2s_channel_enable failed");

    ESP_LOGI(TAG, "PCM5101 I2S ready DIN=%d LRCK=%d BCK=%d @ 44.1 kHz", PIN_I2S_DOUT, PIN_I2S_LRCK,
             PIN_I2S_BCK);
    return true;
}

void board_audio_set_volume(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    s_volume = percent;
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

void board_audio_feed_pcm(const uint8_t *data, size_t len)
{
    if (!s_tx || !data || len == 0) {
        return;
    }

    /* Volume is applied by cspot sink (0–65535). Write raw PCM here. */
    size_t written = 0;
    while (written < len) {
        size_t chunk = 0;
        esp_err_t err = i2s_channel_write(s_tx, data + written, len - written, &chunk, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s write failed: %s", esp_err_to_name(err));
            break;
        }
        written += chunk;
    }
}
