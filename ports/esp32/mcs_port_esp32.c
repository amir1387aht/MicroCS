/*
 * MicroCS port for ESP-IDF 5.x (ESP32 / S2 / S3 / C2 / C3 / C5 / C6 / H2 / P4).
 * See mcs_port_esp32.h. Uses only public ESP-IDF driver APIs.
 */
#include "mcs_port_esp32.h"
#include "mcs_driver.h"
#include <string.h>
#include <sys/time.h>
#include "sdkconfig.h"
#include "esp_idf_version.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "mcs_esp_power.h"
#include "esp_mac.h"
#include "esp_rom_sys.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "soc/soc_caps.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/gptimer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#include "driver/gpio_filter.h"
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0) && SOC_ASYNC_MEMCPY_SUPPORTED
#define MCS_ESP_MEMCPY 1
#include "esp_async_memcpy.h"
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
#define MCS_ESP_NEW_I2C 1
#include "driver/i2c_master.h"
#else
#define MCS_ESP_NEW_I2C 0
#include "driver/i2c.h"
#endif
#if SOC_I2S_SUPPORTED
#include "driver/i2s_std.h"
#endif
#if SOC_DAC_SUPPORTED
#include "driver/dac_oneshot.h"
#endif
#if SOC_TWAI_SUPPORTED && !SOC_TWAI_SUPPORT_FD
#include "driver/twai.h"
#endif
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "driver/usb_serial_jtag.h"
#endif

#if defined(CONFIG_NEWLIB_NANO_FORMAT) && CONFIG_NEWLIB_NANO_FORMAT && MCS_ENABLE_FLOAT
/* ESP32-C2 defaults to the ROM's "nano" printf, which cannot format floating
 * point: MicroCS would print doubles as empty strings. */
#warning "MicroCS: CONFIG_NEWLIB_NANO_FORMAT=y has no float formatting - set it to n (menuconfig > Component config > LibC) or build with MCS_ENABLE_FLOAT=0"
#endif

/* hardware gptimers usable for Timer.Start (ESP32/S2/S3: 4, C3/C6/H2: 2, C2: 1) */
#if defined(SOC_TIMER_GROUP_TOTAL_TIMERS) && SOC_TIMER_GROUP_TOTAL_TIMERS < MCS_ESP32_TIMERS
#define MCS_ESP32_HW_TIMERS ((int)SOC_TIMER_GROUP_TOTAL_TIMERS)
#else
#define MCS_ESP32_HW_TIMERS MCS_ESP32_TIMERS
#endif
/* LEDC source clock used to pick the duty resolution (LEDC_AUTO_CLK still
 * chooses the clock; a too-wide resolution is retried one bit narrower) */
#ifndef MCS_ESP32_LEDC_CLK_HZ
#if CONFIG_IDF_TARGET_ESP32C2
#define MCS_ESP32_LEDC_CLK_HZ 60000000u     /* PLL_F60M */
#elif CONFIG_IDF_TARGET_ESP32H2
#define MCS_ESP32_LEDC_CLK_HZ 96000000u     /* PLL_F96M */
#else
#define MCS_ESP32_LEDC_CLK_HZ 80000000u     /* APB / PLL_F80M */
#endif
#endif

#ifndef MCS_ESP32_TIMEOUT_MS
#define MCS_ESP32_TIMEOUT_MS 100
#endif
#ifndef MCS_ESP32_I2C_DEVICES
#define MCS_ESP32_I2C_DEVICES 8      /* cached device handles per bus (new I2C driver) */
#endif

static mcs_esp32_cfg_t g_cfg;
static QueueHandle_t g_events;

static int esp_rc(esp_err_t e) {
    switch (e) {
    case ESP_OK: return 0;
    case ESP_ERR_TIMEOUT: return MCS_HAL_ETIMEOUT;
    case ESP_ERR_NOT_SUPPORTED: return MCS_HAL_ENOTSUP;
    case ESP_ERR_INVALID_ARG: return MCS_HAL_EINVAL;
    case ESP_ERR_INVALID_STATE: return MCS_HAL_EBUSY;
    case ESP_ERR_NOT_FOUND: return MCS_HAL_ENODEV;
    default: return MCS_HAL_ERR;
    }
}
static TickType_t ms_ticks(uint32_t ms) {
    TickType_t t = pdMS_TO_TICKS(ms);
    return ms && !t ? 1 : t;
}

/* events from ISRs -> FreeRTOS queue -> mcs_hal_poll on the VM task */
static void IRAM_ATTR post_isr(uint8_t type, uint16_t source, int32_t value) {
    mcs_hal_event_t ev = { type, 0, source, value };
    BaseType_t woken = pdFALSE;
    if (g_events) xQueueSendFromISR(g_events, &ev, &woken);
    if (woken) portYIELD_FROM_ISR();
}
#if MCS_ESP32_WIFI || MCS_ESP32_BLE
/* network / Bluetooth events come from the Wi-Fi, lwIP or NimBLE tasks, not from an ISR */
static bool post_task(int type, int source, int32_t value) {
    mcs_hal_event_t ev = { (uint8_t)type, 0, (uint16_t)source, value };
    return g_events && xQueueSend(g_events, &ev, 0) == pdTRUE;
}
#endif
static int e_poll_event(void* ctx, mcs_hal_event_t* ev) {
    (void)ctx;
    return g_events && xQueueReceive(g_events, ev, 0) == pdTRUE;
}

/* board options: cfg.Set("queue_size", 8) ... (Hal.Options() lists them) */
static const mcs_hal_option_t e_options[] = {
    { "GPIO", "drive_cap", 0, 3, "raw pad drive capability 0..3 (~5 / 10 / 20 / 40 mA)" },
    { "UART", "rx_threshold", 1, 127, "RX FIFO level that wakes the driver (default 120)" },
    { "SPI", "queue_size", 1, 16, "transactions the SPI driver can queue (default 4)" },
    { "PWM", "timer", 0, 3, "LEDC timer for the channel (default channel % 4); channels on one timer share frequency and resolution" },
    { NULL, NULL, 0, 0, NULL }
};

/* ------------------------------------------------------------------ GPIO */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0) && (SOC_GPIO_SUPPORT_PIN_GLITCH_FILTER || SOC_GPIO_FLEX_GLITCH_FILTER_NUM > 0)
#define MCS_ESP_GPIO_FILTER 1
static gpio_glitch_filter_handle_t g_gpio_filt[SOC_GPIO_PIN_COUNT];
static void gpio_filter_off(int pin) {
    if (pin < 0 || pin >= SOC_GPIO_PIN_COUNT || !g_gpio_filt[pin]) return;
    gpio_glitch_filter_disable(g_gpio_filt[pin]);
    gpio_del_glitch_filter(g_gpio_filt[pin]);
    g_gpio_filt[pin] = NULL;
}
static int gpio_filter_on(int pin, uint32_t ns) {
    esp_err_t e = ESP_ERR_NOT_SUPPORTED;
    gpio_glitch_filter_handle_t h = NULL;
#if SOC_GPIO_FLEX_GLITCH_FILTER_NUM > 0
    gpio_flex_glitch_filter_config_t f;
    memset(&f, 0, sizeof f);
    f.clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT;
    f.gpio_num = (gpio_num_t)pin;
    f.window_width_ns = ns;
    f.window_thres_ns = ns;
    e = gpio_new_flex_glitch_filter(&f, &h);
    if (e == ESP_ERR_INVALID_ARG) { mcs_hal_error("FilterNs: outside the glitch filter window range"); return MCS_HAL_EINVAL; }
#endif
#if SOC_GPIO_SUPPORT_PIN_GLITCH_FILTER
    if (e != ESP_OK) {               /* fixed filter: pulses shorter than 2 IO-MUX clocks (~25 ns) */
        if (ns > 50) { mcs_hal_error("FilterNs: this chip only filters pulses up to ~25 ns (use 1..50)"); return MCS_HAL_ENOTSUP; }
        gpio_pin_glitch_filter_config_t p;
        memset(&p, 0, sizeof p);
        p.clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT;
        p.gpio_num = (gpio_num_t)pin;
        e = gpio_new_pin_glitch_filter(&p, &h);
    }
#endif
    if (e != ESP_OK) return esp_rc(e);
    e = gpio_glitch_filter_enable(h);
    if (e != ESP_OK) { gpio_del_glitch_filter(h); return esp_rc(e); }
    g_gpio_filt[pin] = h;
    return 0;
}
#else
static void gpio_filter_off(int pin) { (void)pin; }
#endif
/* pins inverted with GpioConfig.Invert: the GPIO driver reads the pad directly,
 * so the level is flipped here (reads, writes, interrupt edges and values) */
static uint64_t g_gpio_inv;
#define E_INV(pin) ((int)((g_gpio_inv >> ((unsigned)(pin) & 63u)) & 1u))

