/* MicroCS port for RP2040 / RP2350 - pico-sdk. See mcs_port_rp2.h. */
#include "mcs_port_rp2.h"
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"

#ifndef ADC_BASE_PIN
#define ADC_BASE_PIN 26
#endif
#ifndef NUM_ADC_CHANNELS
#define NUM_ADC_CHANNELS 5
#endif
#ifndef MCS_RP2_TIMEOUT_US
#define MCS_RP2_TIMEOUT_US 50000
#endif

static mcs_rp2_cfg_t g_cfg;
static uint64_t g_od_mask;           /* open-drain pins (emulated with direction) */

/* ------------------------------------------------------------------ GPIO */
static int r_gpio_mode(void* ctx, int pin, int mode) {
    (void)ctx;
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_EINVAL;
    uint p = (uint)pin;
    gpio_init(p);
    g_od_mask &= ~(1ull << pin);
    switch (mode) {
    case MCS_GPIO_INPUT: gpio_set_dir(p, GPIO_IN); gpio_disable_pulls(p); break;
    case MCS_GPIO_INPUT_PULLUP: gpio_set_dir(p, GPIO_IN); gpio_pull_up(p); break;
    case MCS_GPIO_INPUT_PULLDOWN: gpio_set_dir(p, GPIO_IN); gpio_pull_down(p); break;
    case MCS_GPIO_OUTPUT: gpio_set_dir(p, GPIO_OUT); break;
    case MCS_GPIO_OPEN_DRAIN:    /* released = input with pull-up, low = output 0 */
        gpio_put(p, 0); gpio_set_dir(p, GPIO_IN); gpio_pull_up(p); g_od_mask |= 1ull << pin; break;
    case MCS_GPIO_ANALOG:
        gpio_disable_pulls(p); gpio_set_input_enabled(p, false); break;
    default: return MCS_HAL_EINVAL;
    }
    return 0;
}
static int r_gpio_write(void* ctx, int pin, int v) {
    (void)ctx;
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_EINVAL;
    if (g_od_mask & (1ull << pin)) gpio_set_dir((uint)pin, v ? GPIO_IN : GPIO_OUT);
    else gpio_put((uint)pin, v != 0);
    return 0;
}
static int r_gpio_read(void* ctx, int pin) {
    (void)ctx;
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_EINVAL;
    return gpio_get((uint)pin) ? 1 : 0;
}
static void gpio_cb(uint gpio, uint32_t events) {
    int level = (events & GPIO_IRQ_EDGE_RISE) && !(events & GPIO_IRQ_EDGE_FALL) ? 1
              : (events & GPIO_IRQ_EDGE_FALL) && !(events & GPIO_IRQ_EDGE_RISE) ? 0 : (int)gpio_get(gpio);
    mcs_hal_post(MCS_HAL_EV_GPIO, (int)gpio, level);
}
static int r_gpio_irq(void* ctx, int pin, int edge) {
    (void)ctx;
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_EINVAL;
    uint32_t ev = (edge & MCS_GPIO_EDGE_RISING ? GPIO_IRQ_EDGE_RISE : 0) | (edge & MCS_GPIO_EDGE_FALLING ? GPIO_IRQ_EDGE_FALL : 0);
    if (!ev) { gpio_set_irq_enabled((uint)pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false); return 0; }
    gpio_set_irq_enabled((uint)pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
    gpio_set_irq_enabled_with_callback((uint)pin, ev, true, gpio_cb);
    return 0;
}

/* ------------------------------------------------------------------ UART */
static struct { uint8_t buf[MCS_RP2_UART_RXBUF]; volatile uint16_t head, tail; bool on; } g_rx[2];
static uart_inst_t* uart_of(int port) { return port == 0 ? uart0 : port == 1 ? uart1 : NULL; }
static void uart_irq_common(int port) {
    uart_inst_t* u = uart_of(port);
    while (uart_is_readable(u)) {
        uint8_t c = (uint8_t)uart_getc(u);
        uint16_t next = (uint16_t)((g_rx[port].head + 1) & (MCS_RP2_UART_RXBUF - 1));
        if (next != g_rx[port].tail) { g_rx[port].buf[g_rx[port].head] = c; g_rx[port].head = next; }
    }
}
static void uart0_irq(void) { uart_irq_common(0); }
static void uart1_irq(void) { uart_irq_common(1); }
static int r_uart_config(void* ctx, int port, const mcs_uart_cfg_t* c) {
    (void)ctx;
    uart_inst_t* u = uart_of(port);
    if (!u || g_cfg.uart[port].tx < 0) return MCS_HAL_ENOTSUP;
    if (c->data_bits < 5 || c->data_bits > 8) return MCS_HAL_ENOTSUP;
    uart_init(u, c->baud);
    gpio_set_function((uint)g_cfg.uart[port].tx, GPIO_FUNC_UART);
    if (g_cfg.uart[port].rx >= 0) gpio_set_function((uint)g_cfg.uart[port].rx, GPIO_FUNC_UART);
    uart_set_format(u, c->data_bits, c->stop_bits == 2 ? 2 : 1,
                    c->parity == MCS_UART_PARITY_ODD ? UART_PARITY_ODD : c->parity == MCS_UART_PARITY_EVEN ? UART_PARITY_EVEN : UART_PARITY_NONE);
    uart_set_hw_flow(u, c->flow_control != 0, c->flow_control != 0);
    uart_set_fifo_enabled(u, true);
    g_rx[port].head = g_rx[port].tail = 0;
    int irq = port == 0 ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler((uint)irq, port == 0 ? uart0_irq : uart1_irq);
    irq_set_enabled((uint)irq, true);
    uart_set_irq_enables(u, true, false);
    g_rx[port].on = true;
    return 0;
}
static int r_uart_open(void* ctx, int port, uint32_t baud) {
    mcs_uart_cfg_t c = { baud, 8, MCS_UART_PARITY_NONE, 1, 0 };
    return r_uart_config(ctx, port, &c);
}
static int r_uart_close(void* ctx, int port) {
    (void)ctx;
    uart_inst_t* u = uart_of(port);
    if (!u) return MCS_HAL_ENOTSUP;
    uart_set_irq_enables(u, false, false);
    irq_set_enabled((uint)(port == 0 ? UART0_IRQ : UART1_IRQ), false);
    irq_remove_handler((uint)(port == 0 ? UART0_IRQ : UART1_IRQ), port == 0 ? uart0_irq : uart1_irq);
    uart_deinit(u);
    g_rx[port].on = false;
    return 0;
}
static int r_uart_write(void* ctx, int port, const uint8_t* d, size_t n) {
    (void)ctx;
    uart_inst_t* u = uart_of(port);
    if (!u || !g_rx[port].on) return MCS_HAL_ENOTSUP;
    uart_write_blocking(u, d, n);
    return (int)n;
}
static int r_uart_read(void* ctx, int port, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    (void)ctx;
    if (!uart_of(port) || !g_rx[port].on) return MCS_HAL_ENOTSUP;
    absolute_time_t until = make_timeout_time_ms(timeout_ms);
    size_t k = 0;
    while (k < n) {
        if (g_rx[port].tail != g_rx[port].head) {
            buf[k++] = g_rx[port].buf[g_rx[port].tail];
            g_rx[port].tail = (uint16_t)((g_rx[port].tail + 1) & (MCS_RP2_UART_RXBUF - 1));
            continue;
        }
        if (k || time_reached(until)) break;
        tight_loop_contents();
    }
    return (int)k;
}
static int r_uart_available(void* ctx, int port) {
    (void)ctx;
    if (!uart_of(port) || !g_rx[port].on) return MCS_HAL_ENOTSUP;
    return (int)((g_rx[port].head - g_rx[port].tail) & (MCS_RP2_UART_RXBUF - 1));
}

/* ------------------------------------------------------------------ I2C */
static bool g_i2c_on[2];
static i2c_inst_t* i2c_of(int bus) { return bus == 0 ? i2c0 : bus == 1 ? i2c1 : NULL; }
static int r_i2c_open(void* ctx, int bus, uint32_t freq) {
    (void)ctx;
    i2c_inst_t* i = i2c_of(bus);
    if (!i || g_cfg.i2c[bus].sda < 0) return MCS_HAL_ENOTSUP;
    i2c_init(i, freq ? freq : 100000);
    gpio_set_function((uint)g_cfg.i2c[bus].sda, GPIO_FUNC_I2C);
    gpio_set_function((uint)g_cfg.i2c[bus].scl, GPIO_FUNC_I2C);
    gpio_pull_up((uint)g_cfg.i2c[bus].sda);
    gpio_pull_up((uint)g_cfg.i2c[bus].scl);
    g_i2c_on[bus] = true;
    return 0;
}
static i2c_inst_t* i2c_ready(int bus, int* err) {
    i2c_inst_t* i = i2c_of(bus);
    *err = 0;
    if (!i) { *err = MCS_HAL_ENOTSUP; return NULL; }
    if (!g_i2c_on[bus] && (*err = r_i2c_open(NULL, bus, 0))) return NULL;
    return i;
}
static int i2c_rc(int r, size_t n) {
    if (r == (int)n) return 0;
    return r == PICO_ERROR_TIMEOUT ? MCS_HAL_ETIMEOUT : MCS_HAL_ENODEV;
}
static int r_i2c_write(void* ctx, int bus, int addr, const uint8_t* d, size_t n) {
    (void)ctx; int err; i2c_inst_t* i = i2c_ready(bus, &err);
    if (!i) return err;
    return i2c_rc(i2c_write_timeout_us(i, (uint8_t)addr, d, n, false, MCS_RP2_TIMEOUT_US), n);
}
static int r_i2c_read(void* ctx, int bus, int addr, uint8_t* buf, size_t n) {
    (void)ctx; int err; i2c_inst_t* i = i2c_ready(bus, &err);
    if (!i) return err;
    return i2c_rc(i2c_read_timeout_us(i, (uint8_t)addr, buf, n, false, MCS_RP2_TIMEOUT_US), n);
}
static int r_i2c_write_read(void* ctx, int bus, int addr, const uint8_t* tx, size_t tn, uint8_t* rx, size_t rn) {
    (void)ctx; int err; i2c_inst_t* i = i2c_ready(bus, &err);
    if (!i) return err;
    int r = i2c_rc(i2c_write_timeout_us(i, (uint8_t)addr, tx, tn, true, MCS_RP2_TIMEOUT_US), tn);   /* no stop: repeated start */
    return r ? r : i2c_rc(i2c_read_timeout_us(i, (uint8_t)addr, rx, rn, false, MCS_RP2_TIMEOUT_US), rn);
}
static int r_i2c_probe(void* ctx, int bus, int addr) {
    uint8_t b;
    if ((addr & 0x78) == 0 || (addr & 0x78) == 0x78) return MCS_HAL_ENODEV;   /* reserved addresses */
    return r_i2c_read(ctx, bus, addr, &b, 1);
}

/* ------------------------------------------------------------------ SPI */
static bool g_spi_on[2];
static spi_inst_t* spi_of(int bus) { return bus == 0 ? spi0 : bus == 1 ? spi1 : NULL; }
static int r_spi_open(void* ctx, int bus, const mcs_spi_cfg_t* c) {
    (void)ctx;
    spi_inst_t* s = spi_of(bus);
    if (!s || g_cfg.spi[bus].sck < 0) return MCS_HAL_ENOTSUP;
    if (c->lsb_first || c->bits < 4 || c->bits > 16 || c->bits != 8) return MCS_HAL_ENOTSUP;
    spi_init(s, c->freq_hz ? c->freq_hz : 1000000);
    spi_set_format(s, 8, (c->mode & 2) ? SPI_CPOL_1 : SPI_CPOL_0, (c->mode & 1) ? SPI_CPHA_1 : SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function((uint)g_cfg.spi[bus].sck, GPIO_FUNC_SPI);
    if (g_cfg.spi[bus].mosi >= 0) gpio_set_function((uint)g_cfg.spi[bus].mosi, GPIO_FUNC_SPI);
    if (g_cfg.spi[bus].miso >= 0) gpio_set_function((uint)g_cfg.spi[bus].miso, GPIO_FUNC_SPI);
    g_spi_on[bus] = true;
    return 0;
}
static int r_spi_transfer(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n) {
    spi_inst_t* s = spi_of(bus);
    if (!s) return MCS_HAL_ENOTSUP;
    if (!g_spi_on[bus]) {
        mcs_spi_cfg_t c = { 1000000, 0, 8, 0, 0 };
        int r = r_spi_open(ctx, bus, &c);
        if (r) return r;
    }
    if (tx && rx) spi_write_read_blocking(s, tx, rx, n);
    else if (tx) spi_write_blocking(s, tx, n);
    else spi_read_blocking(s, 0xFF, rx, n);
    return 0;
}

/* ------------------------------------------------------------------ ADC / PWM */
static bool g_adc_on;
static int r_adc_read(void* ctx, int ch) {
    (void)ctx;
    if (ch < 0 || ch >= (int)NUM_ADC_CHANNELS) return MCS_HAL_ENOTSUP;
    if (!g_adc_on) { adc_init(); g_adc_on = true; }
    if (ch == (int)NUM_ADC_CHANNELS - 1) adc_set_temp_sensor_enabled(true);
    else adc_gpio_init((uint)(ADC_BASE_PIN + ch));
    adc_select_input((uint)ch);
    return (int)adc_read();
}
static int r_pwm_set16(void* ctx, int pin, uint32_t freq, uint16_t duty) {
    (void)ctx;
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_ENOTSUP;
    if (!freq) return MCS_HAL_EINVAL;
    uint slice = pwm_gpio_to_slice_num((uint)pin), chan = pwm_gpio_to_channel((uint)pin);
    uint32_t clk = clock_get_hz(clk_sys);
    /* integer + 4-bit fractional divider so that top fits 16 bits */
    uint32_t div16 = (uint32_t)(((uint64_t)clk * 16u + (uint64_t)freq * 65536u - 1) / ((uint64_t)freq * 65536u));
    if (div16 < 16) div16 = 16;
    if (div16 > 255u * 16u + 15u) return MCS_HAL_EINVAL;      /* below ~8 Hz at 125 MHz */
    uint32_t top = (uint32_t)((uint64_t)clk * 16u / ((uint64_t)div16 * freq)) - 1;
    if (top > 65535u) top = 65535u;
    gpio_set_function((uint)pin, GPIO_FUNC_PWM);
    pwm_set_clkdiv_int_frac(slice, (uint8_t)(div16 >> 4), (uint8_t)(div16 & 15));
    pwm_set_wrap(slice, (uint16_t)top);
    pwm_set_chan_level(slice, chan, (uint16_t)(((uint64_t)(top + 1) * duty) / 65535u));
    pwm_set_enabled(slice, true);
    return 0;
}
static int r_pwm_set(void* ctx, int pin, uint32_t freq, uint16_t permille) {
    return r_pwm_set16(ctx, pin, freq, (uint16_t)((uint32_t)permille * 65535u / 1000u));
}
static int r_pwm_stop(void* ctx, int pin) {
    (void)ctx;
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_ENOTSUP;
    pwm_set_chan_level(pwm_gpio_to_slice_num((uint)pin), pwm_gpio_to_channel((uint)pin), 0);
    gpio_init((uint)pin);       /* back to a plain low GPIO */
    return 0;
}

/* ------------------------------------------------------------------ timers */
static repeating_timer_t g_rt[MCS_RP2_TIMERS];
static alarm_id_t g_alarm[MCS_RP2_TIMERS];
static bool g_rt_on[MCS_RP2_TIMERS];
static volatile uint32_t g_tcount[MCS_RP2_TIMERS];
static bool rt_cb(repeating_timer_t* rt) {
    int i = (int)(intptr_t)rt->user_data;
    mcs_hal_post(MCS_HAL_EV_TIMER, i, (int32_t)++g_tcount[i]);
    return true;
}
static int64_t alarm_cb(alarm_id_t id, void* ud) {
    (void)id;
    int i = (int)(intptr_t)ud;
    g_alarm[i] = 0;
    mcs_hal_post(MCS_HAL_EV_TIMER, i, (int32_t)++g_tcount[i]);
    return 0;
}
static int r_timer_stop(void* ctx, int i) {
    (void)ctx;
    if (i < 0 || i >= MCS_RP2_TIMERS) return MCS_HAL_ENOTSUP;
    if (g_rt_on[i]) { cancel_repeating_timer(&g_rt[i]); g_rt_on[i] = false; }
    if (g_alarm[i] > 0) { cancel_alarm(g_alarm[i]); g_alarm[i] = 0; }
    return 0;
}
static int r_timer_start(void* ctx, int i, uint32_t period_us, int periodic) {
    if (i < 0 || i >= MCS_RP2_TIMERS) return MCS_HAL_ENOTSUP;
    if (!period_us) return MCS_HAL_EINVAL;
    r_timer_stop(ctx, i);
    g_tcount[i] = 0;
    if (periodic) {
        if (!add_repeating_timer_us(-(int64_t)period_us, rt_cb, (void*)(intptr_t)i, &g_rt[i])) return MCS_HAL_EBUSY;
        g_rt_on[i] = true;
    } else {
        alarm_id_t a = add_alarm_in_us(period_us, alarm_cb, (void*)(intptr_t)i, true);
        if (a < 0) return MCS_HAL_EBUSY;
        g_alarm[i] = a;
    }
    return 0;
}

/* ------------------------------------------------------------------ watchdog / clock / system */
static int r_wdt_start(void* ctx, uint32_t ms) {
    (void)ctx;
    if (ms > 8000) ms = 8000;        /* hardware limit (RP2040 ~8.3 s) */
    watchdog_enable(ms, true);
    return 0;
}
static int r_wdt_feed(void* ctx) { (void)ctx; watchdog_update(); return 0; }
static int64_t g_rtc_offset;         /* unix seconds at boot (software clock) */
static int r_rtc_get(void* ctx, uint32_t* secs) {
    (void)ctx;
    *secs = (uint32_t)(g_rtc_offset + (int64_t)(time_us_64() / 1000000u));
    return 0;
}
static int r_rtc_set(void* ctx, uint32_t secs) {
    (void)ctx;
    g_rtc_offset = (int64_t)secs - (int64_t)(time_us_64() / 1000000u);
    return 0;
}
static uint32_t r_micros(void* ctx) { (void)ctx; return time_us_32(); }
static void r_delay_us(void* ctx, uint32_t us) { (void)ctx; busy_wait_us_32(us); }
static int r_reset(void* ctx) { (void)ctx; watchdog_reboot(0, 0, 0); for (;;) tight_loop_contents(); return 0; }
static int r_unique_id(void* ctx, uint8_t* buf, size_t cap) {
    (void)ctx;
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    size_t n = cap < PICO_UNIQUE_BOARD_ID_SIZE_BYTES ? cap : PICO_UNIQUE_BOARD_ID_SIZE_BYTES;
    memcpy(buf, id.id, n);
    return (int)n;
}

uint32_t mcs_rp2_ticks(void* ud) { (void)ud; return to_ms_since_boot(get_absolute_time()); }
void mcs_rp2_delay(void* ud, uint32_t ms) { (void)ud; sleep_ms(ms); }

static int con_read(void* ud, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    (void)ud;
    int c = getchar_timeout_us(timeout_ms * 1000u);
    if (c < 0) return 0;
    size_t k = 0;
    buf[k++] = (uint8_t)c;
    while (k < n && (c = getchar_timeout_us(0)) >= 0) buf[k++] = (uint8_t)c;
    return (int)k;
}
static void con_write(void* ud, const char* s, size_t n) {
    (void)ud;
    while (n--) putchar_raw(*s++);      /* raw: no \n -> \r\n translation (binary uploads) */
    stdio_flush();
}
mcs_transport_t mcs_rp2_console_stdio(void) {
    mcs_transport_t t = { con_read, con_write, NULL };
    return t;
}

static int r_pin_lookup(void* ctx, const char* name) {
    (void)ctx;
#ifdef PICO_DEFAULT_LED_PIN
    if (!strcmp(name, "LED") || !strcmp(name, "LED_BUILTIN")) return PICO_DEFAULT_LED_PIN;
#else
    (void)name;
#endif
    return -1;                       /* -> generic parser: "GP15", "GPIO15", "15" */
}
void mcs_rp2_hal_init(mcs_hal_t* hal, const mcs_rp2_cfg_t* cfg) {
    static const mcs_rp2_cfg_t defaults = MCS_RP2_CFG_DEFAULT;
    g_cfg = cfg ? *cfg : defaults;
    memset(hal, 0, sizeof *hal);
#if PICO_RP2350
    hal->board = g_cfg.name ? g_cfg.name : "RP2350";
#else
    hal->board = g_cfg.name ? g_cfg.name : "RP2040";
#endif
    hal->ctx = &g_cfg;
    hal->gpio_mode = r_gpio_mode;
    hal->gpio_write = r_gpio_write;
    hal->gpio_read = r_gpio_read;
    hal->gpio_irq = r_gpio_irq;
    hal->uart_open = r_uart_open;
    hal->uart_config = r_uart_config;
    hal->uart_close = r_uart_close;
    hal->uart_write = r_uart_write;
    hal->uart_read = r_uart_read;
    hal->uart_available = r_uart_available;
    hal->i2c_open = r_i2c_open;
    hal->i2c_write = r_i2c_write;
    hal->i2c_read = r_i2c_read;
    hal->i2c_write_read = r_i2c_write_read;
    hal->i2c_probe = r_i2c_probe;
    hal->spi_open = r_spi_open;
    hal->spi_transfer = r_spi_transfer;
    hal->adc_read = r_adc_read;
    hal->adc_bits = 12;
    hal->adc_vref_mv = 3300;
    hal->pwm_set = r_pwm_set;
    hal->pwm_set16 = r_pwm_set16;
    hal->pwm_stop = r_pwm_stop;
    hal->timer_start = r_timer_start;
    hal->timer_stop = r_timer_stop;
    hal->wdt_start = r_wdt_start;
    hal->wdt_feed = r_wdt_feed;
    hal->rtc_get = r_rtc_get;
    hal->rtc_set = r_rtc_set;
    hal->micros = r_micros;
    hal->delay_us = r_delay_us;
    hal->reset = r_reset;
    hal->unique_id = r_unique_id;
    hal->pin_lookup = r_pin_lookup;
    hal->cpu_hz = clock_get_hz(clk_sys);
}
