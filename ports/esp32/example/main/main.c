/* MicroCS on ESP32 / S2 / S3 / C2 / C3 / C6 / H2 / P4: a C# REPL plus every
 * peripheral. Pins below are examples for an ESP32-S3 DevKitC (and for an
 * ESP8684 / ESP32-C2 DevKit) - change them for your board (pin -1 = not used). */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "mcs_runtime.h"
#include "mcs_port_esp32.h"

static mcs_runtime_t rt;
static mcs_hal_t hal;

static void idle(void* ud) { (void)ud; taskYIELD(); }

void app_main(void) {
    mcs_esp32_cfg_t pins = MCS_ESP32_CFG_DEFAULT;
#if CONFIG_IDF_TARGET_ESP32C2
    /* ESP32-C2: GPIO0-20, ADC on GPIO0-4, UART0 = GPIO19/20, no I2S/TWAI/USB-JTAG */
    pins.uart[1] = (mcs_esp32_uart_pins_t){ 7, 10, -1, -1 };       /* UART.Open(1, ...): TX 7, RX 10 */
    pins.i2c[0] = (mcs_esp32_i2c_pins_t){ 5, 6 };                   /* I2C bus 0: SDA 5, SCL 6 */
    pins.spi[0] = (mcs_esp32_spi_pins_t){ 4, 3, 2, -1, -1 };       /* SPI bus 0 (SPI2): SCLK, MOSI, MISO */
    pins.pwm[0] = 1;                                                 /* PWM channel 0 on GPIO1 */
#else
    pins.uart[1] = (mcs_esp32_uart_pins_t){ 17, 18, -1, -1 };      /* UART.Open(1, ...): TX 17, RX 18 */
    pins.i2c[0] = (mcs_esp32_i2c_pins_t){ 8, 9 };                   /* I2C bus 0: SDA 8, SCL 9 */
    pins.spi[0] = (mcs_esp32_spi_pins_t){ 12, 11, 13, -1, -1 };    /* SPI bus 0: SCLK, MOSI, MISO */
    pins.pwm[0] = 2;                                                 /* PWM channel 0 on GPIO2 */
    pins.pwm[1] = 48;                                                /* PWM channel 1: on-board LED */
    pins.i2s[0] = (mcs_esp32_i2s_pins_t){ -1, 4, 5, 6, 7 };          /* MCLK, BCLK, WS, DOUT, DIN */
    pins.can = (mcs_esp32_can_pins_t){ 15, 16 };                     /* TWAI TX, RX */
#endif
    pins.led = -1;                                                   /* GPIO of a plain LED for new Pin("LED"), e.g. 2 on an ESP32 DevKit V1 */
    mcs_esp32_hal_init(&hal, &pins);

    /* VM heap + RAM disk: 192 KB where the chip has it, otherwise what is free
     * minus a reserve for the drivers and FreeRTOS (the C2 has ~180 KB free
     * after boot, so it gets ~128 KB) */
#if CONFIG_IDF_TARGET_ESP32C2
    size_t heap_size = 128 * 1024, ramfs_size = 16 * 1024;
#else
    size_t heap_size = 192 * 1024, ramfs_size = 32 * 1024;
#endif
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largest < heap_size + 32 * 1024) heap_size = largest > 64 * 1024 ? (largest - 32 * 1024) & ~(size_t)1023 : largest / 2;
    if (ramfs_size > heap_size / 4) ramfs_size = heap_size / 4;
    uint8_t* heap = heap_caps_malloc(heap_size, MALLOC_CAP_8BIT);   /* or a static array */

    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap;
    cfg.heap_size = heap ? heap_size : 0;
    cfg.ramfs_size = ramfs_size;
    cfg.console = mcs_esp32_console_usb();     /* USB-Serial-JTAG, or UART0 on chips without it */
    cfg.ticks = mcs_esp32_ticks;
    cfg.delay = mcs_esp32_delay;
    cfg.hal = &hal;
    cfg.idle = idle;
    for (;;) mcs_runtime_run(&rt, &cfg);
}
