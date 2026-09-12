#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bring up TCA9554, backlight, ST77916 QSPI, CST816, LVGL flush/indev. */
bool board_display_init(void);

/** Feed LVGL timers; call from main loop / task. */
void board_display_tick(void);

#ifdef __cplusplus
}
#endif