static int gpio_setup(int pin, int mode, int initial) {
    if (!GPIO_IS_VALID_GPIO(pin)) return MCS_HAL_EINVAL;
    gpio_num_t p = (gpio_num_t)pin;
    gpio_hold_dis(p);
    gpio_reset_pin(p);
    if (initial >= 0) gpio_set_level(p, (uint32_t)((initial != 0) ^ E_INV(pin)));
    esp_err_t e = ESP_OK;
    switch (mode) {
    case MCS_GPIO_INPUT: e = gpio_set_direction(p, GPIO_MODE_INPUT); gpio_set_pull_mode(p, GPIO_FLOATING); break;
    case MCS_GPIO_INPUT_PULLUP: e = gpio_set_direction(p, GPIO_MODE_INPUT); gpio_set_pull_mode(p, GPIO_PULLUP_ONLY); break;
    case MCS_GPIO_INPUT_PULLDOWN: e = gpio_set_direction(p, GPIO_MODE_INPUT); gpio_set_pull_mode(p, GPIO_PULLDOWN_ONLY); break;
    case MCS_GPIO_OUTPUT:      /* INPUT_OUTPUT so GPIO.Read() returns the driven level */
        if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) return MCS_HAL_ENOTSUP;
        e = gpio_set_direction(p, GPIO_MODE_INPUT_OUTPUT); break;
    case MCS_GPIO_OPEN_DRAIN:
        if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) return MCS_HAL_ENOTSUP;
        e = gpio_set_direction(p, GPIO_MODE_INPUT_OUTPUT_OD); gpio_set_pull_mode(p, GPIO_PULLUP_ONLY); break;
    case MCS_GPIO_ANALOG: e = gpio_set_direction(p, GPIO_MODE_DISABLE); gpio_set_pull_mode(p, GPIO_FLOATING); break;
    default: return MCS_HAL_EINVAL;
    }
    return esp_rc(e);
}
static int e_gpio_mode(void* ctx, int pin, int mode) {
    (void)ctx;
    if (!GPIO_IS_VALID_GPIO(pin)) return MCS_HAL_EINVAL;
    g_gpio_inv &= ~(1ull << (pin & 63));       /* a plain GPIO.Mode() resets GpioConfig extras */
    gpio_filter_off(pin);
    return gpio_setup(pin, mode, -1);
}
static int e_gpio_configure(void* ctx, int pin, const mcs_gpio_config_t* c) {
    (void)ctx;
    if (!GPIO_IS_VALID_GPIO(pin) || pin > 63) return MCS_HAL_EINVAL;
    if (c->schmitt) { mcs_hal_error("Schmitt: ESP32 input hysteresis is not configurable"); return MCS_HAL_ENOTSUP; }
    if (c->alternate >= 0) {
        mcs_hal_error("Alternate: ESP32 routes peripherals through the GPIO matrix - set the pins in UartConfig / I2cConfig / SpiConfig instead");
        return MCS_HAL_ENOTSUP;
    }
    if (c->pull > MCS_PULL_BOTH || c->speed > MCS_SPEED_VERY_HIGH) return MCS_HAL_EINVAL;
    bool out = c->mode == MCS_GPIO_OUTPUT || c->mode == MCS_GPIO_OPEN_DRAIN;
    gpio_filter_off(pin);
    if (c->invert == MCS_ON) g_gpio_inv |= 1ull << pin; else g_gpio_inv &= ~(1ull << pin);
    int r = gpio_setup(pin, c->mode, out ? c->initial : -1);
    if (r) return r;
    gpio_num_t p = (gpio_num_t)pin;
    if (c->pull) {
        static const gpio_pull_mode_t pm[] = { GPIO_FLOATING, GPIO_FLOATING, GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY, GPIO_PULLUP_PULLDOWN };
        if ((r = esp_rc(gpio_set_pull_mode(p, pm[c->pull])))) return r;
    }
    /* drive strength: ~5 / 10 / 20 (default) / 40 mA; Speed maps onto it (no slew control) */
    int cap = -1;
    if (c->drive_ma) cap = c->drive_ma <= 7 ? 0 : c->drive_ma <= 14 ? 1 : c->drive_ma <= 29 ? 2 : 3;
    else if (c->speed) cap = c->speed - 1;
    cap = (int)mcs_hal_opt(e_options, &c->opts, "GPIO", "drive_cap", cap);
    if (cap >= 0 && (r = esp_rc(gpio_set_drive_capability(p, (gpio_drive_cap_t)cap)))) return r;
    if (c->filter_ns) {
#if MCS_ESP_GPIO_FILTER
        if ((r = gpio_filter_on(pin, c->filter_ns))) return r;
#else
        mcs_hal_error("FilterNs: this chip has no GPIO glitch filter");
        return MCS_HAL_ENOTSUP;
#endif
    }
    if (c->hold == MCS_ON) return esp_rc(gpio_hold_en(p));
    return 0;
}
static int e_gpio_write(void* ctx, int pin, int v) {
    (void)ctx;
    return esp_rc(gpio_set_level((gpio_num_t)pin, (uint32_t)((v != 0) ^ E_INV(pin))));
}
static int e_gpio_read(void* ctx, int pin) {
    (void)ctx;
    if (!GPIO_IS_VALID_GPIO(pin)) return MCS_HAL_EINVAL;
    return gpio_get_level((gpio_num_t)pin) ^ E_INV(pin);
}
static void IRAM_ATTR gpio_isr(void* arg) {
    int pin = (int)(intptr_t)arg;
    post_isr(MCS_HAL_EV_GPIO, (uint16_t)pin, gpio_get_level((gpio_num_t)pin) ^ E_INV(pin));
}
static int e_gpio_irq(void* ctx, int pin, int edge) {
    (void)ctx;
    if (!GPIO_IS_VALID_GPIO(pin)) return MCS_HAL_EINVAL;
    static bool service;
    if (!service) {
        esp_err_t e = gpio_install_isr_service(0);
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return esp_rc(e);   /* already installed by the app: fine */
        service = true;
    }
    gpio_num_t p = (gpio_num_t)pin;
    if (!edge) { gpio_intr_disable(p); gpio_isr_handler_remove(p); return 0; }
    bool rising = edge == MCS_GPIO_EDGE_RISING, falling = edge == MCS_GPIO_EDGE_FALLING;
    if (E_INV(pin)) { bool t = rising; rising = falling; falling = t; }
    gpio_set_intr_type(p, rising ? GPIO_INTR_POSEDGE : falling ? GPIO_INTR_NEGEDGE : GPIO_INTR_ANYEDGE);
    gpio_isr_handler_remove(p);
    esp_err_t e = gpio_isr_handler_add(p, gpio_isr, (void*)(intptr_t)pin);
    if (e == ESP_OK) e = gpio_intr_enable(p);
    return esp_rc(e);
}

/* ------------------------------------------------------------------ UART */
static uint16_t g_uart_rxbuf[MCS_ESP32_UARTS], g_uart_txbuf[MCS_ESP32_UARTS];
/* x = NULL: plain UART.Open / UART.Config (keeps buffers, pins from the board config) */
static int uart_setup(int port, const mcs_uart_cfg_t* c, const mcs_uart_config_t* x) {
    if (port < 0 || port >= (int)SOC_UART_NUM || port >= MCS_ESP32_UARTS) return MCS_HAL_ENOTSUP;
    if (c->data_bits < 5 || c->data_bits > 8) return MCS_HAL_ENOTSUP;
    uart_port_t up = (uart_port_t)port;
    size_t rxb = g_uart_rxbuf[port] ? g_uart_rxbuf[port] : MCS_ESP32_UART_RXBUF, txb = g_uart_txbuf[port];
    if (x) {
        if (x->dma == MCS_ON) { mcs_hal_error("Dma: the ESP-IDF UART driver uses interrupts and ring buffers"); return MCS_HAL_ENOTSUP; }
        if (x->rs485 && c->flow_control) { mcs_hal_error("Rs485: turn FlowControl off (RTS drives the transceiver)"); return MCS_HAL_EINVAL; }
        rxb = x->rx_buffer ? x->rx_buffer : MCS_ESP32_UART_RXBUF;
        txb = x->tx_buffer;
        if (rxb <= SOC_UART_FIFO_LEN) { mcs_hal_error("RxBuffer: must be larger than the 128-byte FIFO"); return MCS_HAL_EINVAL; }
        if (txb && txb <= SOC_UART_FIFO_LEN) { mcs_hal_error("TxBuffer: 0 (blocking writes) or larger than the 128-byte FIFO"); return MCS_HAL_EINVAL; }
    }
    if (uart_is_driver_installed(up) && (rxb != (g_uart_rxbuf[port] ? g_uart_rxbuf[port] : MCS_ESP32_UART_RXBUF) || txb != g_uart_txbuf[port]))
        uart_driver_delete(up);
    uart_config_t u;
    memset(&u, 0, sizeof u);
    u.baud_rate = (int)c->baud;
    u.data_bits = c->data_bits == 5 ? UART_DATA_5_BITS : c->data_bits == 6 ? UART_DATA_6_BITS
                : c->data_bits == 7 ? UART_DATA_7_BITS : UART_DATA_8_BITS;
    u.parity = c->parity == MCS_UART_PARITY_ODD ? UART_PARITY_ODD : c->parity == MCS_UART_PARITY_EVEN ? UART_PARITY_EVEN : UART_PARITY_DISABLE;
    u.stop_bits = c->stop_bits == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
    u.flow_ctrl = c->flow_control ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE;
    u.rx_flow_ctrl_thresh = 100;
    u.source_clk = UART_SCLK_DEFAULT;
    if (!uart_is_driver_installed(up)) {
        esp_err_t e = uart_driver_install(up, (int)rxb, (int)txb, 0, NULL, 0);
        if (e != ESP_OK) return esp_rc(e);
        g_uart_rxbuf[port] = (uint16_t)rxb; g_uart_txbuf[port] = (uint16_t)txb;
    }
    esp_err_t e = uart_param_config(up, &u);
    const mcs_esp32_uart_pins_t* pp = &g_cfg.uart[port];
    int tx = x && x->tx >= 0 ? x->tx : pp->tx, rx = x && x->rx >= 0 ? x->rx : pp->rx;
    int rts = x && x->rts >= 0 ? x->rts : pp->rts, cts = x && x->cts >= 0 ? x->cts : pp->cts;
    if (e == ESP_OK && (tx >= 0 || rx >= 0 || rts >= 0 || cts >= 0))
        e = uart_set_pin(up, tx, rx, rts, cts);
    if (e != ESP_OK || !x) return esp_rc(e);
    uint32_t inv = (x->invert_tx ? UART_SIGNAL_TXD_INV : 0) | (x->invert_rx ? UART_SIGNAL_RXD_INV : 0);
    if ((e = uart_set_line_inverse(up, inv)) != ESP_OK) return esp_rc(e);
    if ((e = uart_set_mode(up, x->rs485 ? UART_MODE_RS485_HALF_DUPLEX : UART_MODE_UART)) != ESP_OK) {
        if (x->rs485) mcs_hal_error("Rs485: needs an Rts pin for the transceiver DE input");
        return esp_rc(e);
    }
    if ((e = uart_set_rx_timeout(up, (uint8_t)(x->rx_timeout ? (x->rx_timeout > 255 ? 255 : x->rx_timeout) : 10))) != ESP_OK) {
        mcs_hal_error("RxTimeout: too long for this chip (symbols, ~1..100)");
        return esp_rc(e);
    }
    int th = (int)mcs_hal_opt(e_options, &x->opts, "UART", "rx_threshold", -1);
    if (th > 0 && (e = uart_set_rx_full_threshold(up, th)) != ESP_OK) return esp_rc(e);
    return 0;
}
static int e_uart_config(void* ctx, int port, const mcs_uart_cfg_t* c) { (void)ctx; return uart_setup(port, c, NULL); }
static int e_uart_configure(void* ctx, int port, const mcs_uart_config_t* x) { (void)ctx; return uart_setup(port, &x->frame, x); }
static int e_uart_open(void* ctx, int port, uint32_t baud) {
    mcs_uart_cfg_t c = { baud, 8, MCS_UART_PARITY_NONE, 1, 0 };
    return e_uart_config(ctx, port, &c);
}
static int e_uart_close(void* ctx, int port) {
    (void)ctx;
    if (port < 0 || port >= (int)SOC_UART_NUM) return MCS_HAL_ENOTSUP;
    return uart_is_driver_installed((uart_port_t)port) ? esp_rc(uart_driver_delete((uart_port_t)port)) : 0;
}
static int e_uart_write(void* ctx, int port, const uint8_t* d, size_t n) {
    (void)ctx;
    if (port < 0 || port >= (int)SOC_UART_NUM || !uart_is_driver_installed((uart_port_t)port)) return MCS_HAL_ENOTSUP;
    int r = uart_write_bytes((uart_port_t)port, (const char*)d, n);
    return r < 0 ? MCS_HAL_ERR : r;
}
static int e_uart_read(void* ctx, int port, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    (void)ctx;
    if (port < 0 || port >= (int)SOC_UART_NUM || !uart_is_driver_installed((uart_port_t)port)) return MCS_HAL_ENOTSUP;
    /* return as soon as something arrived: first byte with the timeout, then what is buffered */
    int r = uart_read_bytes((uart_port_t)port, buf, 1, ms_ticks(timeout_ms));
    if (r <= 0) return r < 0 ? MCS_HAL_ERR : 0;
    size_t more = 0;
    uart_get_buffered_data_len((uart_port_t)port, &more);
    if (more > n - 1) more = n - 1;
    if (more) { int m = uart_read_bytes((uart_port_t)port, buf + 1, more, 0); if (m > 0) r += m; }
    return r;
}
static int e_uart_available(void* ctx, int port) {
    (void)ctx;
    if (port < 0 || port >= (int)SOC_UART_NUM || !uart_is_driver_installed((uart_port_t)port)) return MCS_HAL_ENOTSUP;
    size_t n = 0;
    uart_get_buffered_data_len((uart_port_t)port, &n);
    return (int)n;
}

