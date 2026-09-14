#pragma once

#include <stddef.h>

#include "esp_heap_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Prefer internal SRAM for small LVGL objects; fall back to PSRAM. */
static inline void *marten_lv_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) {
        p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return p;
}

static inline void marten_lv_free(void *ptr)
{
    heap_caps_free(ptr);
}

static inline void *marten_lv_realloc(void *ptr, size_t size)
{
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p && size) {
        p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return p;
}

#ifdef __cplusplus
}
#endif
