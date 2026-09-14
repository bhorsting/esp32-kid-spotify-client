#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_NAME "Marten"
#define APP_DEVICE_NAME "Marten Player"

#define VOLUME_MAX_CHILD 70

typedef enum {
    APP_SCREEN_RING = 0,
    APP_SCREEN_SETTINGS,
    APP_SCREEN_COUNT
} app_screen_t;

#ifdef __cplusplus
}
#endif