/* ------------------------------------------------------------------ I2C */
/* runtime bus settings (I2C.Open(bus, I2cConfig)); pins start as the board config */
static mcs_esp32_i2c_pins_t g_i2c_pins[MCS_ESP32_I2C_BUSES];
static uint8_t g_i2c_pu[MCS_ESP32_I2C_BUSES], g_i2c_glitch[MCS_ESP32_I2C_BUSES];
static uint16_t g_i2c_to[MCS_ESP32_I2C_BUSES];
static int i2c_check(int bus) {
    if (bus < 0 || bus >= MCS_ESP32_I2C_BUSES || bus >= (int)SOC_I2C_NUM) return MCS_HAL_ENOTSUP;
    if (g_i2c_pins[bus].sda < 0 || g_i2c_pins[bus].scl < 0) {
        mcs_hal_error("no I2C pins for this bus - I2C.Open(bus, new I2cConfig { Sda = .., Scl = .. })");
        return MCS_HAL_ENOTSUP;
    }
    return 0;
}
static uint32_t i2c_to(int bus) { return g_i2c_to[bus] ? g_i2c_to[bus] : MCS_ESP32_TIMEOUT_MS; }
#if MCS_ESP_NEW_I2C
typedef struct { uint16_t addr; uint32_t hz; i2c_master_dev_handle_t dev; } i2c_dev_t;
static i2c_master_bus_handle_t g_i2c[MCS_ESP32_I2C_BUSES];
static uint32_t g_i2c_hz[MCS_ESP32_I2C_BUSES];
static i2c_dev_t g_i2c_dev[MCS_ESP32_I2C_BUSES][MCS_ESP32_I2C_DEVICES];
static uint8_t g_i2c_next[MCS_ESP32_I2C_BUSES];
static void i2c_down(int bus) {
    for (int i = 0; i < MCS_ESP32_I2C_DEVICES; i++)
        if (g_i2c_dev[bus][i].dev) { i2c_master_bus_rm_device(g_i2c_dev[bus][i].dev); g_i2c_dev[bus][i].dev = NULL; }
    if (g_i2c[bus]) { i2c_del_master_bus(g_i2c[bus]); g_i2c[bus] = NULL; }
}
static int e_i2c_open(void* ctx, int bus, uint32_t freq) {
    (void)ctx;
    int r = i2c_check(bus);
    if (r) return r;
    g_i2c_hz[bus] = freq ? freq : 100000;
    if (g_i2c[bus]) return 0;
    i2c_master_bus_config_t c;
    memset(&c, 0, sizeof c);
    c.i2c_port = bus;
    c.sda_io_num = (gpio_num_t)g_i2c_pins[bus].sda;
    c.scl_io_num = (gpio_num_t)g_i2c_pins[bus].scl;
    c.clk_source = I2C_CLK_SRC_DEFAULT;
    c.glitch_ignore_cnt = g_i2c_glitch[bus];
    c.flags.enable_internal_pullup = g_i2c_pu[bus];
    return esp_rc(i2c_new_master_bus(&c, &g_i2c[bus]));
}
static i2c_master_dev_handle_t i2c_dev(int bus, int addr, int* err) {
    *err = 0;
    if (bus < 0 || bus >= MCS_ESP32_I2C_BUSES) { *err = MCS_HAL_ENOTSUP; return NULL; }
    if (!g_i2c[bus] && (*err = e_i2c_open(NULL, bus, 0))) return NULL;
    i2c_dev_t* d = g_i2c_dev[bus];
    for (int i = 0; i < MCS_ESP32_I2C_DEVICES; i++)
        if (d[i].dev && d[i].addr == addr && d[i].hz == g_i2c_hz[bus]) return d[i].dev;
    i2c_dev_t* slot = &d[g_i2c_next[bus]];       /* round-robin eviction */
    g_i2c_next[bus] = (uint8_t)((g_i2c_next[bus] + 1) % MCS_ESP32_I2C_DEVICES);
    if (slot->dev) { i2c_master_bus_rm_device(slot->dev); slot->dev = NULL; }
    i2c_device_config_t c;
    memset(&c, 0, sizeof c);
    c.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    if (addr > 0x7F) {
#if SOC_I2C_SUPPORT_10BIT_ADDR
        c.dev_addr_length = I2C_ADDR_BIT_LEN_10;
#else
        *err = MCS_HAL_ENOTSUP;
        return NULL;
#endif
    }
    c.device_address = (uint16_t)addr;
    c.scl_speed_hz = g_i2c_hz[bus];
    esp_err_t e = i2c_master_bus_add_device(g_i2c[bus], &c, &slot->dev);
    if (e != ESP_OK) { *err = esp_rc(e); slot->dev = NULL; return NULL; }
    slot->addr = (uint16_t)addr;
    slot->hz = g_i2c_hz[bus];
    return slot->dev;
}
static int i2c_rc(esp_err_t e) { return e == ESP_FAIL || e == ESP_ERR_NOT_FOUND ? MCS_HAL_ENODEV : esp_rc(e); }
static int e_i2c_write(void* ctx, int bus, int addr, const uint8_t* d, size_t n) {
    (void)ctx; int err; i2c_master_dev_handle_t h = i2c_dev(bus, addr, &err);
    return h ? i2c_rc(i2c_master_transmit(h, d, n, (int)i2c_to(bus))) : err;
}
static int e_i2c_read(void* ctx, int bus, int addr, uint8_t* buf, size_t n) {
    (void)ctx; int err; i2c_master_dev_handle_t h = i2c_dev(bus, addr, &err);
    return h ? i2c_rc(i2c_master_receive(h, buf, n, (int)i2c_to(bus))) : err;
}
static int e_i2c_write_read(void* ctx, int bus, int addr, const uint8_t* tx, size_t tn, uint8_t* rx, size_t rn) {
    (void)ctx; int err; i2c_master_dev_handle_t h = i2c_dev(bus, addr, &err);
    return h ? i2c_rc(i2c_master_transmit_receive(h, tx, tn, rx, rn, (int)i2c_to(bus))) : err;
}
static int e_i2c_probe(void* ctx, int bus, int addr) {
    (void)ctx;
    int err = 0;
    if (bus < 0 || bus >= MCS_ESP32_I2C_BUSES) return MCS_HAL_ENOTSUP;
    if (!g_i2c[bus] && (err = e_i2c_open(NULL, bus, 0))) return err;
    return i2c_rc(i2c_master_probe(g_i2c[bus], (uint16_t)addr, 20));
}
#else  /* legacy driver (IDF 5.0 / 5.1) */
static bool g_i2c_on[MCS_ESP32_I2C_BUSES];
static void i2c_down(int bus) { if (g_i2c_on[bus]) { i2c_driver_delete((i2c_port_t)bus); g_i2c_on[bus] = false; } }
static int e_i2c_open(void* ctx, int bus, uint32_t freq) {
    (void)ctx;
    int r = i2c_check(bus);
    if (r) return r;
    i2c_down(bus);
    i2c_config_t c;
    memset(&c, 0, sizeof c);
    c.mode = I2C_MODE_MASTER;
    c.sda_io_num = g_i2c_pins[bus].sda;
    c.scl_io_num = g_i2c_pins[bus].scl;
    c.sda_pullup_en = g_i2c_pu[bus] ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    c.scl_pullup_en = g_i2c_pu[bus] ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    c.master.clk_speed = freq ? freq : 100000;
    esp_err_t e = i2c_param_config((i2c_port_t)bus, &c);
    if (e == ESP_OK) e = i2c_driver_install((i2c_port_t)bus, I2C_MODE_MASTER, 0, 0, 0);
    if (e == ESP_OK && g_i2c_glitch[bus] != 7) i2c_filter_enable((i2c_port_t)bus, g_i2c_glitch[bus]);
    g_i2c_on[bus] = e == ESP_OK;
    return esp_rc(e);
}
static int i2c_ready(int bus) {
    if (bus < 0 || bus >= MCS_ESP32_I2C_BUSES) return MCS_HAL_ENOTSUP;
    return g_i2c_on[bus] ? 0 : e_i2c_open(NULL, bus, 0);
}
static int i2c_rc(esp_err_t e) { return e == ESP_FAIL ? MCS_HAL_ENODEV : esp_rc(e); }
static int e_i2c_write(void* ctx, int bus, int addr, const uint8_t* d, size_t n) {
    (void)ctx; int r = i2c_ready(bus);
    return r ? r : i2c_rc(i2c_master_write_to_device((i2c_port_t)bus, (uint8_t)addr, d, n, ms_ticks(i2c_to(bus))));
}
static int e_i2c_read(void* ctx, int bus, int addr, uint8_t* buf, size_t n) {
    (void)ctx; int r = i2c_ready(bus);
    return r ? r : i2c_rc(i2c_master_read_from_device((i2c_port_t)bus, (uint8_t)addr, buf, n, ms_ticks(i2c_to(bus))));
}
static int e_i2c_write_read(void* ctx, int bus, int addr, const uint8_t* tx, size_t tn, uint8_t* rx, size_t rn) {
    (void)ctx; int r = i2c_ready(bus);
    return r ? r : i2c_rc(i2c_master_write_read_device((i2c_port_t)bus, (uint8_t)addr, tx, tn, rx, rn, ms_ticks(i2c_to(bus))));
}
static int e_i2c_probe(void* ctx, int bus, int addr) {
    (void)ctx; int r = i2c_ready(bus);
    return r ? r : i2c_rc(i2c_master_write_to_device((i2c_port_t)bus, (uint8_t)addr, NULL, 0, ms_ticks(20)));
}
#endif
static int e_i2c_configure(void* ctx, int bus, const mcs_i2c_config_t* c) {
    if (bus < 0 || bus >= MCS_ESP32_I2C_BUSES || bus >= (int)SOC_I2C_NUM) return MCS_HAL_ENOTSUP;
    if (c->dma == MCS_ON) { mcs_hal_error("Dma: ESP32 I2C controllers have no DMA"); return MCS_HAL_ENOTSUP; }
    if (c->analog_filter == MCS_ON) { mcs_hal_error("AnalogFilter: not available on ESP32 (use GlitchFilter)"); return MCS_HAL_ENOTSUP; }
    if (c->glitch_filter > 7) { mcs_hal_error("GlitchFilter: 0..7 APB clock cycles"); return MCS_HAL_EINVAL; }
    mcs_esp32_i2c_pins_t pins = { c->sda >= 0 ? c->sda : g_cfg.i2c[bus].sda, c->scl >= 0 ? c->scl : g_cfg.i2c[bus].scl };
    uint8_t pu = c->pullups != MCS_OFF, gl = c->glitch_filter ? c->glitch_filter : 7;
    if (pins.sda != g_i2c_pins[bus].sda || pins.scl != g_i2c_pins[bus].scl || pu != g_i2c_pu[bus] || gl != g_i2c_glitch[bus]) {
        i2c_down(bus);
        g_i2c_pins[bus] = pins; g_i2c_pu[bus] = pu; g_i2c_glitch[bus] = gl;
    }
    g_i2c_to[bus] = c->timeout_ms;
    return e_i2c_open(ctx, bus, c->freq_hz);
}

