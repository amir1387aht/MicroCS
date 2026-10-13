/* Explicit cross-build test config only. Tune/audit production memory and features separately. */
#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_OBJ 1
#define LV_USE_ASSERT_STYLE 1
#define LV_USE_ASSERT_MEM_INTEGRITY 1
/* LVGL8 only supports deinit with its built-in allocator (or LV_ENABLE_GC). */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (64U * 1024U)
#define LV_COLOR_16_SWAP 0
#define LV_TICK_CUSTOM 0
#endif
