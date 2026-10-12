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

static void idle(void* ud) { (void)ud; taskYIELD(); }

void app_main(void) {
    mcs_esp32_cfg_t pins = MCS_ESP32_CFG_DEFAULT;      /* pin -1 = not used */
    mcs_esp32_hal_init(&hal, &pins);

    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    /* files on flash; without a "storage" partition: a RAM disk (lost on reset) */
    bool on_flash = mcs_esp32_flash_fs("storage", &cfg.fs_ops, &cfg.fs_ctx);

    /* VM heap: up to 192 KB, leaving 32 KB for the drivers and FreeRTOS
     * (+ menuconfig > MicroCS > Wi-Fi > RAM kept free for the Wi-Fi driver, and the same for BLE) */
    size_t heap_size = 192 * 1024, reserve = 32 * 1024;
#if MCS_ESP32_WIFI && defined(CONFIG_MICROCS_WIFI_RESERVE_KB)
    reserve += (size_t)CONFIG_MICROCS_WIFI_RESERVE_KB * 1024;
#endif
#if MCS_ESP32_BLE && defined(CONFIG_MICROCS_BLE_RESERVE_KB)
    reserve += (size_t)CONFIG_MICROCS_BLE_RESERVE_KB * 1024;   /* NimBLE host + controller, started later */
#endif
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    size_t total = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (heap_size + reserve > total) heap_size = total > reserve + 64 * 1024 ? total - reserve : total / 2;
    if (heap_size + 8 * 1024 > largest) heap_size = largest > 72 * 1024 ? largest - 8 * 1024 : largest / 2;
    heap_size &= ~(size_t)1023;
    uint8_t* heap = heap_caps_malloc(heap_size, MALLOC_CAP_8BIT);

    cfg.heap = heap;
    cfg.heap_size = heap ? heap_size : 0;
    if (!on_flash) cfg.ramfs_size = heap_size / 6;     /* RAM disk, taken from the heap */
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
    for (;;) mcs_runtime_run(&rt, &cfg);
}
