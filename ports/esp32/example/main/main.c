/* MicroCS as the whole firmware on any ESP32-family chip: a C# REPL / shell on
 * the serial console (UART0 and, where the chip has it, USB-Serial-JTAG - so it
 * works on either USB connector), with a RAM filesystem and the GPIO/ADC/PWM/...
 * classes. To give scripts UART1, I2C, SPI, PWM, I2S or CAN pins, set them in
 * `pins` before mcs_esp32_hal_init (see ports/esp32/README.md). */
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

    /* VM heap: up to 192 KB, leaving 32 KB for the drivers and FreeRTOS */
    size_t heap_size = 192 * 1024;
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largest < heap_size + 32 * 1024) heap_size = largest > 64 * 1024 ? (largest - 32 * 1024) & ~(size_t)1023 : largest / 2;
    uint8_t* heap = heap_caps_malloc(heap_size, MALLOC_CAP_8BIT);

    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap;
    cfg.heap_size = heap ? heap_size : 0;
    cfg.ramfs_size = heap_size / 6;                    /* RAM disk, taken from the heap */
    cfg.console = mcs_esp32_console();
    cfg.ticks = mcs_esp32_ticks;
    cfg.delay = mcs_esp32_delay;
    cfg.hal = &hal;
    cfg.idle = idle;
    for (;;) mcs_runtime_run(&rt, &cfg);
}
