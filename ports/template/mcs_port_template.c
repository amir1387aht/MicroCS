/*
 * MicroCS port template. Copy this file into your firmware, fill in the
 * five TODOs and call app_script_start() from your main / a task.
 *
 * Bare metal:  write_fn -> UART TX, ticks_fn -> SysTick counter
 * FreeRTOS:    ticks -> xTaskGetTickCount()*portTICK_PERIOD_MS,
 *              delay -> vTaskDelay(pdMS_TO_TICKS(ms)), hook -> taskYIELD()
 * Zephyr:      ticks -> k_uptime_get_32(), delay -> k_msleep(ms)
 */
#include "mcs.h"
#include "mcs_bind.h"

/* ---- 1. memory: a dedicated pool keeps the script from fragmenting the system heap.
 * Measured: mcs_new() with the full stdlib takes ~50 KB on Cortex-M; add the
 * script's working set (images) or ~100 KB more if scripts are compiled on device. */
static uint8_t script_heap[96 * 1024] __attribute__((aligned(8)));
static mcs_pool_t script_pool;

/* ---- 2. console output */
static void port_write(void* ud, const char* s, size_t n) {
    (void)ud;
    while (n--) { /* TODO: uart_putc(*s++); */ s++; }
}
/* ---- 3. time */
static uint32_t port_ticks(void* ud) { (void)ud; return 0; /* TODO: millisecond counter */ }
static void port_delay(void* ud, uint32_t ms) { (void)ud; (void)ms; /* TODO: sleep / busy-wait */ }
/* ---- 4. periodic hook: watchdog, Ctrl-C, cooperative scheduling */
static volatile int stop_request;
static int port_hook(mcs_vm_t* vm, void* ud) { (void)vm; (void)ud; /* TODO: wdt_feed(); */ return stop_request; }

/* ---- 5. bindings: wrap your HAL */
/* extern void board_led_set(long on);  MCS_WRAP_V_I(w_led_set, board_led_set) */
static const mcs_reg_t board_fns[] = {
    /* MCS_FN("Led", w_led_set, 1), */
    MCS_REG_END
};

/* the application, compiled on the PC with:  mcs -C app.cs -n app_image -o app_image.h */
/* #include "app_image.h" */

int app_script_start(void) {
    mcs_pool_init(&script_pool, script_heap, sizeof script_heap);
    mcs_config_t cfg;
    mcs_config_default(&cfg);
    cfg.realloc_fn = mcs_pool_realloc;
    cfg.alloc_ud = &script_pool;
    cfg.heap_limit = sizeof script_heap - 2048;
    cfg.write_fn = port_write;
    cfg.ticks_fn = port_ticks;
    cfg.delay_fn = port_delay;
    cfg.hook_fn = port_hook;
    cfg.stack_slots = 192;
    cfg.max_frames = 32;
    cfg.stdlib = MCS_LIB_ALL;      /* or e.g. MCS_LIB_CORE | MCS_LIB_MATH */

    mcs_vm_t* vm = mcs_new(&cfg);
    if (!vm) return -1;
    mcs_register_module(vm, "Board", board_fns);

    mcs_result_t r = MCS_OK;
    /* r = mcs_exec_image(vm, app_image, app_image_len);           precompiled */
    /* r = mcs_exec_source(vm, "app.cs", source_from_flash_fs);    on-device compile */
    /* while (r == MCS_OK) r = mcs_call(vm, "App.Loop", 0, NULL, NULL);  main loop */
    mcs_free(vm);
    return r == MCS_OK ? 0 : -1;
}