/* ------------------------------------------------------------------ SPI + QSPI */
static spi_host_device_t spi_host(int bus) {
#if SOC_SPI_PERIPH_NUM > 2
    return bus == 0 ? SPI2_HOST : SPI3_HOST;
#else
    (void)bus;
    return SPI2_HOST;
#endif
}
/* runtime bus settings (SPI.Open(bus, SpiConfig), QSPI.Open(bus, QspiConfig)) */
static mcs_esp32_spi_pins_t g_spi_pins[MCS_ESP32_SPI_BUSES];
static int g_qspi_cs[MCS_ESP32_SPI_BUSES];
static uint8_t g_spi_nodma[MCS_ESP32_SPI_BUSES], g_spi_3w[MCS_ESP32_SPI_BUSES], g_spi_queue[MCS_ESP32_SPI_BUSES];
static uint32_t g_spi_maxsz[MCS_ESP32_SPI_BUSES];
static uint16_t g_spi_delay[MCS_ESP32_SPI_BUSES];
static bool g_spi_bus[MCS_ESP32_SPI_BUSES];
static spi_device_handle_t g_spi_dev[MCS_ESP32_SPI_BUSES], g_qspi_dev[MCS_ESP32_SPI_BUSES];
static mcs_spi_cfg_t g_spi_last[MCS_ESP32_SPI_BUSES];
static uint32_t g_qspi_freq[MCS_ESP32_SPI_BUSES];
static uint8_t g_qspi_mode[MCS_ESP32_SPI_BUSES];

/* transactions: post_cb runs for polled and queued ones; queued (DMA) ones post
 * MCS_HAL_EV_DMA from the SPI interrupt */
typedef struct {
    spi_transaction_ext_t t;          /* first: post_cb gets &t.base */
    volatile uint8_t async, busy;
    uint16_t id;
    int32_t n;
} e_spi_trans_t;
static e_spi_trans_t g_spi_q[MCS_ESP32_SPI_BUSES][2][MCS_HAL_MAX_DMA];   /* [bus][SPI, QSPI][...] */
static void IRAM_ATTR spi_post(spi_transaction_t* t) {
    e_spi_trans_t* w = (e_spi_trans_t*)t;
    if (w->async) { w->async = 0; post_isr(MCS_HAL_EV_DMA, w->id, w->n); }
}
static void spi_reap(spi_device_handle_t dev) {      /* collect finished queued descriptors */
    spi_transaction_t* r;
    while (dev && spi_device_get_trans_result(dev, &r, 0) == ESP_OK) ((e_spi_trans_t*)r)->busy = 0;
}
static uint32_t spi_max(int bus) {
    return g_spi_maxsz[bus] ? g_spi_maxsz[bus] : g_spi_nodma[bus] ? SOC_SPI_MAXIMUM_BUFFER_SIZE : 4092;
}
static int spi_bus_up(int bus) {
    if (bus < 0 || bus >= MCS_ESP32_SPI_BUSES || bus >= (int)SOC_SPI_PERIPH_NUM - 1) return MCS_HAL_ENOTSUP;
    if (g_spi_bus[bus]) return 0;
    if (g_spi_pins[bus].sclk < 0) {
        mcs_hal_error("no SPI pins for this bus - SPI.Open(bus, new SpiConfig { Sck = .., Mosi = .., Miso = .. })");
        return MCS_HAL_ENOTSUP;
    }
    spi_bus_config_t c;
    memset(&c, 0, sizeof c);
    c.sclk_io_num = g_spi_pins[bus].sclk;
    c.mosi_io_num = g_spi_pins[bus].mosi;
    c.miso_io_num = g_spi_pins[bus].miso;
    c.quadwp_io_num = g_spi_pins[bus].quadwp;
    c.quadhd_io_num = g_spi_pins[bus].quadhd;
    c.max_transfer_sz = (int)g_spi_maxsz[bus];        /* 0: 4092 with DMA, 64 without */
    esp_err_t e = spi_bus_initialize(spi_host(bus), &c, g_spi_nodma[bus] ? SPI_DMA_DISABLED : SPI_DMA_CH_AUTO);
    g_spi_bus[bus] = e == ESP_OK;
    return esp_rc(e);
}
static void spi_dev_rm(spi_device_handle_t* d) {
    if (!*d) return;
    spi_reap(*d);
    spi_bus_remove_device(*d);
    *d = NULL;
}
static int spi_dev_add(int bus) {
    const mcs_spi_cfg_t* cfg = &g_spi_last[bus];
    spi_dev_rm(&g_spi_dev[bus]);
    spi_device_interface_config_t d;
    memset(&d, 0, sizeof d);
    d.clock_speed_hz = (int)(cfg->freq_hz ? cfg->freq_hz : 1000000);
    d.mode = cfg->mode;
    d.spics_io_num = -1;               /* chip select is a GPIO driven by MicroCS */
    d.queue_size = g_spi_queue[bus] ? g_spi_queue[bus] : MCS_HAL_MAX_DMA;
    d.input_delay_ns = g_spi_delay[bus];
    d.flags = (cfg->lsb_first ? SPI_DEVICE_BIT_LSBFIRST : 0) | (g_spi_3w[bus] ? SPI_DEVICE_3WIRE | SPI_DEVICE_HALFDUPLEX : 0);
    d.post_cb = spi_post;
    return esp_rc(spi_bus_add_device(spi_host(bus), &d, &g_spi_dev[bus]));
}
static int qspi_dev_add(int bus) {
    spi_dev_rm(&g_qspi_dev[bus]);
    spi_device_interface_config_t d;
    memset(&d, 0, sizeof d);
    d.clock_speed_hz = (int)g_qspi_freq[bus];
    d.mode = g_qspi_mode[bus];
    d.spics_io_num = g_qspi_cs[bus];
    d.queue_size = MCS_HAL_MAX_DMA;
    d.flags = SPI_DEVICE_HALFDUPLEX;
    d.post_cb = spi_post;
    return esp_rc(spi_bus_add_device(spi_host(bus), &d, &g_qspi_dev[bus]));
}
static void spi_bus_down(int bus) {
    spi_dev_rm(&g_spi_dev[bus]);
    spi_dev_rm(&g_qspi_dev[bus]);
    if (g_spi_bus[bus]) { spi_bus_free(spi_host(bus)); g_spi_bus[bus] = false; }
}
static int e_spi_open(void* ctx, int bus, const mcs_spi_cfg_t* cfg) {
    (void)ctx;
    int r = spi_bus_up(bus);
    if (r) return r;
    if (cfg->bits != 8) { mcs_hal_error("Bits: 8 on ESP32"); return MCS_HAL_ENOTSUP; }
    g_spi_last[bus] = *cfg;
    return spi_dev_add(bus);
}
/* new pins / DMA / transfer size: free and re-create the bus, keeping the QSPI device */
static int spi_bus_settings(int bus, const mcs_esp32_spi_pins_t* pins, int qcs, uint8_t nodma, uint32_t maxsz) {
    if (!memcmp(pins, &g_spi_pins[bus], sizeof *pins) && qcs == g_qspi_cs[bus] && nodma == g_spi_nodma[bus] && maxsz == g_spi_maxsz[bus]) return 0;
    bool spi = g_spi_dev[bus] != NULL;
    spi_bus_down(bus);
    g_spi_pins[bus] = *pins; g_qspi_cs[bus] = qcs; g_spi_nodma[bus] = nodma; g_spi_maxsz[bus] = maxsz;
    int r = spi_bus_up(bus);
    if (!r && spi) r = spi_dev_add(bus);
    if (!r && g_qspi_freq[bus]) r = qspi_dev_add(bus);
    return r;
}
static int e_spi_configure(void* ctx, int bus, const mcs_spi_config_t* c) {
    if (bus < 0 || bus >= MCS_ESP32_SPI_BUSES || bus >= (int)SOC_SPI_PERIPH_NUM - 1) return MCS_HAL_ENOTSUP;
    if (c->dma == MCS_OFF && c->max_transfer > SOC_SPI_MAXIMUM_BUFFER_SIZE) {
        mcs_hal_error("MaxTransfer: at most 64 bytes without DMA");
        return MCS_HAL_EINVAL;
    }
    mcs_esp32_spi_pins_t pins = g_spi_pins[bus];
    const mcs_esp32_spi_pins_t* d = &g_cfg.spi[bus];
    pins.sclk = c->sck >= 0 ? c->sck : d->sclk;
    pins.mosi = c->mosi >= 0 ? c->mosi : d->mosi;
    pins.miso = c->miso >= 0 ? c->miso : c->three_wire ? -1 : d->miso;
    int r = spi_bus_settings(bus, &pins, g_qspi_cs[bus], c->dma == MCS_OFF, c->max_transfer);
    if (r) return r;
    g_spi_3w[bus] = c->three_wire ? 1 : 0;
    g_spi_delay[bus] = c->input_delay_ns;
    g_spi_queue[bus] = (uint8_t)mcs_hal_opt(e_options, &c->opts, "SPI", "queue_size", MCS_HAL_MAX_DMA);
    return e_spi_open(ctx, bus, &c->bus);
}
static spi_device_handle_t spi_dev_of(void* ctx, int bus, int* err) {
    *err = 0;
    if (bus < 0 || bus >= MCS_ESP32_SPI_BUSES) { *err = MCS_HAL_ENOTSUP; return NULL; }
    if (!g_spi_dev[bus]) {
        mcs_spi_cfg_t c = { 1000000, 0, 8, 0, 0 };
        if ((*err = e_spi_open(ctx, bus, &c))) return NULL;
    }
    spi_reap(g_spi_dev[bus]);
    return g_spi_dev[bus];
}
static void spi_fill(spi_transaction_t* t, int bus, const uint8_t* tx, uint8_t* rx, size_t n) {
    memset(t, 0, sizeof *t);
    t->length = n * 8;                  /* half duplex read: length = rxlength, no MOSI phase */
    t->rxlength = g_spi_3w[bus] && rx ? n * 8 : 0;
    t->tx_buffer = tx;
    t->rx_buffer = rx;
}
static int e_spi_transfer(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n) {
    int err;
    spi_device_handle_t dev = spi_dev_of(ctx, bus, &err);
    if (!dev) return err;
    size_t max = spi_max(bus);
    for (size_t off = 0; off < n;) {
        size_t k = n - off < max ? n - off : max;
        e_spi_trans_t w;
        spi_fill(&w.t.base, bus, tx ? tx + off : NULL, rx ? rx + off : NULL, k);
        w.async = 0;
        esp_err_t e = spi_device_polling_transmit(dev, &w.t.base);
        if (e != ESP_OK) return esp_rc(e);
        off += k;
    }
    return 0;
}
static e_spi_trans_t* spi_slot(int bus, int which) {
    for (int i = 0; i < MCS_HAL_MAX_DMA; i++)
        if (!g_spi_q[bus][which][i].busy) return &g_spi_q[bus][which][i];
    return NULL;
}
static int spi_queue(spi_device_handle_t dev, e_spi_trans_t* w, int id, size_t n) {
    w->id = (uint16_t)id; w->n = (int32_t)n; w->busy = 1; w->async = 1;
    esp_err_t e = spi_device_queue_trans(dev, &w->t.base, 0);
    if (e != ESP_OK) { w->busy = 0; w->async = 0; }
    return esp_rc(e);
}
static int e_spi_transfer_async(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n, int id) {
    int err;
    spi_device_handle_t dev = spi_dev_of(ctx, bus, &err);
    if (!dev) return err;
    if (n > spi_max(bus)) return MCS_HAL_ENOTSUP;      /* the library runs it synchronously, in pieces */
    e_spi_trans_t* w = spi_slot(bus, 0);
    if (!w) return MCS_HAL_EBUSY;
    spi_fill(&w->t.base, bus, tx, rx, n);
    return spi_queue(dev, w, id, n);
}
static int e_qspi_open(void* ctx, int bus, uint32_t freq) {
    (void)ctx;
    int r = spi_bus_up(bus);
    if (r) return r;
    g_qspi_freq[bus] = freq ? freq : 10000000;
    return qspi_dev_add(bus);
}
static int e_qspi_configure(void* ctx, int bus, const mcs_qspi_config_t* c) {
    if (bus < 0 || bus >= MCS_ESP32_SPI_BUSES || bus >= (int)SOC_SPI_PERIPH_NUM - 1) return MCS_HAL_ENOTSUP;
    if (c->ddr) { mcs_hal_error("Ddr: not supported by the ESP32 SPI master"); return MCS_HAL_ENOTSUP; }
    if (c->cs_high_cycles || c->sample_shift) { mcs_hal_error("CsHighCycles / SampleShift: STM32 QUADSPI settings, not available on ESP32"); return MCS_HAL_ENOTSUP; }
    if (c->mode != 0 && c->mode != 3) { mcs_hal_error("Mode: 0 or 3"); return MCS_HAL_EINVAL; }
    const mcs_esp32_spi_pins_t* d = &g_cfg.spi[bus];
    mcs_esp32_spi_pins_t pins = {
        c->sck >= 0 ? c->sck : d->sclk, c->io[0] >= 0 ? c->io[0] : d->mosi, c->io[1] >= 0 ? c->io[1] : d->miso,
        c->io[2] >= 0 ? c->io[2] : d->quadwp, c->io[3] >= 0 ? c->io[3] : d->quadhd };
    int r = spi_bus_settings(bus, &pins, c->cs >= 0 ? c->cs : g_cfg.qspi_cs[bus], c->dma == MCS_OFF, g_spi_maxsz[bus]);
    if (r) return r;
    g_qspi_mode[bus] = c->mode;
    return e_qspi_open(ctx, bus, c->freq_hz);
}
static int qspi_fill(spi_transaction_ext_t* t, const mcs_qspi_cmd_t* q, const uint8_t* tx, uint8_t* rx, size_t n) {
    if (q->data_lines > 4 || q->addr_lines > 4 || q->instr_lines > 4) return MCS_HAL_ENOTSUP;
    memset(t, 0, sizeof *t);
    t->base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
    uint8_t lines = q->data_lines ? q->data_lines : 1;
    if (lines == 4) t->base.flags |= SPI_TRANS_MODE_QIO;
    else if (lines == 2) t->base.flags |= SPI_TRANS_MODE_DIO;
    if (lines > 1 && q->addr_lines > 1) t->base.flags |= SPI_TRANS_MULTILINE_ADDR;
    if (lines > 1 && q->instr_lines > 1) t->base.flags |= SPI_TRANS_MULTILINE_CMD;
    t->command_bits = q->instr_lines ? 8 : 0;
    t->address_bits = (uint8_t)(q->addr_bytes * 8);
    t->dummy_bits = q->dummy_cycles;
    t->base.cmd = q->instruction;
    t->base.addr = q->address;
    if (tx) { t->base.length = n * 8; t->base.tx_buffer = tx; }
    else if (rx) { t->base.rxlength = n * 8; t->base.rx_buffer = rx; }
    return 0;
}
static int e_qspi_command(void* ctx, int bus, const mcs_qspi_cmd_t* q, const uint8_t* tx, uint8_t* rx, size_t n) {
    (void)ctx;
    if (bus < 0 || bus >= MCS_ESP32_SPI_BUSES || !g_qspi_dev[bus]) return MCS_HAL_ENOTSUP;
    spi_reap(g_qspi_dev[bus]);
    e_spi_trans_t w;
    int r = qspi_fill(&w.t, q, tx, rx, n);
    if (r) return r;
    w.async = 0;
    return esp_rc(spi_device_polling_transmit(g_qspi_dev[bus], &w.t.base));
}
static int e_qspi_command_async(void* ctx, int bus, const mcs_qspi_cmd_t* q, const uint8_t* tx, uint8_t* rx, size_t n, int id) {
    (void)ctx;
    if (bus < 0 || bus >= MCS_ESP32_SPI_BUSES || !g_qspi_dev[bus]) return MCS_HAL_ENOTSUP;
    if (n > spi_max(bus)) return MCS_HAL_ENOTSUP;
    spi_reap(g_qspi_dev[bus]);
    e_spi_trans_t* w = spi_slot(bus, 1);
    if (!w) return MCS_HAL_EBUSY;
    int r = qspi_fill(&w->t, q, tx, rx, n);
    if (r) return r;
    return spi_queue(g_qspi_dev[bus], w, id, n);
}

