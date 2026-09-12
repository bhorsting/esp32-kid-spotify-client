/**
 * Display/touch stub — UI builds without hardware.
 * Replace with Waveshare ST77916 + CST816 + TCA9554 bring-up when flashing.
 */
#include "board/display.h"
#include "board_pins.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#ifndef MARTEN_HOST_SIM
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_buf1[DISPLAY_WIDTH * 20];
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    (void)drv;
    (void)area;
    (void)color_p;
    /* Hardware: push area to ST77916 QSPI framebuffer */
    lv_disp_flush_ready(drv);
}

static void touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->state = LV_INDEV_STATE_RELEASED;
    /* Hardware: read CST816 point */
}

#ifndef MARTEN_HOST_SIM
static void lv_tick_task(void *arg)
{
    (void)arg;
    lv_tick_inc(5);
}
#endif

bool board_display_init(void)
{
    lv_init();

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, NULL, DISPLAY_WIDTH * 20);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = DISPLAY_WIDTH;
    s_disp_drv.ver_res = DISPLAY_HEIGHT;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_cb;
    lv_indev_drv_register(&s_indev_drv);

#ifndef MARTEN_HOST_SIM
    const esp_timer_create_args_t tick_args = {
        .callback = &lv_tick_task,
        .name = "lv_tick",
    };
    esp_timer_handle_t tick;
    esp_timer_create(&tick_args, &tick);
    esp_timer_start_periodic(tick, 5 * 1000);
#endif

    printf("[board] display stub ready (%dx%d) — wire ST77916/CST816 next\n",
           DISPLAY_WIDTH, DISPLAY_HEIGHT);
    return true;
}

void board_display_tick(void)
{
    lv_timer_handler();
}
