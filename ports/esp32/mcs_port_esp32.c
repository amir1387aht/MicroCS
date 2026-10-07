/*
 * MicroCS port for ESP-IDF 5.x (ESP32 / S2 / S3 / C2 / C3 / C5 / C6 / H2 / P4).
 * See mcs_port_esp32.h. Uses only public ESP-IDF driver APIs.
 */
#include "mcs_port_esp32.h"
#include <string.h>
#include <sys/time.h>
#include "sdkconfig.h"
#include "esp_idf_version.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_system.h"
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
#if SOC_TWAI_SUPPORTED
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
static int e_poll_event(void* ctx, mcs_hal_event_t* ev) {
    (void)ctx;
    return g_events && xQueueReceive(g_events, ev, 0) == pdTRUE;
}

/* ------------------------------------------------------------------ GPIO */
static int e_gpio_mode(void* ctx, int pin, int mode) {
    (void)ctx;
    if (!GPIO_IS_VALID_GPIO(pin)) return MCS_HAL_EINVAL;
    gpio_reset_pin((gpio_num_t)pin);
    gpio_num_t p = (gpio_num_t)pin;
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
static int e_gpio_write(void* ctx, int pin, int v) {
    (void)ctx;
    return esp_rc(gpio_set_level((gpio_num_t)pin, v ? 1 : 0));
}
static int e_gpio_read(void* ctx, int pin) {
    (void)ctx;
    if (!GPIO_IS_VALID_GPIO(pin)) return MCS_HAL_EINVAL;
    return gpio_get_level((gpio_num_t)pin);
}
static void IRAM_ATTR gpio_isr(void* arg) {
    int pin = (int)(intptr_t)arg;
    post_isr(MCS_HAL_EV_GPIO, (uint16_t)pin, gpio_get_level((gpio_num_t)pin));
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
    gpio_set_intr_type(p, edge == MCS_GPIO_EDGE_RISING ? GPIO_INTR_POSEDGE
                        : edge == MCS_GPIO_EDGE_FALLING ? GPIO_INTR_NEGEDGE : GPIO_INTR_ANYEDGE);
    gpio_isr_handler_remove(p);
    esp_err_t e = gpio_isr_handler_add(p, gpio_isr, (void*)(intptr_t)pin);
    if (e == ESP_OK) e = gpio_intr_enable(p);
    return esp_rc(e);
}

/* ------------------------------------------------------------------ UART */
static int e_uart_config(void* ctx, int port, const mcs_uart_cfg_t* c) {
    (void)ctx;
    if (port < 0 || port >= (int)SOC_UART_NUM || port >= MCS_ESP32_UARTS) return MCS_HAL_ENOTSUP;
    uart_config_t u;
    memset(&u, 0, sizeof u);
    u.baud_rate = (int)c->baud;
    u.data_bits = c->data_bits == 5 ? UART_DATA_5_BITS : c->data_bits == 6 ? UART_DATA_6_BITS
                : c->data_bits == 7 ? UART_DATA_7_BITS : UART_DATA_8_BITS;
    if (c->data_bits < 5 || c->data_bits > 8) return MCS_HAL_ENOTSUP;
    u.parity = c->parity == MCS_UART_PARITY_ODD ? UART_PARITY_ODD : c->parity == MCS_UART_PARITY_EVEN ? UART_PARITY_EVEN : UART_PARITY_DISABLE;
    u.stop_bits = c->stop_bits == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
    u.flow_ctrl = c->flow_control ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE;
    u.rx_flow_ctrl_thresh = 100;
    u.source_clk = UART_SCLK_DEFAULT;
    if (!uart_is_driver_installed((uart_port_t)port)) {
        esp_err_t e = uart_driver_install((uart_port_t)port, MCS_ESP32_UART_RXBUF, 0, 0, NULL, 0);
        if (e != ESP_OK) return esp_rc(e);
    }
    esp_err_t e = uart_param_config((uart_port_t)port, &u);
    const mcs_esp32_uart_pins_t* pp = &g_cfg.uart[port];
    if (e == ESP_OK && (pp->tx >= 0 || pp->rx >= 0 || pp->rts >= 0 || pp->cts >= 0))
        e = uart_set_pin((uart_port_t)port, pp->tx, pp->rx, pp->rts, pp->cts);
    return esp_rc(e);
}
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
#if MCS_ESP_NEW_I2C
typedef struct { uint16_t addr; uint32_t hz; i2c_master_dev_handle_t dev; } i2c_dev_t;
static i2c_master_bus_handle_t g_i2c[MCS_ESP32_I2C_BUSES];
static uint32_t g_i2c_hz[MCS_ESP32_I2C_BUSES];
static i2c_dev_t g_i2c_dev[MCS_ESP32_I2C_BUSES][MCS_ESP32_I2C_DEVICES];
static uint8_t g_i2c_next[MCS_ESP32_I2C_BUSES];
static int e_i2c_open(void* ctx, int bus, uint32_t freq) {
    (void)ctx;
    if (bus < 0 || bus >= MCS_ESP32_I2C_BUSES || bus >= (int)SOC_I2C_NUM || g_cfg.i2c[bus].sda < 0) return MCS_HAL_ENOTSUP;
    g_i2c_hz[bus] = freq ? freq : 100000;
    if (g_i2c[bus]) return 0;
    i2c_master_bus_config_t c;
    memset(&c, 0, sizeof c);
    c.i2c_port = bus;
    c.sda_io_num = (gpio_num_t)g_cfg.i2c[bus].sda;
    c.scl_io_num = (gpio_num_t)g_cfg.i2c[bus].scl;
    c.clk_source = I2C_CLK_SRC_DEFAULT;
    c.glitch_ignore_cnt = 7;
    c.flags.enable_internal_pullup = true;
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
    return h ? i2c_rc(i2c_master_transmit(h, d, n, MCS_ESP32_TIMEOUT_MS)) : err;
}
static int e_i2c_read(void* ctx, int bus, int addr, uint8_t* buf, size_t n) {
    (void)ctx; int err; i2c_master_dev_handle_t h = i2c_dev(bus, addr, &err);
    return h ? i2c_rc(i2c_master_receive(h, buf, n, MCS_ESP32_TIMEOUT_MS)) : err;
}
static int e_i2c_write_read(void* ctx, int bus, int addr, const uint8_t* tx, size_t tn, uint8_t* rx, size_t rn) {
    (void)ctx; int err; i2c_master_dev_handle_t h = i2c_dev(bus, addr, &err);
    return h ? i2c_rc(i2c_master_transmit_receive(h, tx, tn, rx, rn, MCS_ESP32_TIMEOUT_MS)) : err;
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
static int e_i2c_open(void* ctx, int bus, uint32_t freq) {
    (void)ctx;
    if (bus < 0 || bus >= MCS_ESP32_I2C_BUSES || bus >= (int)SOC_I2C_NUM || g_cfg.i2c[bus].sda < 0) return MCS_HAL_ENOTSUP;
    if (g_i2c_on[bus]) { i2c_driver_delete((i2c_port_t)bus); g_i2c_on[bus] = false; }
    i2c_config_t c;
    memset(&c, 0, sizeof c);
    c.mode = I2C_MODE_MASTER;
    c.sda_io_num = g_cfg.i2c[bus].sda;
    c.scl_io_num = g_cfg.i2c[bus].scl;
    c.sda_pullup_en = GPIO_PULLUP_ENABLE;
    c.scl_pullup_en = GPIO_PULLUP_ENABLE;
    c.master.clk_speed = freq ? freq : 100000;
    esp_err_t e = i2c_param_config((i2c_port_t)bus, &c);
    if (e == ESP_OK) e = i2c_driver_install((i2c_port_t)bus, I2C_MODE_MASTER, 0, 0, 0);
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
    return r ? r : i2c_rc(i2c_master_write_to_device((i2c_port_t)bus, (uint8_t)addr, d, n, ms_ticks(MCS_ESP32_TIMEOUT_MS)));
}
static int e_i2c_read(void* ctx, int bus, int addr, uint8_t* buf, size_t n) {
    (void)ctx; int r = i2c_ready(bus);
    return r ? r : i2c_rc(i2c_master_read_from_device((i2c_port_t)bus, (uint8_t)addr, buf, n, ms_ticks(MCS_ESP32_TIMEOUT_MS)));
}
static int e_i2c_write_read(void* ctx, int bus, int addr, const uint8_t* tx, size_t tn, uint8_t* rx, size_t rn) {
    (void)ctx; int r = i2c_ready(bus);
    return r ? r : i2c_rc(i2c_master_write_read_device((i2c_port_t)bus, (uint8_t)addr, tx, tn, rx, rn, ms_ticks(MCS_ESP32_TIMEOUT_MS)));
}
static int e_i2c_probe(void* ctx, int bus, int addr) {
    (void)ctx; int r = i2c_ready(bus);
    return r ? r : i2c_rc(i2c_master_write_to_device((i2c_port_t)bus, (uint8_t)addr, NULL, 0, ms_ticks(20)));
}
#endif

/* ------------------------------------------------------------------ SPI + QSPI */
static spi_host_device_t spi_host(int bus) {
#if SOC_SPI_PERIPH_NUM > 2
    return bus == 0 ? SPI2_HOST : SPI3_HOST;
#else
    (void)bus;
    return SPI2_HOST;
#endif
}
static bool g_spi_bus[MCS_ESP32_SPI_BUSES];
static spi_device_handle_t g_spi_dev[MCS_ESP32_SPI_BUSES], g_qspi_dev[MCS_ESP32_SPI_BUSES];
static int spi_bus_up(int bus) {
    if (bus < 0 || bus >= MCS_ESP32_SPI_BUSES || bus >= (int)SOC_SPI_PERIPH_NUM - 1 || g_cfg.spi[bus].sclk < 0) return MCS_HAL_ENOTSUP;
    if (g_spi_bus[bus]) return 0;
    spi_bus_config_t c;
    memset(&c, 0, sizeof c);
    c.sclk_io_num = g_cfg.spi[bus].sclk;
    c.mosi_io_num = g_cfg.spi[bus].mosi;
    c.miso_io_num = g_cfg.spi[bus].miso;
    c.quadwp_io_num = g_cfg.spi[bus].quadwp;
    c.quadhd_io_num = g_cfg.spi[bus].quadhd;
    c.max_transfer_sz = MCS_HAL_MAX_XFER > 64 ? MCS_HAL_MAX_XFER : 64;
    esp_err_t e = spi_bus_initialize(spi_host(bus), &c, SPI_DMA_CH_AUTO);
    g_spi_bus[bus] = e == ESP_OK;
    return esp_rc(e);
}
static int e_spi_open(void* ctx, int bus, const mcs_spi_cfg_t* cfg) {
    (void)ctx;
    int r = spi_bus_up(bus);
    if (r) return r;
    if (cfg->bits != 8) return MCS_HAL_ENOTSUP;
    if (g_spi_dev[bus]) { spi_bus_remove_device(g_spi_dev[bus]); g_spi_dev[bus] = NULL; }
    spi_device_interface_config_t d;
    memset(&d, 0, sizeof d);
    d.clock_speed_hz = (int)(cfg->freq_hz ? cfg->freq_hz : 1000000);
    d.mode = cfg->mode;
    d.spics_io_num = -1;               /* chip select is a GPIO driven by SpiDevice */
    d.queue_size = 1;
    d.flags = cfg->lsb_first ? SPI_DEVICE_BIT_LSBFIRST : 0;
    return esp_rc(spi_bus_add_device(spi_host(bus), &d, &g_spi_dev[bus]));
}
static int e_spi_transfer(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n) {
    if (bus < 0 || bus >= MCS_ESP32_SPI_BUSES) return MCS_HAL_ENOTSUP;
    if (!g_spi_dev[bus]) {
        mcs_spi_cfg_t c = { 1000000, 0, 8, 0, 0 };
        int r = e_spi_open(ctx, bus, &c);
        if (r) return r;
    }
    spi_transaction_t t;
    memset(&t, 0, sizeof t);
    t.length = n * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return esp_rc(spi_device_polling_transmit(g_spi_dev[bus], &t));
}
static int e_qspi_open(void* ctx, int bus, uint32_t freq) {
    (void)ctx;
    int r = spi_bus_up(bus);
    if (r) return r;
    if (g_qspi_dev[bus]) { spi_bus_remove_device(g_qspi_dev[bus]); g_qspi_dev[bus] = NULL; }
    spi_device_interface_config_t d;
    memset(&d, 0, sizeof d);
    d.clock_speed_hz = (int)(freq ? freq : 10000000);
    d.mode = 0;
    d.spics_io_num = g_cfg.qspi_cs[bus];
    d.queue_size = 1;
    d.flags = SPI_DEVICE_HALFDUPLEX;
    return esp_rc(spi_bus_add_device(spi_host(bus), &d, &g_qspi_dev[bus]));
}
static int e_qspi_command(void* ctx, int bus, const mcs_qspi_cmd_t* q, const uint8_t* tx, uint8_t* rx, size_t n) {
    (void)ctx;
    if (bus < 0 || bus >= MCS_ESP32_SPI_BUSES || !g_qspi_dev[bus]) return MCS_HAL_ENOTSUP;
    if (q->data_lines > 4 || q->addr_lines > 4 || q->instr_lines > 4) return MCS_HAL_ENOTSUP;
    spi_transaction_ext_t t;
    memset(&t, 0, sizeof t);
    t.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
    uint8_t lines = q->data_lines ? q->data_lines : 1;
    if (lines == 4) t.base.flags |= SPI_TRANS_MODE_QIO;
    else if (lines == 2) t.base.flags |= SPI_TRANS_MODE_DIO;
    if (lines > 1 && q->addr_lines > 1) t.base.flags |= SPI_TRANS_MULTILINE_ADDR;
    if (lines > 1 && q->instr_lines > 1) t.base.flags |= SPI_TRANS_MULTILINE_CMD;
    t.command_bits = q->instr_lines ? 8 : 0;
    t.address_bits = (uint8_t)(q->addr_bytes * 8);
    t.dummy_bits = q->dummy_cycles;
    t.base.cmd = q->instruction;
    t.base.addr = q->address;
    if (tx) { t.base.length = n * 8; t.base.tx_buffer = tx; }
    else if (rx) { t.base.rxlength = n * 8; t.base.rx_buffer = rx; }
    return esp_rc(spi_device_polling_transmit(g_qspi_dev[bus], &t.base));
}

/* ------------------------------------------------------------------ ADC / DAC */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#define MCS_ESP_ATTEN ADC_ATTEN_DB_12
#else
#define MCS_ESP_ATTEN ADC_ATTEN_DB_11
#endif
static adc_oneshot_unit_handle_t g_adc[2];
static adc_cali_handle_t g_cali[2][10];
static uint16_t g_adc_conf[2];        /* configured channels */
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
        adc_oneshot_chan_cfg_t cc;
        memset(&cc, 0, sizeof cc);
        cc.atten = MCS_ESP_ATTEN;
        cc.bitwidth = ADC_BITWIDTH_DEFAULT;
        esp_err_t e = adc_oneshot_config_channel(g_adc[u], *ch, &cc);
        if (e != ESP_OK) return esp_rc(e);
        g_adc_conf[u] |= (uint16_t)(1u << *ch);
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t k;
        memset(&k, 0, sizeof k);
        k.unit_id = *unit;
        k.chan = *ch;
        k.atten = MCS_ESP_ATTEN;
        k.bitwidth = ADC_BITWIDTH_DEFAULT;
        if (adc_cali_create_scheme_curve_fitting(&k, &g_cali[u][*ch]) != ESP_OK) g_cali[u][*ch] = NULL;
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_line_fitting_config_t k;
        memset(&k, 0, sizeof k);
        k.unit_id = *unit;
        k.atten = MCS_ESP_ATTEN;
        k.bitwidth = ADC_BITWIDTH_DEFAULT;
        if (adc_cali_create_scheme_line_fitting(&k, &g_cali[u][*ch]) != ESP_OK) g_cali[u][*ch] = NULL;
#endif
    }
    return 0;
}
static int e_adc_read(void* ctx, int gpio) {
    (void)ctx;
    adc_unit_t unit; adc_channel_t ch;
    int r = adc_prepare(gpio, &unit, &ch);
    if (r) return r;
    int raw = 0;
    r = esp_rc(adc_oneshot_read(g_adc[unit], ch, &raw));
    return r ? r : raw;
}
static int e_adc_read_mv(void* ctx, int gpio) {
    (void)ctx;
    adc_unit_t unit; adc_channel_t ch;
    int r = adc_prepare(gpio, &unit, &ch);
    if (r) return r;
    int raw = 0, mv = 0;
    if ((r = esp_rc(adc_oneshot_read(g_adc[unit], ch, &raw)))) return r;
    if (g_cali[unit][ch] && adc_cali_raw_to_voltage(g_cali[unit][ch], raw, &mv) == ESP_OK) return mv;
    return (int)((int64_t)raw * 3100 / ((1 << SOC_ADC_RTC_MAX_BITWIDTH) - 1));   /* uncalibrated: ~0..3.1 V at 12 dB */
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
static bool g_pwm_on[MCS_ESP32_PWM_CHANNELS];
static int e_pwm_set16(void* ctx, int ch, uint32_t freq, uint16_t duty) {
    (void)ctx;
    if (ch < 0 || ch >= MCS_ESP32_PWM_CHANNELS || ch >= (int)SOC_LEDC_CHANNEL_NUM || g_cfg.pwm[ch] < 0) return MCS_HAL_ENOTSUP;
    if (!freq) return MCS_HAL_EINVAL;
    int timer = ch & 3;
    if (freq != g_pwm_freq[timer] || !g_pwm_on[ch]) {
        /* widest resolution that still divides the LEDC clock */
        int bits = 1;
        while (bits < SOC_LEDC_TIMER_BIT_WIDTH && (MCS_ESP32_LEDC_CLK_HZ >> (bits + 1)) >= freq) bits++;
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
        } while (e != ESP_OK && --bits > 0);
        if (e != ESP_OK) return esp_rc(e);
        g_pwm_freq[timer] = freq;
        for (int i = timer; i < MCS_ESP32_PWM_CHANNELS; i += 4) g_pwm_bits[i] = (uint8_t)bits;
        if (!g_pwm_on[ch]) {
            ledc_channel_config_t c;
            memset(&c, 0, sizeof c);
            c.gpio_num = g_cfg.pwm[ch];
            c.speed_mode = LEDC_LOW_SPEED_MODE;
            c.channel = (ledc_channel_t)ch;
            c.timer_sel = (ledc_timer_t)timer;
            c.duty = 0;
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

/* ------------------------------------------------------------------ CAN (TWAI) */
#if SOC_TWAI_SUPPORTED
static bool g_twai;
static int e_can_open(void* ctx, int bus, uint32_t bitrate) {
    (void)ctx;
    if (bus != 0 || g_cfg.can.tx < 0) return MCS_HAL_ENOTSUP;
    if (g_twai) { twai_stop(); twai_driver_uninstall(); g_twai = false; }
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)g_cfg.can.tx, (gpio_num_t)g_cfg.can.rx, TWAI_MODE_NORMAL);
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    twai_timing_config_t t;
    switch (bitrate) {
    case 25000:   { twai_timing_config_t x = TWAI_TIMING_CONFIG_25KBITS(); t = x; break; }
    case 50000:   { twai_timing_config_t x = TWAI_TIMING_CONFIG_50KBITS(); t = x; break; }
    case 100000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_100KBITS(); t = x; break; }
    case 125000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_125KBITS(); t = x; break; }
    case 250000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_250KBITS(); t = x; break; }
    case 500000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_500KBITS(); t = x; break; }
    case 800000:  { twai_timing_config_t x = TWAI_TIMING_CONFIG_800KBITS(); t = x; break; }
    case 1000000: { twai_timing_config_t x = TWAI_TIMING_CONFIG_1MBITS(); t = x; break; }
    default: return MCS_HAL_ENOTSUP;
    }
    esp_err_t e = twai_driver_install(&g, &t, &f);
    if (e == ESP_OK) e = twai_start();
    g_twai = e == ESP_OK;
    return esp_rc(e);
}
static int e_can_send(void* ctx, int bus, const mcs_can_frame_t* fr, uint32_t timeout_ms) {
    (void)ctx;
    if (bus != 0 || !g_twai) return MCS_HAL_ENOTSUP;
    twai_message_t m;
    memset(&m, 0, sizeof m);
    m.identifier = fr->id;
    m.extd = fr->extended ? 1 : 0;
    m.rtr = fr->rtr ? 1 : 0;
    m.data_length_code = fr->len > 8 ? 8 : fr->len;
    memcpy(m.data, fr->data, m.data_length_code);
    return esp_rc(twai_transmit(&m, ms_ticks(timeout_ms)));
}
static int e_can_recv(void* ctx, int bus, mcs_can_frame_t* fr, uint32_t timeout_ms) {
    (void)ctx;
    if (bus != 0 || !g_twai) return MCS_HAL_ENOTSUP;
    twai_message_t m;
    esp_err_t e = twai_receive(&m, ms_ticks(timeout_ms));
    if (e != ESP_OK) return esp_rc(e);
    fr->id = m.identifier;
    fr->extended = m.extd;
    fr->rtr = m.rtr;
    fr->len = m.data_length_code > 8 ? 8 : m.data_length_code;
    memcpy(fr->data, m.data, fr->len);
    return 0;
}
#endif

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
    usb_serial_jtag_driver_config_t c = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t e = usb_serial_jtag_driver_install(&c);
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
        int r = usb_serial_jtag_read_bytes(buf, n, 0);
        if (r > 0) return r;
        uint32_t step = timeout_ms - waited < 10 ? timeout_ms - waited : 10;
        r = e_uart_read(NULL, 0, buf, n, step);    /* waits up to `step` ms */
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
    usb_serial_jtag_driver_config_t c = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t e = usb_serial_jtag_driver_install(&c);
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
#endif