/* ------------------------------------------------------------------ DMA memory */
static void* e_dma_alloc(void* ctx, size_t n) { (void)ctx; return heap_caps_aligned_alloc(4, n ? n : 4, MALLOC_CAP_DMA | MALLOC_CAP_8BIT); }
static void e_dma_free(void* ctx, void* p) { (void)ctx; heap_caps_free(p); }
#if MCS_ESP_MEMCPY
static async_memcpy_handle_t g_mcp;
static int32_t g_mcp_n[8];
static bool IRAM_ATTR mcp_done(async_memcpy_handle_t h, async_memcpy_event_t* ev, void* arg) {
    (void)h; (void)ev;
    int id = (int)(intptr_t)arg;
    mcs_hal_event_t e = { MCS_HAL_EV_DMA, 0, (uint16_t)id, g_mcp_n[id & 7] };
    BaseType_t woken = pdFALSE;
    if (g_events) xQueueSendFromISR(g_events, &e, &woken);
    return woken == pdTRUE;
}
static int e_dma_copy(void* ctx, void* dst, const void* src, size_t n, unsigned flags, int id) {
    (void)ctx;
    /* GDMA copies SRAM to SRAM; fills, flash sources and odd sizes run on the CPU */
    if (flags || !n || ((uintptr_t)dst | (uintptr_t)src | n) & 3u || !esp_ptr_dma_capable(dst) || !esp_ptr_dma_capable(src))
        return MCS_HAL_ENOTSUP;
    if (!g_mcp) {
        async_memcpy_config_t c = ASYNC_MEMCPY_DEFAULT_CONFIG();
        c.backlog = MCS_HAL_MAX_DMA;
        if (esp_async_memcpy_install(&c, &g_mcp) != ESP_OK) { g_mcp = NULL; return MCS_HAL_ENOTSUP; }
    }
    g_mcp_n[id & 7] = (int32_t)n;
    esp_err_t e = esp_async_memcpy(g_mcp, dst, (void*)src, n, mcp_done, (void*)(intptr_t)id);
    return e == ESP_ERR_INVALID_ARG ? MCS_HAL_ENOTSUP : esp_rc(e);
}
#endif

/* ------------------------------------------------------------------ ADC / DAC */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#define MCS_ESP_ATTEN ADC_ATTEN_DB_12
#else
#define MCS_ESP_ATTEN ADC_ATTEN_DB_11
#endif
static adc_oneshot_unit_handle_t g_adc[2];
static adc_cali_handle_t g_cali[2][10];
static uint16_t g_adc_conf[2];        /* configured channels */
static uint8_t g_adc_atten[2][10], g_adc_bits[2][10], g_adc_avg[2][10];   /* ADC.Configure: 0 = default */
static adc_atten_t adc_atten_of(int u, int ch) {
    switch (g_adc_atten[u][ch]) {
    case MCS_ADC_ATTEN_0DB: return ADC_ATTEN_DB_0;
    case MCS_ADC_ATTEN_2_5DB: return ADC_ATTEN_DB_2_5;
    case MCS_ADC_ATTEN_6DB: return ADC_ATTEN_DB_6;
    default: return MCS_ESP_ATTEN;
    }
}
static void adc_cali_drop(int u, int ch) {
    if (!g_cali[u][ch]) return;
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_delete_scheme_curve_fitting(g_cali[u][ch]);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_delete_scheme_line_fitting(g_cali[u][ch]);
#endif
    g_cali[u][ch] = NULL;
}
static int adc_prepare(int gpio, adc_unit_t* unit, adc_channel_t* ch) {
    if (adc_oneshot_io_to_channel(gpio, unit, ch) != ESP_OK) return MCS_HAL_ENOTSUP;
    int u = (int)*unit;
    if (u < 0 || u > 1 || (int)*ch >= 10) return MCS_HAL_ENOTSUP;
    if (!g_adc[u]) {
        adc_oneshot_unit_init_cfg_t uc;
        memset(&uc, 0, sizeof uc);
        uc.unit_id = *unit;
        esp_err_t e = adc_oneshot_new_unit(&uc, &g_adc[u]);
        if (e != ESP_OK) return esp_rc(e);
    }
    if (!(g_adc_conf[u] & (1u << *ch))) {
        adc_atten_t at = adc_atten_of(u, *ch);
        adc_bitwidth_t bw = g_adc_bits[u][*ch] ? (adc_bitwidth_t)g_adc_bits[u][*ch] : ADC_BITWIDTH_DEFAULT;
        adc_oneshot_chan_cfg_t cc;
        memset(&cc, 0, sizeof cc);
        cc.atten = at;
        cc.bitwidth = bw;
        esp_err_t e = adc_oneshot_config_channel(g_adc[u], *ch, &cc);
        if (e != ESP_OK) return esp_rc(e);
        g_adc_conf[u] |= (uint16_t)(1u << *ch);
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t k;
        memset(&k, 0, sizeof k);
        k.unit_id = *unit;
        k.chan = *ch;
        k.atten = at;
        k.bitwidth = bw;
        if (adc_cali_create_scheme_curve_fitting(&k, &g_cali[u][*ch]) != ESP_OK) g_cali[u][*ch] = NULL;
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_line_fitting_config_t k;
        memset(&k, 0, sizeof k);
        k.unit_id = *unit;
        k.atten = at;
        k.bitwidth = bw;
        if (adc_cali_create_scheme_line_fitting(&k, &g_cali[u][*ch]) != ESP_OK) g_cali[u][*ch] = NULL;
#endif
    }
    return 0;
}
static int adc_raw(adc_unit_t unit, adc_channel_t ch, int* raw) {
    int n = g_adc_avg[unit][ch] ? g_adc_avg[unit][ch] : 1, sum = 0;
    for (int i = 0; i < n; i++) {             /* Oversample: averaged in software */
        int v = 0;
        int r = esp_rc(adc_oneshot_read(g_adc[unit], ch, &v));
        if (r) return r;
        sum += v;
    }
    *raw = sum / n;
    return 0;
}
static int e_adc_read(void* ctx, int gpio) {
    (void)ctx;
    adc_unit_t unit; adc_channel_t ch;
    int r = adc_prepare(gpio, &unit, &ch);
    if (r) return r;
    int raw = 0;
    r = adc_raw(unit, ch, &raw);
    return r ? r : raw;
}
static int e_adc_read_mv(void* ctx, int gpio) {
    (void)ctx;
    adc_unit_t unit; adc_channel_t ch;
    int r = adc_prepare(gpio, &unit, &ch);
    if (r) return r;
    int raw = 0, mv = 0;
    if ((r = adc_raw(unit, ch, &raw))) return r;
    if (g_cali[unit][ch] && adc_cali_raw_to_voltage(g_cali[unit][ch], raw, &mv) == ESP_OK) return mv;
    /* uncalibrated: approximate full scale per attenuation */
    static const uint16_t fs[] = { 3100, 950, 1250, 1750, 3100 };
    int bits = g_adc_bits[unit][ch] ? g_adc_bits[unit][ch] : SOC_ADC_RTC_MAX_BITWIDTH;
    return (int)((int64_t)raw * fs[g_adc_atten[unit][ch] <= 4 ? g_adc_atten[unit][ch] : 0] / ((1 << bits) - 1));
}
static int e_adc_configure(void* ctx, int gpio, const mcs_adc_config_t* c) {
    (void)ctx;
    adc_unit_t unit; adc_channel_t ch;
    if (adc_oneshot_io_to_channel(gpio, &unit, &ch) != ESP_OK || (int)unit > 1 || (int)ch >= 10) return MCS_HAL_ENOTSUP;
    if (c->attenuation > MCS_ADC_ATTEN_12DB) return MCS_HAL_EINVAL;
    if (c->resolution_bits && (c->resolution_bits < SOC_ADC_RTC_MIN_BITWIDTH || c->resolution_bits > SOC_ADC_RTC_MAX_BITWIDTH)) {
#if SOC_ADC_RTC_MIN_BITWIDTH == SOC_ADC_RTC_MAX_BITWIDTH
        mcs_hal_error("ResolutionBits: this chip's ADC is fixed at its native width");
#else
        mcs_hal_error("ResolutionBits: 9..12 on ESP32, 13 on ESP32-S2");
#endif
        return MCS_HAL_EINVAL;
    }
    if (c->oversample > 64 || (c->oversample & (c->oversample - 1))) { mcs_hal_error("Oversample: 0, 1, 2, 4 .. 64"); return MCS_HAL_EINVAL; }
    if (c->sample_time) { mcs_hal_error("SampleTime: fixed by the ESP32 ADC controller"); return MCS_HAL_ENOTSUP; }
    int u = (int)unit;
    g_adc_atten[u][ch] = c->attenuation;
    g_adc_bits[u][ch] = c->resolution_bits;
    g_adc_avg[u][ch] = c->oversample;
    g_adc_conf[u] &= (uint16_t)~(1u << ch);
    adc_cali_drop(u, ch);
    return adc_prepare(gpio, &unit, &ch);
}
#if SOC_DAC_SUPPORTED
static dac_oneshot_handle_t g_dac[2];
static int e_dac_write(void* ctx, int ch, uint32_t v) {
    (void)ctx;
    if (ch < 0 || ch > 1) return MCS_HAL_ENOTSUP;
    if (!g_dac[ch]) {
        dac_oneshot_config_t c = { .chan_id = ch ? DAC_CHAN_1 : DAC_CHAN_0 };
        esp_err_t e = dac_oneshot_new_channel(&c, &g_dac[ch]);
        if (e != ESP_OK) return esp_rc(e);
    }
    return esp_rc(dac_oneshot_output_voltage(g_dac[ch], (uint8_t)(v > 255 ? 255 : v)));
}
#endif

