/**
 * Waveshare ESP32-S3-Touch-LCD-1.85 display bring-up:
 * I2C + TCA9554 → ST77916 QSPI → CST816 touch → LVGL flush/indev.
 */
#include "board/display.h"

#include "I2C_Driver.h"
#include "TCA9554PWR.h"
#include "ST77916.h"
#include "CST816.h"
#include "esp_lcd_touch.h"

#include <assert.h>
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "display";

#define LVGL_BUF_LEN                 (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT / 20)
#define EXAMPLE_LVGL_TICK_PERIOD_MS  2

static lv_disp_draw_buf_t s_disp_buf;
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

static void lvgl_touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t touchpad_x[5] = {0};
    uint16_t touchpad_y[5] = {0};
    uint8_t touchpad_cnt = 0;

    esp_lcd_touch_read_data(drv->user_data);
    bool pressed = esp_lcd_touch_get_coordinates(drv->user_data, touchpad_x, touchpad_y, NULL,
                                                 &touchpad_cnt, 5);

    if (pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x[0];
        data->point.y = touchpad_y[0];
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

static void lvgl_port_update_cb(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;

    switch (drv->rotated) {
    case LV_DISP_ROT_NONE:
        esp_lcd_panel_swap_xy(panel, false);
        esp_lcd_panel_mirror(panel, true, false);
        break;
    case LV_DISP_ROT_90:
        esp_lcd_panel_swap_xy(panel, true);
        esp_lcd_panel_mirror(panel, true, true);
        break;
    case LV_DISP_ROT_180:
        esp_lcd_panel_swap_xy(panel, false);
        esp_lcd_panel_mirror(panel, false, true);
        break;
    case LV_DISP_ROT_270:
        esp_lcd_panel_swap_xy(panel, true);
        esp_lcd_panel_mirror(panel, false, false);
        break;
    }
}

static bool lvgl_port_init(void)
{
    ESP_LOGI(TAG, "Initialize LVGL");
    lv_init();

    /* ~13KB each — keep in internal RAM so flush isn't stalled on PSRAM reads. */
    lv_color_t *buf1 = heap_caps_malloc(LVGL_BUF_LEN * sizeof(lv_color_t),
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_color_t *buf2 = heap_caps_malloc(LVGL_BUF_LEN * sizeof(lv_color_t),
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf1 || !buf2) {
        if (buf1) {
            heap_caps_free(buf1);
        }
        if (buf2) {
            heap_caps_free(buf2);
        }
        buf1 = heap_caps_malloc(LVGL_BUF_LEN * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        buf2 = heap_caps_malloc(LVGL_BUF_LEN * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "draw buffers alloc failed");
        return false;
    }
    lv_disp_draw_buf_init(&s_disp_buf, buf1, buf2, LVGL_BUF_LEN);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = EXAMPLE_LCD_WIDTH;
    s_disp_drv.ver_res = EXAMPLE_LCD_HEIGHT;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.drv_update_cb = lvgl_port_update_cb;
    s_disp_drv.draw_buf = &s_disp_buf;
    s_disp_drv.user_data = panel_handle;
    lv_disp_t *disp = lv_disp_drv_register(&s_disp_drv);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.disp = disp;
    s_indev_drv.read_cb = lvgl_touchpad_read;
    s_indev_drv.user_data = tp;
    lv_indev_drv_register(&s_indev_drv);

    const esp_timer_create_args_t tick_args = {
        .callback = &lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    return true;
}

bool board_display_init(void)
{
    I2C_Init();
    ESP_ERROR_CHECK(EXIO_Init());
    LCD_Init();

    if (!panel_handle) {
        ESP_LOGE(TAG, "LCD panel handle is NULL");
        return false;
    }
    if (!tp) {
        ESP_LOGE(TAG, "Touch handle is NULL");
        return false;
    }
    if (!lvgl_port_init()) {
        return false;
    }

    ESP_LOGI(TAG, "display ready (%dx%d ST77916 + CST816)", EXAMPLE_LCD_WIDTH, EXAMPLE_LCD_HEIGHT);
    return true;
}

void board_display_tick(void)
{
    lv_timer_handler();
}
