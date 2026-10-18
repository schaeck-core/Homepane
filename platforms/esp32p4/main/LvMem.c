// LVGL's memory (LV_USE_CUSTOM_MALLOC): internal RAM while there is enough
// of it, else PSRAM.
//
// LVGL's objects, styles and texts are small, so plain malloc() put them
// all into internal RAM (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL). The dashboard
// is fast there, but the music pages (hundreds of objects) took it down to a
// few KB, and Wi-Fi, lwIP, HTTP and the voice pipeline need it: the panel
// crashed while the search page loaded its covers. Below INTERNAL_KEEP free
// bytes LVGL now allocates from PSRAM (cached, a little slower). 80 KB: the
// dashboard alone used to take internal RAM down to that already, and below
// it Wi-Fi's buffers (a stream of covers) ran out (2026-10-18).

#include "lvgl.h"

#include "esp_heap_caps.h"
#include "esp_memory_utils.h"

#include <stdlib.h>

#define INTERNAL_KEEP (80 * 1024)

static bool internal_low(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < INTERNAL_KEEP;
}

void lv_mem_init(void) {}

void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t size)
{
    if (internal_low()) {
        void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
    }
    return malloc(size);
}

// What is in PSRAM stays there: plain realloc() moves blocks below 16 KB to
// internal RAM, and LVGL grows its style and child arrays by realloc, so
// every new object took internal RAM again (a music library page of covers
// ~5 KB, until Wi-Fi had no buffer left).
void *lv_realloc_core(void *p, size_t new_size)
{
    if (!p) return lv_malloc_core(new_size);
    if (esp_ptr_external_ram(p) || internal_low()) {
        void *q = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (q) return q;
    }
    return realloc(p, new_size);
}

void lv_free_core(void *p)
{
    free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    LV_UNUSED(mon_p);
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}