/* ------------------------------------------------------------------ PWM (LEDC) */
static uint8_t g_pwm_bits[MCS_ESP32_PWM_CHANNELS];
static uint32_t g_pwm_freq[4];
static uint8_t g_pwm_res[4];                          /* PwmConfig.ResolutionBits per LEDC timer, 0 = auto */
static bool g_pwm_on[MCS_ESP32_PWM_CHANNELS];
static int8_t g_pwm_timer[MCS_ESP32_PWM_CHANNELS];    /* -1 = channel % 4 */
static int16_t g_pwm_pin[MCS_ESP32_PWM_CHANNELS];     /* -1 = board config */
static uint8_t g_pwm_inv[MCS_ESP32_PWM_CHANNELS];
static int pwm_timer(int ch) { return g_pwm_timer[ch] >= 0 ? g_pwm_timer[ch] : (ch & 3); }
static int pwm_pin(int ch) { return g_pwm_pin[ch] >= 0 ? g_pwm_pin[ch] : g_cfg.pwm[ch]; }
static int e_pwm_set16(void* ctx, int ch, uint32_t freq, uint16_t duty) {
    (void)ctx;
    if (ch < 0 || ch >= MCS_ESP32_PWM_CHANNELS || ch >= (int)SOC_LEDC_CHANNEL_NUM) return MCS_HAL_ENOTSUP;
    if (pwm_pin(ch) < 0) { mcs_hal_error("no pin for this PWM channel - PWM.Configure(ch, new PwmConfig { Pin = .. })"); return MCS_HAL_ENOTSUP; }
    if (!freq) return MCS_HAL_EINVAL;
    int timer = pwm_timer(ch);
    if (freq != g_pwm_freq[timer] || !g_pwm_on[ch]) {
        /* widest resolution that still divides the LEDC clock (or the configured one) */
        int bits = 1;
        while (bits < SOC_LEDC_TIMER_BIT_WIDTH && (MCS_ESP32_LEDC_CLK_HZ >> (bits + 1)) >= freq) bits++;
        if (g_pwm_res[timer]) bits = g_pwm_res[timer];
        esp_err_t e;
        do {
            ledc_timer_config_t t;
            memset(&t, 0, sizeof t);
            t.speed_mode = LEDC_LOW_SPEED_MODE;
            t.duty_resolution = (ledc_timer_bit_t)bits;
            t.timer_num = (ledc_timer_t)timer;
            t.freq_hz = freq;
            t.clk_cfg = LEDC_AUTO_CLK;
            e = ledc_timer_config(&t);
        } while (e != ESP_OK && !g_pwm_res[timer] && --bits > 0);
        if (e != ESP_OK) {
            if (g_pwm_res[timer]) mcs_hal_error("ResolutionBits: too fine for this frequency (bits + log2(freq) must stay below the LEDC clock)");
            return esp_rc(e);
        }
        g_pwm_freq[timer] = freq;
        for (int i = 0; i < MCS_ESP32_PWM_CHANNELS; i++) if (pwm_timer(i) == timer) g_pwm_bits[i] = (uint8_t)bits;
        if (!g_pwm_on[ch]) {
            ledc_channel_config_t c;
            memset(&c, 0, sizeof c);
            c.gpio_num = pwm_pin(ch);
            c.speed_mode = LEDC_LOW_SPEED_MODE;
            c.channel = (ledc_channel_t)ch;
            c.timer_sel = (ledc_timer_t)timer;
            c.duty = 0;
            c.flags.output_invert = g_pwm_inv[ch];
            e = ledc_channel_config(&c);
            if (e != ESP_OK) return esp_rc(e);
            g_pwm_on[ch] = true;
        }
    }
    uint32_t max = (1u << g_pwm_bits[ch]);
    uint32_t d = (uint32_t)(((uint64_t)duty * max) / 65535u);
    esp_err_t e = ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, d);
    if (e == ESP_OK) e = ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch);
    return esp_rc(e);
}
static int e_pwm_configure(void* ctx, int ch, const mcs_pwm_config_t* c) {
    if (ch < 0 || ch >= MCS_ESP32_PWM_CHANNELS || ch >= (int)SOC_LEDC_CHANNEL_NUM) return MCS_HAL_ENOTSUP;
    if (c->center_aligned) { mcs_hal_error("CenterAligned: the LEDC counter only counts up"); return MCS_HAL_ENOTSUP; }
    if (c->resolution_bits > SOC_LEDC_TIMER_BIT_WIDTH) { mcs_hal_error("ResolutionBits: 1..14 (1..20 on ESP32)"); return MCS_HAL_EINVAL; }
    int timer = (int)mcs_hal_opt(e_options, &c->opts, "PWM", "timer", ch & 3);
    int old = pwm_pin(ch);
    if (g_pwm_on[ch]) { ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, 0); g_pwm_on[ch] = false; }
    if (old >= 0 && old != (c->pin >= 0 ? c->pin : g_cfg.pwm[ch])) gpio_reset_pin((gpio_num_t)old);   /* detach the old pad */
    g_pwm_timer[ch] = (int8_t)timer;
    g_pwm_pin[ch] = c->pin;
    g_pwm_inv[ch] = c->invert ? 1 : 0;
    if (g_pwm_res[timer] != c->resolution_bits) { g_pwm_res[timer] = c->resolution_bits; g_pwm_freq[timer] = 0; }
    return c->freq_hz ? e_pwm_set16(ctx, ch, c->freq_hz, 0) : 0;    /* starts at 0 % (inactive level) */
}
static int e_pwm_set(void* ctx, int ch, uint32_t freq, uint16_t permille) {
    return e_pwm_set16(ctx, ch, freq, (uint16_t)((uint32_t)permille * 65535u / 1000u));
}
static int e_pwm_stop(void* ctx, int ch) {
    (void)ctx;
    if (ch < 0 || ch >= MCS_ESP32_PWM_CHANNELS || !g_pwm_on[ch]) return ch >= 0 && ch < MCS_ESP32_PWM_CHANNELS ? 0 : MCS_HAL_ENOTSUP;
    g_pwm_on[ch] = false;
    return esp_rc(ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch, 0));
}

/* ------------------------------------------------------------------ timers (gptimer) */
static gptimer_handle_t g_tim[MCS_ESP32_TIMERS];
static volatile uint32_t g_tim_count[MCS_ESP32_TIMERS];
static volatile uint8_t g_tim_once[MCS_ESP32_TIMERS];
static bool IRAM_ATTR tim_isr(gptimer_handle_t t, const gptimer_alarm_event_data_t* ed, void* ud) {
    (void)ed;
    int i = (int)(intptr_t)ud;
    g_tim_count[i]++;
    if (g_tim_once[i]) gptimer_stop(t);
    mcs_hal_event_t ev = { MCS_HAL_EV_TIMER, 0, (uint16_t)i, (int32_t)g_tim_count[i] };
    BaseType_t woken = pdFALSE;
    if (g_events) xQueueSendFromISR(g_events, &ev, &woken);
    return woken == pdTRUE;
}
static int e_timer_start(void* ctx, int i, uint32_t period_us, int periodic) {
    (void)ctx;
    if (i < 0 || i >= MCS_ESP32_HW_TIMERS) return MCS_HAL_ENOTSUP;
    if (!period_us) return MCS_HAL_EINVAL;
    esp_err_t e;
    if (!g_tim[i]) {
        gptimer_config_t c;
        memset(&c, 0, sizeof c);
        c.clk_src = GPTIMER_CLK_SRC_DEFAULT;
        c.direction = GPTIMER_COUNT_UP;
        c.resolution_hz = 1000000;
        if ((e = gptimer_new_timer(&c, &g_tim[i])) != ESP_OK) { g_tim[i] = NULL; return esp_rc(e); }
        gptimer_event_callbacks_t cb = { .on_alarm = tim_isr };
        if ((e = gptimer_register_event_callbacks(g_tim[i], &cb, (void*)(intptr_t)i)) != ESP_OK) return esp_rc(e);
        if ((e = gptimer_enable(g_tim[i])) != ESP_OK) return esp_rc(e);
    } else gptimer_stop(g_tim[i]);
    g_tim_count[i] = 0;
    g_tim_once[i] = !periodic;
    gptimer_alarm_config_t a;
    memset(&a, 0, sizeof a);
    a.alarm_count = period_us;
    a.reload_count = 0;
    a.flags.auto_reload_on_alarm = periodic ? 1 : 0;
    gptimer_set_raw_count(g_tim[i], 0);
    if ((e = gptimer_set_alarm_action(g_tim[i], &a)) != ESP_OK) return esp_rc(e);
    return esp_rc(gptimer_start(g_tim[i]));
}
static int e_timer_stop(void* ctx, int i) {
    (void)ctx;
    if (i < 0 || i >= MCS_ESP32_HW_TIMERS) return MCS_HAL_ENOTSUP;
    if (g_tim[i]) gptimer_stop(g_tim[i]);
    return 0;
}

