/* MicroCS as the whole firmware on any ESP32-family chip: a C# REPL / shell on
 * the serial console (UART0 and, where the chip has it, USB-Serial-JTAG - so it
 * works on either USB connector), the GPIO/ADC/PWM/... classes and a LittleFS
 * (or YAFFS2: menuconfig > MicroCS > Filesystem) filesystem on the flash
 * partition "storage" (partitions.csv): uploaded
 * scripts, /boot.cs and /main.cs survive resets and power cycles. To give
 * scripts UART1, I2C, SPI, PWM, I2S or CAN pins, set them in `pins` before
 * mcs_esp32_hal_init (see ports/esp32/README.md). */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "mcs_runtime.h"
#include "mcs_port_esp32.h"

static mcs_runtime_t rt;
static mcs_hal_t hal;
static mcs_esp32_heap_t vm_heap;

static void idle(void* ud) { (void)ud; taskYIELD(); }

void app_main(void) {
    mcs_esp32_cfg_t pins = MCS_ESP32_CFG_DEFAULT;      /* pin -1 = not used */
    mcs_esp32_hal_init(&hal, &pins);

    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    /* files on flash; without a "storage" partition: a RAM disk (lost on reset) */
    bool on_flash = mcs_esp32_flash_fs("storage", &cfg.fs_ops, &cfg.fs_ctx);

    /* Hard pool ceiling, not a promise about total ESP32 RAM consumption. */
#ifdef CONFIG_MICROCS_VM_HEAP_KB
    size_t target = (size_t)CONFIG_MICROCS_VM_HEAP_KB * 1024;
#else
    size_t target = 192 * 1024;
#endif
#ifdef CONFIG_MICROCS_VM_STACK_SLOTS
    cfg.stack_slots = CONFIG_MICROCS_VM_STACK_SLOTS;
#endif
#ifdef CONFIG_MICROCS_VM_MAX_FRAMES
    cfg.max_frames = CONFIG_MICROCS_VM_MAX_FRAMES;
#endif
    size_t reserve = 32 * 1024;
#if MCS_ESP32_WIFI && defined(CONFIG_MICROCS_WIFI_RESERVE_KB)
    reserve += (size_t)CONFIG_MICROCS_WIFI_RESERVE_KB * 1024;
#endif
#if MCS_ESP32_BLE && defined(CONFIG_MICROCS_BLE_RESERVE_KB)
    reserve += (size_t)CONFIG_MICROCS_BLE_RESERVE_KB * 1024;   /* NimBLE host + controller, started later */
#endif
    /* Regions are acquired only as needed, up to the combined target. The
     * provider rechecks internal native headroom on every growth attempt. */
    mcs_esp32_heap_init(&vm_heap, reserve);
    cfg.heap = NULL;
    cfg.heap_size = target;
    cfg.heap_provider = &vm_heap.provider;
    printf("MicroCS: automatic VM pool, ceiling %u bytes, native reserve %u bytes\n",
           (unsigned)target, (unsigned)reserve);
    printf("MicroCS: internal heap free %u bytes, largest %u bytes (before VM growth)\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    if (!on_flash) cfg.ramfs_size = target / 6; /* same bounded pool; may fail if native RAM is insufficient */
    mcs_vfs_statfs_t sf;
    if (on_flash && cfg.fs_ops->statfs && cfg.fs_ops->statfs(cfg.fs_ctx, &sf) == 0)
        printf("MicroCS: %s on flash, %u of %u KB free\n", sf.format, (unsigned)(sf.free / 1024), (unsigned)(sf.total / 1024));
    else if (!on_flash)
        printf("MicroCS: no \"storage\" partition - files are kept in RAM only\n");
#if MCS_ESP32_WIFI && defined(CONFIG_MICROCS_WIFI)
    /* menuconfig > MicroCS > Wi-Fi: join a network in the background (Wifi.Save remembers one) */
    if (CONFIG_MICROCS_WIFI_SSID[0] || CONFIG_MICROCS_WIFI_AUTOCONNECT)
        mcs_esp32_wifi_autoconnect(CONFIG_MICROCS_WIFI_SSID, CONFIG_MICROCS_WIFI_PASSWORD);
#endif
    cfg.console = mcs_esp32_console();
    cfg.ticks = mcs_esp32_ticks;
    cfg.delay = mcs_esp32_delay;
    cfg.hal = &hal;
    cfg.idle = idle;
    for (;;) {
        int rc = mcs_runtime_run(&rt, &cfg);
        if (rc) {
            printf("MicroCS: runtime startup failed (%d); native reserve/cap may leave insufficient RAM\n", rc);
            return; /* fail closed, not an allocation retry loop */
        }
    }
}
