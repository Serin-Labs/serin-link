/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "sdkconfig.h"
#if CONFIG_LV_USE_CUSTOM_MALLOC

#include "lvgl.h"
#include "esp_heap_caps.h"

#define LV_PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) { (void)mem; (void)bytes; return NULL; }
void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }

void *lv_malloc_core(size_t size)
{
    void *p = heap_caps_malloc(size, LV_PSRAM_CAPS);
    return p ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    void *q = heap_caps_realloc(p, new_size, LV_PSRAM_CAPS);
    return q ? q : heap_caps_realloc(p, new_size, MALLOC_CAP_8BIT);
}

void lv_free_core(void *p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    multi_heap_info_t hi;
    heap_caps_get_info(&hi, LV_PSRAM_CAPS);
    mon->total_size = hi.total_free_bytes + hi.total_allocated_bytes;
    mon->free_size  = hi.total_free_bytes;
    mon->free_biggest_size = hi.largest_free_block;
    mon->free_cnt   = hi.free_blocks;
    mon->used_cnt   = hi.allocated_blocks;
    mon->max_used   = mon->total_size - hi.minimum_free_bytes;
    mon->used_pct   = mon->total_size ? (uint8_t)(100 - (100 * mon->free_size) / mon->total_size) : 0;
    mon->frag_pct   = mon->free_size ? (uint8_t)(100 - (100 * mon->free_biggest_size) / mon->free_size) : 0;
}

lv_result_t lv_mem_test_core(void)
{
    void *p = lv_malloc_core(64);
    if (!p) return LV_RESULT_INVALID;
    lv_free_core(p);
    return LV_RESULT_OK;
}

#endif
