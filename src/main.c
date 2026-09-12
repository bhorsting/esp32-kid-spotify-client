#include "board/display.h"
#include "board/audio.h"
#include "spotify/player.h"
#include "ui/ui.h"
#include "app_config.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "marten";

static void ui_task(void *arg)
{
    (void)arg;
    while (1) {
        board_display_tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "%s starting (Spotify local player, kid UI)", APP_NAME);

    board_audio_init();
    player_init();

    if (!board_display_init()) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }

    ui_init();
    xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "UI up — stub player; cspot integration pending");
}
