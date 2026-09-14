#include "board/display.h"
#include "board/audio.h"
#include "board/sdcard.h"
#include "net/wifi_manager.h"
#include "spotify/player.h"
#include "spotify_bridge.h"
#include "ui/ui.h"
#include "app_config.h"

#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "marten";

static void ui_task(void *arg)
{
    (void)arg;
    ui_init();
    ESP_LOGI(TAG, "UI up");
    while (1) {
        ui_poll_refresh();
        board_display_tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void net_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    wifi_manager_start();

    bool started = false;
    bool playlist_loaded = false;
    while (1) {
        if (!started && wifi_manager_is_connected()) {
            spotify_connect_start();
            started = true;
            ESP_LOGI(TAG, "Spotify Connect advertising as '%s'", APP_DEVICE_NAME);
            ui_request_refresh();
        }
        if (started && !playlist_loaded && spotify_connect_is_ready()) {
            char pl[64] = {0};
            player_get_curated_playlist_id(pl, sizeof(pl));
            if (pl[0] && player_reload_curated()) {
                playlist_loaded = true;
                ESP_LOGI(TAG, "Curated playlist ready");
                ui_request_refresh();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s starting (8MB PSRAM free=%u)", APP_NAME,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    if (!wifi_manager_init()) {
        ESP_LOGE(TAG, "wifi init failed");
        return;
    }
    if (!board_audio_init()) {
        ESP_LOGE(TAG, "audio init failed");
    }
    if (!board_display_init()) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    if (!board_sd_init()) {
        ESP_LOGW(TAG, "TF card not mounted — covers stay in RAM only");
    }

    player_init();

    const uint32_t ui_stack = 32 * 1024;
    StackType_t *stack = heap_caps_malloc(ui_stack, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    StaticTask_t *tcb = heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (stack && tcb) {
        xTaskCreateStaticPinnedToCore(ui_task, "ui", ui_stack / sizeof(StackType_t), NULL, 6, stack,
                                      tcb, 1);
    } else {
        ESP_LOGW(TAG, "PSRAM ui stack alloc failed — falling back");
        xTaskCreatePinnedToCore(ui_task, "ui", 12288, NULL, 6, NULL, 1);
    }
    xTaskCreatePinnedToCore(net_task, "net", 8192, NULL, 4, NULL, 0);
}
