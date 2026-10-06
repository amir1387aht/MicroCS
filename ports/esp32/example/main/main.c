/* MicroCS on ESP32 / S2 / S3 / C3 / C6 / H2 / P4: a C# REPL plus every
 * peripheral. Pins below are examples for an ESP32-S3 DevKitC - change them
 * for your board (pin -1 = not used). */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "mcs_runtime.h"
#include "mcs_port_esp32.h"

static mcs_runtime_t rt;
static mcs_hal_t hal;

static void idle(void* ud) { (void)ud; taskYIELD(); }

void app_main(void) {
    mcs_esp32_cfg_t pins = MCS_ESP32_CFG_DEFAULT;
    pins.uart[1] = (mcs_esp32_uart_pins_t){ 17, 18, -1, -1 };      /* UART.Open(1, ...): TX 17, RX 18 */
    pins.i2c[0] = (mcs_esp32_i2c_pins_t){ 8, 9 };                   /* I2C bus 0: SDA 8, SCL 9 */
    pins.spi[0] = (mcs_esp32_spi_pins_t){ 12, 11, 13, -1, -1 };    /* SPI bus 0: SCLK, MOSI, MISO */
    pins.pwm[0] = 2;                                                 /* PWM channel 0 on GPIO2 */
    pins.pwm[1] = 48;                                                /* PWM channel 1: on-board LED */
    pins.i2s[0] = (mcs_esp32_i2s_pins_t){ -1, 4, 5, 6, 7 };          /* MCLK, BCLK, WS, DOUT, DIN */
    pins.can = (mcs_esp32_can_pins_t){ 15, 16 };                     /* TWAI TX, RX */
    pins.led = -1;                                                   /* GPIO of a plain LED for new Pin("LED"), e.g. 2 on an ESP32 DevKit V1 */
    mcs_esp32_hal_init(&hal, &pins);

    static uint8_t* heap;
    size_t heap_size = 192 * 1024;
    heap = heap_caps_malloc(heap_size, MALLOC_CAP_8BIT);   /* or a static array */

    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap;
    cfg.heap_size = heap_size;
    cfg.ramfs_size = 32 * 1024;
    cfg.console = mcs_esp32_console_usb();     /* USB-Serial-JTAG, or UART0 on chips without it */
    cfg.ticks = mcs_esp32_ticks;
    cfg.delay = mcs_esp32_delay;
    cfg.hal = &hal;
    cfg.idle = idle;
    for (;;) mcs_runtime_run(&rt, &cfg);
}