/* ------------------------------------------------------------------ I2S */
#if SOC_I2S_SUPPORTED
static i2s_chan_handle_t g_i2s_tx[2], g_i2s_rx[2];
static int e_i2s_close(void* ctx, int bus) {
    (void)ctx;
    if (bus < 0 || bus >= (int)SOC_I2S_NUM || bus > 1) return MCS_HAL_ENOTSUP;
    if (g_i2s_tx[bus]) { i2s_channel_disable(g_i2s_tx[bus]); i2s_del_channel(g_i2s_tx[bus]); g_i2s_tx[bus] = NULL; }
    if (g_i2s_rx[bus]) { i2s_channel_disable(g_i2s_rx[bus]); i2s_del_channel(g_i2s_rx[bus]); g_i2s_rx[bus] = NULL; }
    return 0;
}
static int e_i2s_open(void* ctx, int bus, const mcs_i2s_cfg_t* c) {
    if (bus < 0 || bus >= (int)SOC_I2S_NUM || bus > 1 || g_cfg.i2s[bus].bclk < 0) return MCS_HAL_ENOTSUP;
    e_i2s_close(ctx, bus);
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)bus, I2S_ROLE_MASTER);
    esp_err_t e = i2s_new_channel(&cc, (c->direction & MCS_I2S_TX) ? &g_i2s_tx[bus] : NULL,
                                  (c->direction & MCS_I2S_RX) ? &g_i2s_rx[bus] : NULL);
    if (e != ESP_OK) return esp_rc(e);
    i2s_data_bit_width_t bw = c->bits == 16 ? I2S_DATA_BIT_WIDTH_16BIT : c->bits == 24 ? I2S_DATA_BIT_WIDTH_24BIT : I2S_DATA_BIT_WIDTH_32BIT;
    i2s_slot_mode_t sm = c->channels == 1 ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    i2s_std_config_t sc;
    memset(&sc, 0, sizeof sc);
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(c->sample_rate);
    sc.clk_cfg = clk;
    if (c->format == MCS_I2S_MSB) { i2s_std_slot_config_t s = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(bw, sm); sc.slot_cfg = s; }
    else if (c->format == MCS_I2S_PCM) { i2s_std_slot_config_t s = I2S_STD_PCM_SLOT_DEFAULT_CONFIG(bw, sm); sc.slot_cfg = s; }
    else { i2s_std_slot_config_t s = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bw, sm); sc.slot_cfg = s; }
    sc.gpio_cfg.mclk = (gpio_num_t)g_cfg.i2s[bus].mclk;
    sc.gpio_cfg.bclk = (gpio_num_t)g_cfg.i2s[bus].bclk;
    sc.gpio_cfg.ws = (gpio_num_t)g_cfg.i2s[bus].ws;
    sc.gpio_cfg.dout = (gpio_num_t)g_cfg.i2s[bus].dout;
    sc.gpio_cfg.din = (gpio_num_t)g_cfg.i2s[bus].din;
    i2s_chan_handle_t hs[2] = { g_i2s_tx[bus], g_i2s_rx[bus] };
    for (int k = 0; k < 2; k++) {
        if (!hs[k]) continue;
        if ((e = i2s_channel_init_std_mode(hs[k], &sc)) != ESP_OK || (e = i2s_channel_enable(hs[k])) != ESP_OK) {
            e_i2s_close(ctx, bus);
            return esp_rc(e);
        }
    }
    return 0;
}
static int e_i2s_write(void* ctx, int bus, const uint8_t* d, size_t n, uint32_t timeout_ms) {
    (void)ctx;
    if (bus < 0 || bus > 1 || !g_i2s_tx[bus]) return MCS_HAL_ENOTSUP;
    size_t done = 0;
    esp_err_t e = i2s_channel_write(g_i2s_tx[bus], d, n, &done, timeout_ms);
    return e == ESP_OK || (e == ESP_ERR_TIMEOUT && done) ? (int)done : esp_rc(e);
}
static int e_i2s_read(void* ctx, int bus, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    (void)ctx;
    if (bus < 0 || bus > 1 || !g_i2s_rx[bus]) return MCS_HAL_ENOTSUP;
    size_t done = 0;
    esp_err_t e = i2s_channel_read(g_i2s_rx[bus], buf, n, &done, timeout_ms);
    return e == ESP_OK || (e == ESP_ERR_TIMEOUT && done) ? (int)done : esp_rc(e);
}
#endif

/* Native CAN is shared with Arduino-ESP32 (legacy TWAI + IDF 5.5 FD nodes). */
#define MCS_CAN_TX_DEFAULT g_cfg.can.tx
#define MCS_CAN_RX_DEFAULT g_cfg.can.rx
#include "mcs_esp_can.h"

/* ------------------------------------------------------------------ watchdog / RTC / system */
static int e_wdt_start(void* ctx, uint32_t timeout_ms) {
    (void)ctx;
    esp_task_wdt_config_t c = { .timeout_ms = timeout_ms, .idle_core_mask = 0, .trigger_panic = true };
    esp_err_t e = esp_task_wdt_init(&c);
    if (e == ESP_ERR_INVALID_STATE) e = esp_task_wdt_reconfigure(&c);   /* already running (sdkconfig) */
    if (e != ESP_OK) return esp_rc(e);
    e = esp_task_wdt_add(NULL);                 /* watch the task running the VM */
    return e == ESP_ERR_INVALID_ARG ? 0 : esp_rc(e);   /* already subscribed */
}
static int e_wdt_feed(void* ctx) { (void)ctx; return esp_rc(esp_task_wdt_reset()); }
static int e_rtc_get(void* ctx, uint32_t* secs) {
    (void)ctx;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *secs = (uint32_t)tv.tv_sec;
    return 0;
}
static int e_rtc_set(void* ctx, uint32_t secs) {
    (void)ctx;
    struct timeval tv = { .tv_sec = (time_t)secs, .tv_usec = 0 };
    return settimeofday(&tv, NULL) == 0 ? 0 : MCS_HAL_ERR;
}
static uint32_t e_micros(void* ctx) { (void)ctx; return (uint32_t)esp_timer_get_time(); }
static void e_delay_us(void* ctx, uint32_t us) { (void)ctx; esp_rom_delay_us(us); }
static int e_reset(void* ctx) { (void)ctx; esp_restart(); return 0; }
static int e_unique_id(void* ctx, uint8_t* buf, size_t cap) {
    (void)ctx;
    uint8_t mac[8] = { 0 };
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return MCS_HAL_ERR;
    size_t n = cap < 6 ? cap : 6;
    memcpy(buf, mac, n);
    return (int)n;
}

uint32_t mcs_esp32_ticks(void* ud) { (void)ud; return (uint32_t)(esp_timer_get_time() / 1000); }
void mcs_esp32_delay(void* ud, uint32_t ms) { (void)ud; vTaskDelay(ms_ticks(ms)); }

/* ------------------------------------------------------------------ console */
static int con_uart_read(void* ud, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    return e_uart_read(NULL, (int)(intptr_t)ud, buf, n, timeout_ms);
}
static void con_uart_write(void* ud, const char* s, size_t n) {
    e_uart_write(NULL, (int)(intptr_t)ud, (const uint8_t*)s, n);
}
mcs_transport_t mcs_esp32_console_uart(int port, uint32_t baud) {
    mcs_uart_cfg_t c = { baud ? baud : 115200, 8, MCS_UART_PARITY_NONE, 1, 0 };
    e_uart_config(NULL, port, &c);
    mcs_transport_t t = { con_uart_read, con_uart_write, (void*)(intptr_t)port };
    return t;
}
#if SOC_USB_SERIAL_JTAG_SUPPORTED
/* USB-Serial-JTAG receive buffer. ESP-IDF's driver reads every USB packet into it from the
 * interrupt and silently drops what does not fit (no USB flow control), so it must hold
 * everything that can arrive while MicroCS is busy (a flash erase, a long statement).
 * Uploads use the shell's windowed `put` on top (MCS_SHELL_PUT_WINDOW). */
#ifndef MCS_ESP32_USB_RXBUF
#define MCS_ESP32_USB_RXBUF 4096
#endif
#ifndef MCS_ESP32_USB_TXBUF
#define MCS_ESP32_USB_TXBUF 1024
#endif
static esp_err_t con_usb_install(void) {
    usb_serial_jtag_driver_config_t c = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    c.rx_buffer_size = MCS_ESP32_USB_RXBUF;
    c.tx_buffer_size = MCS_ESP32_USB_TXBUF;
    return usb_serial_jtag_driver_install(&c);    /* ESP_ERR_INVALID_STATE: the app installed it already */
}
static int con_usb_read(void* ud, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    (void)ud;
    int r = usb_serial_jtag_read_bytes(buf, n, ms_ticks(timeout_ms));
    return r < 0 ? 0 : r;
}
static void con_usb_write(void* ud, const char* s, size_t n) {
    (void)ud;
    while (n) {
        int w = usb_serial_jtag_write_bytes(s, n, ms_ticks(50));
        if (w <= 0) return;           /* host not listening: drop */
        s += w; n -= (size_t)w;
    }
}
#endif
mcs_transport_t mcs_esp32_console_usb(void) {
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    esp_err_t e = con_usb_install();
    if (e == ESP_OK || e == ESP_ERR_INVALID_STATE) {      /* INVALID_STATE: already installed */
        mcs_transport_t t = { con_usb_read, con_usb_write, NULL };
        return t;
    }
#endif
    return mcs_esp32_console_uart(0, 115200);
}

/* both consoles at once: output goes to UART0 (the USB-UART bridge, where the
 * boot log is) and to USB-Serial-JTAG (when a host is attached), input is taken
 * from whichever has data - works whichever USB connector the board is on */
#if SOC_USB_SERIAL_JTAG_SUPPORTED
static int con_auto_read(void* ud, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    (void)ud;
    uint32_t waited = 0;
    for (;;) {
        uint32_t step = timeout_ms - waited < 10 ? timeout_ms - waited : 10;
        int r;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
        bool usb = usb_serial_jtag_is_connected();
#else
        bool usb = true;
#endif
        /* sleep on the USB port when a host is attached, so its (dropping) buffer is drained
         * the moment data arrives; the UART driver buffers by itself and is polled */
        if (usb) {
            r = usb_serial_jtag_read_bytes(buf, n, ms_ticks(step));
            if (r > 0) return r;
            r = e_uart_read(NULL, 0, buf, n, 0);
        } else {
            r = e_uart_read(NULL, 0, buf, n, step);
            if (r > 0) return r;
            r = usb_serial_jtag_read_bytes(buf, n, 0);
        }
        if (r > 0) return r;
        waited += step;
        if (waited >= timeout_ms) return 0;
    }
}
static void con_auto_write(void* ud, const char* s, size_t n) {
    (void)ud;
    e_uart_write(NULL, 0, (const uint8_t*)s, n);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
    if (!usb_serial_jtag_is_connected()) return;   /* no host on the USB port: don't stall */
#endif
    con_usb_write(NULL, s, n);
}
#endif
mcs_transport_t mcs_esp32_console(void) {
    mcs_transport_t t = mcs_esp32_console_uart(0, 115200);
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    esp_err_t e = con_usb_install();
    if (e == ESP_OK || e == ESP_ERR_INVALID_STATE) {
        t.read = con_auto_read; t.write = con_auto_write; t.ud = NULL;
    }
#endif
    return t;
}

