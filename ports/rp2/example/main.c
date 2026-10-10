/* MicroCS on a Raspberry Pi Pico / Pico 2: the whole firmware is this file.
 * Connect a serial terminal (or MicroCS Studio) to the USB port and type C#
 * at the "> " prompt. Scripts and files live in LittleFS on the on-board
 * flash (the last 1 MB on a Pico, 3 MB on a Pico 2) and survive resets and
 * firmware updates; build with -DMICROCS_FS=yaffs2 for YAFFS2 or
 * -DMICROCS_FS= for a RAM disk. With -DMICROCS_OS=freertos (RP2040) the runtime runs
 * in a FreeRTOS SMP task on core 0 and C# threads can use core 1 (docs/THREADS.md). */
#include <stdio.h>
#include "pico/stdlib.h"
#include "mcs_runtime.h"
#include "mcs_port_rp2.h"

#if MCS_OS == MCS_OS_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
static uint8_t heap[128 * 1024] __attribute__((aligned(8)));   /* REPL VM + thread heaps */
#else
static uint8_t heap[160 * 1024] __attribute__((aligned(8)));
#endif
static mcs_runtime_t rt;
static mcs_hal_t hal;
#ifdef MCS_HAVE_FLASHFS
static mcs_rp2_flash_t flash;
static mcs_flashfs_t flashfs;
#endif

static void microcs_main(void) {
    mcs_rp2_cfg_t pins = MCS_RP2_CFG_DEFAULT;
#if PICO_RP2350
    pins.name = "Raspberry Pi Pico 2";
#else
    pins.name = "Raspberry Pi Pico";
#endif
    mcs_rp2_hal_init(&hal, &pins);

    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap;
    cfg.heap_size = sizeof heap;
#ifdef MCS_HAVE_FLASHFS
    if (mcs_rp2_flash_init(&flash, 0, 0) == 0 &&
        mcs_flashfs_mount(&flashfs, &flash.flash, 0, 0, MCS_FLASHFS_DEFAULT, MCS_FLASHFS_FORMAT_IF_NEEDED) == 0) {
        cfg.fs_ops = flashfs.ops;                /* files on flash */
        cfg.fs_ctx = flashfs.ctx;
    } else
#endif
    {
        cfg.ramfs_size = 32 * 1024;              /* no flash filesystem: RAM disk, lost on reset */
    }
    cfg.console = mcs_rp2_console_stdio();
    cfg.ticks = mcs_rp2_ticks;
    cfg.delay = mcs_rp2_delay;
    cfg.hal = &hal;
    for (;;) mcs_runtime_run(&rt, &cfg);         /* restarts if the session ends */
}

#if MCS_OS == MCS_OS_FREERTOS
static void microcs_task(void* arg) { (void)arg; microcs_main(); }
int main(void) {
    stdio_init_all();
    TaskHandle_t t;
    xTaskCreate(microcs_task, "microcs", 16 * 1024 / sizeof(StackType_t), NULL, 1, &t);
    vTaskCoreAffinitySet(t, 1u << 0);           /* REPL on core 0, core 1 for C# threads */
    vTaskStartScheduler();
    for (;;) {}
}
#else
int main(void) {
    stdio_init_all();
    microcs_main();
}
#endif