static int e_pin_lookup(void* ctx, const char* name) {
    (void)ctx;
    if ((!strcmp(name, "LED") || !strcmp(name, "LED_BUILTIN")) && g_cfg.led >= 0) return g_cfg.led;
    return -1;                       /* -> generic parser: "GPIO5", "IO5", "5" */
}
void mcs_esp32_hal_init(mcs_hal_t* hal, const mcs_esp32_cfg_t* cfg) {
    static const mcs_esp32_cfg_t defaults = MCS_ESP32_CFG_DEFAULT;
    g_cfg = cfg ? *cfg : defaults;
    if (!g_events) g_events = xQueueCreate(MCS_ESP32_EVENT_QUEUE, sizeof(mcs_hal_event_t));
    memset(hal, 0, sizeof *hal);
    hal->board = g_cfg.name ? g_cfg.name : CONFIG_IDF_TARGET;
    hal->ctx = &g_cfg;
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
    hal->can_open = e_can_open;
    hal->can_send = e_can_send;
    hal->can_recv = e_can_recv;
#endif
    hal->wdt_start = e_wdt_start;
    hal->wdt_feed = e_wdt_feed;
    hal->rtc_get = e_rtc_get;
    hal->rtc_set = e_rtc_set;
    hal->micros = e_micros;
    hal->delay_us = e_delay_us;
    hal->reset = e_reset;
    hal->unique_id = e_unique_id;
    hal->pin_lookup = e_pin_lookup;
    hal->cpu_hz = (uint32_t)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000u;
    hal->poll_event = e_poll_event;
}