/* ------------------------------------------------------------------ flash filesystem */
#if MCS_ENABLE_FS
#if defined(__has_include)
#if __has_include("esp_littlefs.h")
#include "esp_littlefs.h"
#define MCS_ESP32_LITTLEFS 1
#endif
#endif
static mcs_posixfs_t g_flash_fs;
#if MCS_ESP32_LITTLEFS
static const char* g_flash_label = "storage";
static int esp_lfs_statfs(void* ctx, mcs_vfs_statfs_t* st) {
    size_t total = 0, used = 0;
    (void)ctx;
    if (esp_littlefs_info(g_flash_label, &total, &used) != ESP_OK) return MCS_VFS_EIO;
    st->total = total; st->free = used < total ? total - used : 0; st->format = "littlefs";
    return MCS_VFS_OK;
}
static mcs_vfs_ops_t g_flash_ops;     /* mcs_posixfs_ops + statfs */
#endif
bool mcs_esp32_littlefs(const char* label, const mcs_vfs_ops_t** ops, void** ctx) {
#if MCS_ESP32_LITTLEFS
    esp_vfs_littlefs_conf_t c;
    memset(&c, 0, sizeof c);
    c.base_path = MCS_ESP32_FS_PATH;
    c.partition_label = label ? label : "storage";
    c.format_if_mount_failed = true;          /* first boot: format the empty partition */
    esp_err_t e = esp_vfs_littlefs_register(&c);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;   /* no such partition, ... */
    strcpy(g_flash_fs.root, MCS_ESP32_FS_PATH);
    g_flash_label = c.partition_label;
    g_flash_ops = mcs_posixfs_ops;
    g_flash_ops.statfs = esp_lfs_statfs;
    *ops = &g_flash_ops;
    *ctx = &g_flash_fs;
    return true;
#else
    (void)label; (void)ops; (void)ctx;
    return false;
#endif
}
bool mcs_esp32_littlefs_info(const char* label, size_t* total, size_t* used) {
#if MCS_ESP32_LITTLEFS
    return esp_littlefs_info(label ? label : "storage", total, used) == ESP_OK;
#else
    (void)label; (void)total; (void)used;
    return false;
#endif
}

#if MCS_ENABLE_FLASH
#include "esp_partition.h"
static int ep_read(mcs_flash_t* f, uint32_t addr, void* buf, uint32_t n) {
    const esp_partition_t* p = (const esp_partition_t*)((mcs_esp32_flash_t*)f->ctx)->part;
    return esp_partition_read(p, addr, buf, n) == ESP_OK ? 0 : MCS_FLASH_EIO;
}
static int ep_prog(mcs_flash_t* f, uint32_t addr, const void* buf, uint32_t n) {
    const esp_partition_t* p = (const esp_partition_t*)((mcs_esp32_flash_t*)f->ctx)->part;
    return esp_partition_write(p, addr, buf, n) == ESP_OK ? 0 : MCS_FLASH_EPROG;
}
static int ep_erase(mcs_flash_t* f, uint32_t block) {
    const esp_partition_t* p = (const esp_partition_t*)((mcs_esp32_flash_t*)f->ctx)->part;
    return esp_partition_erase_range(p, block * f->block_size, f->block_size) == ESP_OK ? 0 : MCS_FLASH_EPROG;
}
bool mcs_esp32_partition_flash(mcs_esp32_flash_t* d, const char* label) {
    const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                                        label ? label : "storage");
    if (!p || p->size < 2 * 4096) return false;
    memset(d, 0, sizeof *d);
    d->part = p;
    d->flash.type = MCS_FLASH_NOR;
    d->flash.page_size = 256;
    d->flash.block_size = 4096;
    d->flash.block_count = p->size / 4096;
    d->flash.read = ep_read;
    d->flash.prog = ep_prog;
    d->flash.erase = ep_erase;
    d->flash.ctx = d;
    return true;
}
#endif

bool mcs_esp32_flash_fs(const char* label, const mcs_vfs_ops_t** ops, void** ctx) {
#if MCS_ENABLE_FLASH && (MCS_ENABLE_YAFFS || MCS_ENABLE_TINYFS)
    static mcs_esp32_flash_t fl;
    static mcs_flashfs_t fs;
    if (!mcs_esp32_partition_flash(&fl, label) ||
        mcs_flashfs_mount(&fs, &fl.flash, 0, 0, MCS_ENABLE_YAFFS ? MCS_FLASHFS_YAFFS2 : MCS_FLASHFS_TINYFS,
                          MCS_FLASHFS_FORMAT_IF_NEEDED) != 0)
        return false;
    *ops = fs.ops; *ctx = fs.ctx;
    return true;
#else
    return mcs_esp32_littlefs(label, ops, ctx);
#endif
}
#endif

/* ------------------------------------------------------------------ WS2812 / NeoPixel (RMT)
 * One RMT TX channel + bytes encoder per pin (created on first use), 10 MHz
 * resolution: 0 = 0.4 us high + 0.85 us low, 1 = 0.8 us high + 0.45 us low. */
#ifndef MCS_ESP32_RGB_LED            /* on-board RGB LED for the "NEOPIXEL" pin name */
#  if CONFIG_IDF_TARGET_ESP32S3
#    define MCS_ESP32_RGB_LED 48     /* ESP32-S3-DevKitC-1 v1.0 (v1.1: 38) */
#  elif CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2
#    define MCS_ESP32_RGB_LED 8      /* DevKitM / DevKitC */
#  else
#    define MCS_ESP32_RGB_LED -1
#  endif
#endif
#if SOC_RMT_SUPPORTED && MCS_ENABLE_WS2812   /* "ws2812" driver: RMT backend shared with Arduino-ESP32 */
#define MCS_WS2812_ESP32_RMT 1
#include "mcs_ws2812_backends.h"
static const mcs_ws2812_ops_t e_ws2812_ops = { mcs_ws2812_rmt_write };
static const mcs_driver_t e_ws2812 = MCS_WS2812_DRIVER(&e_ws2812_ops, NULL);
#define E_HAS_WS2812 1
#endif
static int e_pin_lookup(void* ctx, const char* name) {
    (void)ctx;
    if ((!strcmp(name, "LED") || !strcmp(name, "LED_BUILTIN")) && g_cfg.led >= 0) return g_cfg.led;
    if (!strcmp(name, "NEOPIXEL") || !strcmp(name, "RGB_LED") || !strcmp(name, "WS2812")) return MCS_ESP32_RGB_LED;
    return -1;                       /* -> generic parser: "GPIO5", "IO5", "5" */
}
void mcs_esp32_hal_init(mcs_hal_t* hal, const mcs_esp32_cfg_t* cfg) {
    static const mcs_esp32_cfg_t defaults = MCS_ESP32_CFG_DEFAULT;
    g_cfg = cfg ? *cfg : defaults;
    if (!g_events) g_events = xQueueCreate(MCS_ESP32_EVENT_QUEUE, sizeof(mcs_hal_event_t));
    memset(hal, 0, sizeof *hal);
    for (int i = 0; i < MCS_ESP32_I2C_BUSES; i++) { g_i2c_pins[i] = g_cfg.i2c[i]; g_i2c_pu[i] = 1; g_i2c_glitch[i] = 7; }
    for (int i = 0; i < MCS_ESP32_SPI_BUSES; i++) { g_spi_pins[i] = g_cfg.spi[i]; g_qspi_cs[i] = g_cfg.qspi_cs[i]; }
    for (int i = 0; i < MCS_ESP32_PWM_CHANNELS; i++) { g_pwm_timer[i] = -1; g_pwm_pin[i] = -1; }
    hal->board = g_cfg.name ? g_cfg.name : CONFIG_IDF_TARGET;
    hal->ctx = &g_cfg;
    mcs_esp_power_init(hal);
    hal->gpio_mode = e_gpio_mode;
    hal->gpio_write = e_gpio_write;
    hal->gpio_read = e_gpio_read;
    hal->gpio_irq = e_gpio_irq;
    hal->uart_open = e_uart_open;
    hal->uart_config = e_uart_config;
    hal->uart_close = e_uart_close;
    hal->uart_write = e_uart_write;
    hal->uart_read = e_uart_read;
    hal->uart_available = e_uart_available;
    hal->i2c_open = e_i2c_open;
    hal->i2c_write = e_i2c_write;
    hal->i2c_read = e_i2c_read;
    hal->i2c_write_read = e_i2c_write_read;
    hal->i2c_probe = e_i2c_probe;
    hal->spi_open = e_spi_open;
    hal->spi_transfer = e_spi_transfer;
    hal->qspi_open = e_qspi_open;
    hal->qspi_command = e_qspi_command;
    hal->adc_read = e_adc_read;
    hal->adc_read_mv = e_adc_read_mv;
    hal->adc_bits = SOC_ADC_RTC_MAX_BITWIDTH;
    hal->adc_vref_mv = 3100;
#if SOC_DAC_SUPPORTED
    hal->dac_write = e_dac_write;
    hal->dac_bits = 8;
#endif
    hal->pwm_set = e_pwm_set;
    hal->pwm_set16 = e_pwm_set16;
    hal->pwm_stop = e_pwm_stop;
    hal->timer_start = e_timer_start;
    hal->timer_stop = e_timer_stop;
#if SOC_I2S_SUPPORTED
    hal->i2s_open = e_i2s_open;
    hal->i2s_write = e_i2s_write;
    hal->i2s_read = e_i2s_read;
    hal->i2s_close = e_i2s_close;
#endif
#if SOC_TWAI_SUPPORTED
    hal->can_open = esp_can_open;
    hal->can_configure = esp_can_configure;
    hal->can_close = esp_can_close;
    hal->can_send = esp_can_send;
    hal->can_recv = esp_can_recv;
#endif
    hal->wdt_start = e_wdt_start;
    hal->wdt_feed = e_wdt_feed;
    hal->rtc_get = e_rtc_get;
    hal->rtc_set = e_rtc_set;
    hal->micros = e_micros;
    hal->delay_us = e_delay_us;
    hal->reset = e_reset;
    hal->unique_id = e_unique_id;
#ifdef E_HAS_WS2812
    mcs_driver_register_default(&e_ws2812);       /* keeps a "ws2812" you registered first */
#endif
    hal->pin_lookup = e_pin_lookup;
    hal->cpu_hz = (uint32_t)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000u;
    hal->poll_event = e_poll_event;
    /* v3: full configuration + DMA */
    hal->gpio_configure = e_gpio_configure;
    hal->uart_configure = e_uart_configure;
    hal->i2c_configure = e_i2c_configure;
    hal->spi_configure = e_spi_configure;
    hal->qspi_configure = e_qspi_configure;
    hal->pwm_configure = e_pwm_configure;
    hal->adc_configure = e_adc_configure;
    hal->options = e_options;
    hal->dma_alloc = e_dma_alloc;
    hal->dma_free = e_dma_free;
    hal->dma_align = 4;
#if defined(SOC_GDMA_PAIRS_PER_GROUP_MAX)
    hal->dma_channels = SOC_GDMA_PAIRS_PER_GROUP_MAX;
#else
    hal->dma_channels = 2;            /* ESP32 / S2: SPI DMA channels */
#endif
#if MCS_ESP_MEMCPY
    hal->dma_copy = e_dma_copy;
#endif
    hal->spi_transfer_async = e_spi_transfer_async;
    hal->qspi_command_async = e_qspi_command_async;
#if MCS_ESP32_WIFI
    mcs_net_set_poster(post_task);
    mcs_net_set(mcs_esp32_net());           /* C# Wifi, EspNow, Ntp, Mdns, Ping, Http (TLS) */
#endif
#if MCS_ESP32_BLE
    mcs_ble_set_poster(post_task);
    mcs_ble_set(mcs_esp32_ble());           /* C# Ble, BleService, BleConnection ... (NimBLE) */
#endif
}
