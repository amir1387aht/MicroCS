/* MicroCS port for RP2040 / RP2350 - pico-sdk. See mcs_port_rp2.h. */
#include "mcs_port_rp2.h"
#include "mcs_driver.h"
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
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
#include "hardware/dma.h"
#if MCS_ENABLE_FLASH
#include "hardware/flash.h"
#include "hardware/sync.h"
#if __has_include("pico/flash.h")
#include "pico/flash.h"
#define MCS_RP2_SAFE_EXEC 1
#endif
#endif

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
static uint64_t g_inv_mask;          /* GpioConfig.Invert on a plain GPIO (done here: SIO reads the pad) */
#define R_INV(pin) ((int)((g_inv_mask >> (pin)) & 1u))

/* ------------------------------------------------------------------ GPIO */
static int r_gpio_mode(void* ctx, int pin, int mode) {
    (void)ctx;
#if MCS_RP2_WIFI
    if (pin >= MCS_RP2_WL_PIN(0) && pin <= MCS_RP2_WL_PIN(2)) return mode == MCS_GPIO_OUTPUT || mode == MCS_GPIO_INPUT ? 0 : MCS_HAL_ENOTSUP;
#endif
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_EINVAL;
    uint p = (uint)pin;
    gpio_init(p);
    gpio_set_outover(p, GPIO_OVERRIDE_NORMAL);
    gpio_set_inover(p, GPIO_OVERRIDE_NORMAL);
    g_od_mask &= ~(1ull << pin);
    g_inv_mask &= ~(1ull << pin);
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
#if MCS_RP2_WIFI
    if (pin >= MCS_RP2_WL_PIN(0) && pin <= MCS_RP2_WL_PIN(2)) return mcs_rp2_wl_gpio(pin, 1, v);
#endif
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_EINVAL;
    v = (v != 0) ^ R_INV(pin);
    if (g_od_mask & (1ull << pin)) gpio_set_dir((uint)pin, v ? GPIO_IN : GPIO_OUT);
    else gpio_put((uint)pin, v != 0);
    return 0;
}
static int r_gpio_read(void* ctx, int pin) {
    (void)ctx;
#if MCS_RP2_WIFI
    if (pin >= MCS_RP2_WL_PIN(0) && pin <= MCS_RP2_WL_PIN(2)) return mcs_rp2_wl_gpio(pin, 0, 0);
#endif
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_EINVAL;
    return (gpio_get((uint)pin) ? 1 : 0) ^ R_INV(pin);
}
static void gpio_cb(uint gpio, uint32_t events) {
    int level = (events & GPIO_IRQ_EDGE_RISE) && !(events & GPIO_IRQ_EDGE_FALL) ? 1
              : (events & GPIO_IRQ_EDGE_FALL) && !(events & GPIO_IRQ_EDGE_RISE) ? 0 : (int)gpio_get(gpio);
    mcs_hal_post(MCS_HAL_EV_GPIO, (int)gpio, level ^ R_INV(gpio));
}
static int r_gpio_configure(void* ctx, int pin, const mcs_gpio_config_t* c) {
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_EINVAL;
    if (c->hold == MCS_ON) { mcs_hal_error("Hold: not available on RP2 pads"); return MCS_HAL_ENOTSUP; }
    if (c->filter_ns) { mcs_hal_error("FilterNs: RP2 GPIOs have no glitch filter (use Schmitt)"); return MCS_HAL_ENOTSUP; }
    if (c->drive_ma && c->drive_ma != 2 && c->drive_ma != 4 && c->drive_ma != 8 && c->drive_ma != 12) {
        mcs_hal_error("DriveMa: 2, 4, 8 or 12 mA");
        return MCS_HAL_EINVAL;
    }
    if (c->alternate > 31 || c->pull > MCS_PULL_BOTH) return MCS_HAL_EINVAL;
    uint p = (uint)pin;
    int r, lvl = (c->initial != 0) ^ (c->invert == MCS_ON);
    if (c->mode == MCS_GPIO_OUTPUT && c->initial >= 0) {     /* level first, then drive: no glitch */
        if ((r = r_gpio_mode(ctx, pin, MCS_GPIO_INPUT))) return r;
        gpio_put(p, lvl);
        gpio_set_dir(p, GPIO_OUT);
    } else if ((r = r_gpio_mode(ctx, pin, c->mode))) return r;
    if (c->mode == MCS_GPIO_OPEN_DRAIN && c->initial >= 0 && !lvl) gpio_set_dir(p, GPIO_OUT);
    if (c->pull) gpio_set_pulls(p, c->pull == MCS_PULL_UP || c->pull == MCS_PULL_BOTH, c->pull == MCS_PULL_DOWN || c->pull == MCS_PULL_BOTH);
    if (c->drive_ma) gpio_set_drive_strength(p, c->drive_ma == 2 ? GPIO_DRIVE_STRENGTH_2MA : c->drive_ma == 4 ? GPIO_DRIVE_STRENGTH_4MA
                                               : c->drive_ma == 8 ? GPIO_DRIVE_STRENGTH_8MA : GPIO_DRIVE_STRENGTH_12MA);
    if (c->speed) gpio_set_slew_rate(p, c->speed >= MCS_SPEED_HIGH ? GPIO_SLEW_RATE_FAST : GPIO_SLEW_RATE_SLOW);
    if (c->schmitt) gpio_set_input_hysteresis_enabled(p, c->schmitt == MCS_ON);
    if (c->alternate >= 0) {
        /* peripheral function: invert in the pad mux, the signal never passes SIO */
        gpio_set_function(p, (gpio_function_t)c->alternate);
        gpio_set_outover(p, c->invert == MCS_ON ? GPIO_OVERRIDE_INVERT : GPIO_OVERRIDE_NORMAL);
        gpio_set_inover(p, c->invert == MCS_ON ? GPIO_OVERRIDE_INVERT : GPIO_OVERRIDE_NORMAL);
    } else if (c->invert == MCS_ON) g_inv_mask |= 1ull << pin;
    return 0;
}
static int r_gpio_irq(void* ctx, int pin, int edge) {
    (void)ctx;
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_EINVAL;
    if (R_INV(pin) && edge != MCS_GPIO_EDGE_BOTH) edge = edge == MCS_GPIO_EDGE_RISING ? MCS_GPIO_EDGE_FALLING : edge == MCS_GPIO_EDGE_FALLING ? MCS_GPIO_EDGE_RISING : edge;
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
static int r_uart_configure(void* ctx, int port, const mcs_uart_config_t* x) {
    uart_inst_t* u = uart_of(port);
    if (!u) return MCS_HAL_ENOTSUP;
    if (x->rs485) { mcs_hal_error("Rs485: the RP2 UART has no RS-485 mode - drive the DE pin from C#"); return MCS_HAL_ENOTSUP; }
    if (x->dma == MCS_ON) { mcs_hal_error("Dma: RP2 UART receive uses an interrupt ring buffer"); return MCS_HAL_ENOTSUP; }
    if (x->rx_buffer && x->rx_buffer != MCS_RP2_UART_RXBUF) { mcs_hal_error("RxBuffer: fixed at MCS_RP2_UART_RXBUF bytes (compile time)"); return MCS_HAL_EINVAL; }
    if (x->tx_buffer) { mcs_hal_error("TxBuffer: RP2 UART writes are blocking"); return MCS_HAL_ENOTSUP; }
    if (x->rx_timeout) { mcs_hal_error("RxTimeout: not configurable on RP2"); return MCS_HAL_ENOTSUP; }
    mcs_rp2_uart_pins_t* pp = &g_cfg.uart[port];
    if (x->tx >= 0 && x->tx != pp->tx) { if (pp->tx >= 0) gpio_init((uint)pp->tx); pp->tx = x->tx; }
    if (x->rx >= 0 && x->rx != pp->rx) { if (pp->rx >= 0) gpio_init((uint)pp->rx); pp->rx = x->rx; }
    int r = r_uart_config(ctx, port, &x->frame);
    if (r) return r;
    if (x->rts >= 0) gpio_set_function((uint)x->rts, GPIO_FUNC_UART);
    if (x->cts >= 0) gpio_set_function((uint)x->cts, GPIO_FUNC_UART);
    gpio_set_outover((uint)pp->tx, x->invert_tx ? GPIO_OVERRIDE_INVERT : GPIO_OVERRIDE_NORMAL);
    if (pp->rx >= 0) gpio_set_inover((uint)pp->rx, x->invert_rx ? GPIO_OVERRIDE_INVERT : GPIO_OVERRIDE_NORMAL);
    return 0;
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
static uint8_t g_i2c_nopull[2], g_i2c_spk[2];
static uint32_t g_i2c_to_us[2];
#define I2C_TO(bus) (g_i2c_to_us[bus] ? g_i2c_to_us[bus] : MCS_RP2_TIMEOUT_US)
static i2c_inst_t* i2c_of(int bus) { return bus == 0 ? i2c0 : bus == 1 ? i2c1 : NULL; }
static int r_i2c_open(void* ctx, int bus, uint32_t freq) {
    (void)ctx;
    i2c_inst_t* i = i2c_of(bus);
    if (!i || g_cfg.i2c[bus].sda < 0) return MCS_HAL_ENOTSUP;
    i2c_init(i, freq ? freq : 100000);
    if (g_i2c_spk[bus]) {             /* spike suppression length, written while disabled */
        i2c_hw_t* hw = i2c_get_hw(i);
        hw->enable = 0;
        hw->fs_spklen = g_i2c_spk[bus];
        hw->enable = 1;
    }
    gpio_set_function((uint)g_cfg.i2c[bus].sda, GPIO_FUNC_I2C);
    gpio_set_function((uint)g_cfg.i2c[bus].scl, GPIO_FUNC_I2C);
    if (g_i2c_nopull[bus]) {
        gpio_disable_pulls((uint)g_cfg.i2c[bus].sda);
        gpio_disable_pulls((uint)g_cfg.i2c[bus].scl);
    } else {
        gpio_pull_up((uint)g_cfg.i2c[bus].sda);
        gpio_pull_up((uint)g_cfg.i2c[bus].scl);
    }
    g_i2c_on[bus] = true;
    return 0;
}
static int r_i2c_configure(void* ctx, int bus, const mcs_i2c_config_t* c) {
    if (!i2c_of(bus)) return MCS_HAL_ENOTSUP;
    if (c->dma == MCS_ON) { mcs_hal_error("Dma: RP2 I2C transfers are short and run from the FIFO"); return MCS_HAL_ENOTSUP; }
    if (c->analog_filter == MCS_ON) { mcs_hal_error("AnalogFilter: not available on RP2 (use GlitchFilter)"); return MCS_HAL_ENOTSUP; }
    mcs_rp2_i2c_pins_t* pp = &g_cfg.i2c[bus];
    if (c->sda >= 0 && c->sda != pp->sda) { if (pp->sda >= 0) gpio_init((uint)pp->sda); pp->sda = c->sda; }
    if (c->scl >= 0 && c->scl != pp->scl) { if (pp->scl >= 0) gpio_init((uint)pp->scl); pp->scl = c->scl; }
    if (pp->sda < 0 || pp->scl < 0) { mcs_hal_error("no I2C pins for this bus - set Sda and Scl"); return MCS_HAL_ENOTSUP; }
    g_i2c_nopull[bus] = c->pullups == MCS_OFF;
    g_i2c_spk[bus] = c->glitch_filter;
    g_i2c_to_us[bus] = (uint32_t)c->timeout_ms * 1000u;
    return r_i2c_open(ctx, bus, c->freq_hz);
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
    return i2c_rc(i2c_write_timeout_us(i, (uint8_t)addr, d, n, false, I2C_TO(bus)), n);
}
static int r_i2c_read(void* ctx, int bus, int addr, uint8_t* buf, size_t n) {
    (void)ctx; int err; i2c_inst_t* i = i2c_ready(bus, &err);
    if (!i) return err;
    return i2c_rc(i2c_read_timeout_us(i, (uint8_t)addr, buf, n, false, I2C_TO(bus)), n);
}
static int r_i2c_write_read(void* ctx, int bus, int addr, const uint8_t* tx, size_t tn, uint8_t* rx, size_t rn) {
    (void)ctx; int err; i2c_inst_t* i = i2c_ready(bus, &err);
    if (!i) return err;
    int r = i2c_rc(i2c_write_timeout_us(i, (uint8_t)addr, tx, tn, true, I2C_TO(bus)), tn);   /* no stop: repeated start */
    return r ? r : i2c_rc(i2c_read_timeout_us(i, (uint8_t)addr, rx, rn, false, I2C_TO(bus)), rn);
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

static uint8_t g_spi_nodma[2];
static int r_spi_configure(void* ctx, int bus, const mcs_spi_config_t* c) {
    if (!spi_of(bus)) return MCS_HAL_ENOTSUP;
    if (c->three_wire) { mcs_hal_error("ThreeWire: the RP2 SPI (PL022) is full duplex only"); return MCS_HAL_ENOTSUP; }
    if (c->input_delay_ns) { mcs_hal_error("InputDelayNs: not adjustable on RP2 (lower the frequency)"); return MCS_HAL_ENOTSUP; }
    if (c->bus.lsb_first) { mcs_hal_error("LsbFirst: the RP2 SPI sends MSB first only"); return MCS_HAL_ENOTSUP; }
    mcs_rp2_spi_pins_t* pp = &g_cfg.spi[bus];
    if (c->sck >= 0 && c->sck != pp->sck) { if (pp->sck >= 0) gpio_init((uint)pp->sck); pp->sck = c->sck; }
    if (c->mosi >= 0 && c->mosi != pp->mosi) { if (pp->mosi >= 0) gpio_init((uint)pp->mosi); pp->mosi = c->mosi; }
    if (c->miso >= 0 && c->miso != pp->miso) { if (pp->miso >= 0) gpio_init((uint)pp->miso); pp->miso = c->miso; }
    g_spi_nodma[bus] = c->dma == MCS_OFF;
    return r_spi_open(ctx, bus, &c->bus);
}

/* ------------------------------------------------------------------ DMA
 * SPI: a TX and an RX channel paced by the SPI DREQs (dummy 0xFF / sink when one
 * side is NULL), done when RX has all bytes. Memory: one channel, DREQ_FORCE.
 * Completion on DMA_IRQ_1 (shared handler) -> MCS_HAL_EV_DMA. */
typedef struct { int8_t ch, ch2; uint16_t id; int32_t n; volatile uint8_t on; } r_dma_job_t;
static r_dma_job_t g_sdma[2], g_mdma[MCS_HAL_MAX_DMA];
static uint8_t g_dummy_tx = 0xFF, g_dummy_rx;
static void r_dma_end(r_dma_job_t* j, bool post) {
    dma_channel_set_irq1_enabled((uint)j->ch, false);
    dma_channel_acknowledge_irq1((uint)j->ch);
    dma_channel_unclaim((uint)j->ch);
    if (j->ch2 >= 0) dma_channel_unclaim((uint)j->ch2);
    j->on = 0;
    if (post) mcs_hal_post(MCS_HAL_EV_DMA, j->id, j->n);
}
static void r_dma_isr(void) {
    for (int i = 0; i < 2; i++)
        if (g_sdma[i].on && dma_channel_get_irq1_status((uint)g_sdma[i].ch)) r_dma_end(&g_sdma[i], true);
    for (int i = 0; i < MCS_HAL_MAX_DMA; i++)
        if (g_mdma[i].on && dma_channel_get_irq1_status((uint)g_mdma[i].ch)) r_dma_end(&g_mdma[i], true);
}
static void r_dma_irq_init(void) {
    static bool on;
    if (on) return;
    irq_add_shared_handler(DMA_IRQ_1, r_dma_isr, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);
    on = true;
}
static int r_spi_transfer_async(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n, int id) {
    spi_inst_t* s = spi_of(bus);
    if (!s) return MCS_HAL_ENOTSUP;
    if (!g_spi_on[bus]) {
        mcs_spi_cfg_t c = { 1000000, 0, 8, 0, 0 };
        int r = r_spi_open(ctx, bus, &c);
        if (r) return r;
    }
    if (g_spi_nodma[bus]) return MCS_HAL_ENOTSUP;          /* SpiConfig.Dma = false: synchronous */
    if (g_sdma[bus].on) return MCS_HAL_EBUSY;
    int tc = dma_claim_unused_channel(false);
    if (tc < 0) return MCS_HAL_ENOTSUP;
    int rc = dma_claim_unused_channel(false);
    if (rc < 0) { dma_channel_unclaim((uint)tc); return MCS_HAL_ENOTSUP; }
    dma_channel_config ct = dma_channel_get_default_config((uint)tc);
    channel_config_set_transfer_data_size(&ct, DMA_SIZE_8);
    channel_config_set_dreq(&ct, spi_get_dreq(s, true));
    channel_config_set_read_increment(&ct, tx != NULL);
    channel_config_set_write_increment(&ct, false);
    dma_channel_configure((uint)tc, &ct, &spi_get_hw(s)->dr, tx ? tx : &g_dummy_tx, (uint32_t)n, false);
    dma_channel_config cr = dma_channel_get_default_config((uint)rc);
    channel_config_set_transfer_data_size(&cr, DMA_SIZE_8);
    channel_config_set_dreq(&cr, spi_get_dreq(s, false));
    channel_config_set_read_increment(&cr, false);
    channel_config_set_write_increment(&cr, rx != NULL);
    dma_channel_configure((uint)rc, &cr, rx ? (void*)rx : (void*)&g_dummy_rx, &spi_get_hw(s)->dr, (uint32_t)n, false);
    while (spi_is_readable(s)) (void)spi_get_hw(s)->dr;    /* stale RX bytes */
    r_dma_job_t* j = &g_sdma[bus];
    j->ch = (int8_t)rc; j->ch2 = (int8_t)tc; j->id = (uint16_t)id; j->n = (int32_t)n; j->on = 1;
    r_dma_irq_init();
    dma_channel_acknowledge_irq1((uint)rc);
    dma_channel_set_irq1_enabled((uint)rc, true);
    dma_start_channel_mask((1u << tc) | (1u << rc));
    return 0;
}
static int r_dma_copy(void* ctx, void* dst, const void* src, size_t n, unsigned flags, int id) {
    (void)ctx;
    r_dma_job_t* j = NULL;
    for (int i = 0; i < MCS_HAL_MAX_DMA; i++) if (!g_mdma[i].on) { j = &g_mdma[i]; break; }
    if (!j || !n) return MCS_HAL_ENOTSUP;
    int ch = dma_claim_unused_channel(false);
    if (ch < 0) return MCS_HAL_ENOTSUP;                    /* all channels busy: the CPU copies */
    bool words = !(flags & MCS_DMA_SRC_FIXED) && !(((uintptr_t)dst | (uintptr_t)src | n) & 3u);
    dma_channel_config c = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&c, words ? DMA_SIZE_32 : DMA_SIZE_8);
    channel_config_set_read_increment(&c, !(flags & MCS_DMA_SRC_FIXED));
    channel_config_set_write_increment(&c, !(flags & MCS_DMA_DST_FIXED));
    j->ch = (int8_t)ch; j->ch2 = -1; j->id = (uint16_t)id; j->n = (int32_t)n; j->on = 1;
    r_dma_irq_init();
    dma_channel_acknowledge_irq1((uint)ch);
    dma_channel_set_irq1_enabled((uint)ch, true);
    dma_channel_configure((uint)ch, &c, dst, src, (uint32_t)(words ? n / 4 : n), true);
    return 0;
}
static int r_dma_abort(void* ctx, int id) {
    (void)ctx;
    r_dma_job_t* all[2 + MCS_HAL_MAX_DMA] = { &g_sdma[0], &g_sdma[1] };
    for (int i = 0; i < MCS_HAL_MAX_DMA; i++) all[2 + i] = &g_mdma[i];
    for (int i = 0; i < 2 + MCS_HAL_MAX_DMA; i++) {
        r_dma_job_t* j = all[i];
        if (!j->on || j->id != (uint16_t)id) continue;
        dma_channel_set_irq1_enabled((uint)j->ch, false);   /* RP2040-E13: disable the IRQ before aborting */
        dma_channel_abort((uint)j->ch);
        if (j->ch2 >= 0) dma_channel_abort((uint)j->ch2);
        r_dma_end(j, false);
        return 0;
    }
    return MCS_HAL_EINVAL;
}

/* ------------------------------------------------------------------ ADC / PWM */
static bool g_adc_on;
static uint8_t g_adc_avg[NUM_ADC_CHANNELS];
static int r_adc_read(void* ctx, int ch) {
    (void)ctx;
    if (ch < 0 || ch >= (int)NUM_ADC_CHANNELS) return MCS_HAL_ENOTSUP;
    if (!g_adc_on) { adc_init(); g_adc_on = true; }
    if (ch == (int)NUM_ADC_CHANNELS - 1) adc_set_temp_sensor_enabled(true);
    else adc_gpio_init((uint)(ADC_BASE_PIN + ch));
    adc_select_input((uint)ch);
    int n = g_adc_avg[ch] ? g_adc_avg[ch] : 1;
    uint32_t sum = 0;
    for (int i = 0; i < n; i++) sum += adc_read();      /* Oversample: averaged in software */
    return (int)(sum / (uint32_t)n);
}
static int r_adc_configure(void* ctx, int ch, const mcs_adc_config_t* c) {
    (void)ctx;
    if (ch < 0 || ch >= (int)NUM_ADC_CHANNELS) return MCS_HAL_ENOTSUP;
    if (c->resolution_bits && c->resolution_bits != 12) { mcs_hal_error("ResolutionBits: the RP2 ADC is 12-bit"); return MCS_HAL_ENOTSUP; }
    if (c->attenuation) { mcs_hal_error("Attenuation: the RP2 ADC range is fixed at 0..ADC_VREF"); return MCS_HAL_ENOTSUP; }
    if (c->sample_time) { mcs_hal_error("SampleTime: fixed at 96 ADC clocks on RP2"); return MCS_HAL_ENOTSUP; }
    if (c->oversample > 128 || (c->oversample & (c->oversample - 1))) { mcs_hal_error("Oversample: 0, 1, 2, 4 .. 128"); return MCS_HAL_EINVAL; }
    g_adc_avg[ch] = c->oversample;
    return 0;
}
/* per slice: PwmConfig.ResolutionBits, centre-aligned, inverted channels (bit 0 = A, 1 = B) */
static uint8_t g_pwm_res[NUM_PWM_SLICES], g_pwm_cen[NUM_PWM_SLICES], g_pwm_inv[NUM_PWM_SLICES];
static int r_pwm_set16(void* ctx, int pin, uint32_t freq, uint16_t duty) {
    (void)ctx;
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_ENOTSUP;
    if (!freq) return MCS_HAL_EINVAL;
    uint slice = pwm_gpio_to_slice_num((uint)pin), chan = pwm_gpio_to_channel((uint)pin);
    uint32_t clk = clock_get_hz(clk_sys);
    uint64_t f = (uint64_t)freq * (g_pwm_cen[slice] ? 2u : 1u);     /* phase-correct counts up and down */
    uint32_t div16, top;
    if (g_pwm_res[slice]) {
        top = (1u << g_pwm_res[slice]) - 1u;
        div16 = (uint32_t)(((uint64_t)clk * 16u) / (f * (top + 1u)));
        if (div16 < 16 || div16 > 255u * 16u + 15u) { mcs_hal_error("ResolutionBits: does not fit this frequency (clk_sys / 2^bits / freq must be 1..255)"); return MCS_HAL_EINVAL; }
    } else {
        /* integer + 4-bit fractional divider so that top fits 16 bits */
        div16 = (uint32_t)(((uint64_t)clk * 16u + f * 65536u - 1) / (f * 65536u));
        if (div16 < 16) div16 = 16;
        if (div16 > 255u * 16u + 15u) return MCS_HAL_EINVAL;      /* below ~8 Hz at 125 MHz */
        top = (uint32_t)((uint64_t)clk * 16u / ((uint64_t)div16 * f)) - 1;
        if (top > 65535u) top = 65535u;
    }
    gpio_set_function((uint)pin, GPIO_FUNC_PWM);
    pwm_set_clkdiv_int_frac(slice, (uint8_t)(div16 >> 4), (uint8_t)(div16 & 15));
    pwm_set_wrap(slice, (uint16_t)top);
    pwm_set_phase_correct(slice, g_pwm_cen[slice] != 0);
    pwm_set_output_polarity(slice, g_pwm_inv[slice] & 1, (g_pwm_inv[slice] >> 1) & 1);
    pwm_set_chan_level(slice, chan, (uint16_t)(((uint64_t)(top + 1) * duty) / 65535u));
    pwm_set_enabled(slice, true);
    return 0;
}
static int r_pwm_configure(void* ctx, int pin, const mcs_pwm_config_t* c) {
    if (pin < 0 || pin >= (int)NUM_BANK0_GPIOS) return MCS_HAL_ENOTSUP;
    if (c->pin >= 0 && c->pin != pin) { mcs_hal_error("Pin: on RP2 the PWM channel number is the GPIO"); return MCS_HAL_ENOTSUP; }
    if (c->resolution_bits > 16) { mcs_hal_error("ResolutionBits: 1..16"); return MCS_HAL_EINVAL; }
    uint slice = pwm_gpio_to_slice_num((uint)pin), chan = pwm_gpio_to_channel((uint)pin);
    g_pwm_res[slice] = c->resolution_bits;
    g_pwm_cen[slice] = c->center_aligned ? 1 : 0;
    g_pwm_inv[slice] = (uint8_t)((g_pwm_inv[slice] & ~(1u << chan)) | ((c->invert ? 1u : 0u) << chan));
    return c->freq_hz ? r_pwm_set16(ctx, pin, c->freq_hz, 0) : 0;
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
/* RTC = the C library clock (time(), gettimeofday), which SNTP sets on a Pico W */
static int r_rtc_get(void* ctx, uint32_t* secs) {
    (void)ctx;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *secs = (uint32_t)tv.tv_sec;
    return 0;
}
static int r_rtc_set(void* ctx, uint32_t secs) {
    (void)ctx;
    struct timeval tv = { (time_t)secs, 0 };
    settimeofday(&tv, NULL);
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
    /* One SDK write per block, not per byte: USB writes pump TinyUSB and flush
     * a packet each time. Byte writes turn caps / file output into hundreds of
     * tiny packets, especially expensive under FreeRTOS. Keep binary output raw. */
    stdio_put_string(s, (int)n, false, false);
    stdio_flush();
}
mcs_transport_t mcs_rp2_console_stdio(void) {
    mcs_transport_t t = { con_read, con_write, NULL };
    return t;
}

/* "ws2812" driver (C# LedStrip): PIO backend shared with the Arduino-Pico port */
#if MCS_ENABLE_WS2812
#define MCS_WS2812_RP2_PIO 1
#include "mcs_ws2812_backends.h"
static const mcs_ws2812_ops_t r_ws2812_ops = { mcs_ws2812_pio_write };
static const mcs_driver_t r_ws2812 = MCS_WS2812_DRIVER(&r_ws2812_ops, NULL);
#endif
static int r_pin_lookup(void* ctx, const char* name) {
    (void)ctx;
#ifdef PICO_DEFAULT_LED_PIN
    if (!strcmp(name, "LED") || !strcmp(name, "LED_BUILTIN")) return PICO_DEFAULT_LED_PIN;
#elif MCS_RP2_WIFI && defined(CYW43_WL_GPIO_LED_PIN)
    if (!strcmp(name, "LED") || !strcmp(name, "LED_BUILTIN")) return MCS_RP2_WL_PIN(CYW43_WL_GPIO_LED_PIN);
#endif
#if MCS_RP2_WIFI
    if (!strncmp(name, "WL_GPIO", 7) && name[7] >= '0' && name[7] <= '2' && !name[8]) return MCS_RP2_WL_PIN(name[7] - '0');
#endif
#ifdef PICO_DEFAULT_WS2812_PIN       /* boards with an RGB LED: Waveshare RP2040-Zero (GP16), ... */
    if (!strcmp(name, "NEOPIXEL") || !strcmp(name, "RGB_LED") || !strcmp(name, "WS2812")) return PICO_DEFAULT_WS2812_PIN;
#endif
    (void)name;
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
#if MCS_ENABLE_WS2812
    mcs_driver_register_default(&r_ws2812);       /* keeps a "ws2812" you registered first */
#endif
    hal->cpu_hz = clock_get_hz(clk_sys);
    /* v3: full configuration + DMA (all SRAM is DMA-capable: DmaBuffer uses the VM heap) */
    hal->gpio_configure = r_gpio_configure;
    hal->uart_configure = r_uart_configure;
    hal->i2c_configure = r_i2c_configure;
    hal->spi_configure = r_spi_configure;
    hal->pwm_configure = r_pwm_configure;
    hal->adc_configure = r_adc_configure;
    hal->dma_align = 4;
    hal->dma_channels = NUM_DMA_CHANNELS;
    hal->dma_copy = r_dma_copy;
    hal->dma_abort = r_dma_abort;
    hal->spi_transfer_async = r_spi_transfer_async;
#if MCS_RP2_WIFI && MCS_ENABLE_NET
    mcs_net_set(mcs_rp2_net());                    /* C# Wifi, Ntp, Mdns, Ping; sockets over lwIP */
#endif
#if MCS_RP2_BLE && MCS_ENABLE_BLE
    mcs_ble_set(mcs_rp2_ble());                    /* C# Ble, BleService, BleConnection, BleUart (BTstack) */
#endif
}

/* ------------------------------------------------------------------ internal flash */
#if MCS_ENABLE_FLASH
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2u << 20)
#endif
extern char __flash_binary_end;
typedef struct { uint32_t off; const uint8_t* data; uint32_t n; int erase; } rp2_flash_op_t;
static void rp2_flash_do(void* p) {
    rp2_flash_op_t* op = (rp2_flash_op_t*)p;
    if (op->erase) flash_range_erase(op->off, op->n);
    else flash_range_program(op->off, op->data, op->n);
}
static int rp2_flash_run(rp2_flash_op_t* op) {
#if MCS_RP2_SAFE_EXEC
    if (flash_safe_execute(rp2_flash_do, op, 500) == PICO_OK) return 0;
#endif
    uint32_t irq = save_and_disable_interrupts();
    rp2_flash_do(op);
    restore_interrupts(irq);
    return 0;
}
static int rp2_fl_read(mcs_flash_t* f, uint32_t addr, void* buf, uint32_t n) {
    mcs_rp2_flash_t* d = (mcs_rp2_flash_t*)f->ctx;
    if ((uint64_t)addr + n > d->size) return MCS_FLASH_EINVAL;
    memcpy(buf, (const void*)(XIP_BASE + d->offset + addr), n);
    return 0;
}
/* any size and alignment: whole 256-byte pages, untouched bytes stay 0xFF */
static int rp2_fl_prog(mcs_flash_t* f, uint32_t addr, const void* buf, uint32_t n) {
    mcs_rp2_flash_t* d = (mcs_rp2_flash_t*)f->ctx;
    if ((uint64_t)addr + n > d->size) return MCS_FLASH_EINVAL;
    static uint8_t page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));
    const uint8_t* src = (const uint8_t*)buf;
    while (n) {
        uint32_t pg = addr & ~(uint32_t)(FLASH_PAGE_SIZE - 1), o = addr - pg;
        uint32_t k = FLASH_PAGE_SIZE - o < n ? FLASH_PAGE_SIZE - o : n;
        memset(page, 0xFF, sizeof page);
        memcpy(page + o, src, k);
        rp2_flash_op_t op = { d->offset + pg, page, FLASH_PAGE_SIZE, 0 };
        rp2_flash_run(&op);
        if (memcmp((const void*)(XIP_BASE + d->offset + addr), src, k) != 0) return MCS_FLASH_EPROG;
        addr += k; src += k; n -= k;
    }
    return 0;
}
static int rp2_fl_erase(mcs_flash_t* f, uint32_t block) {
    mcs_rp2_flash_t* d = (mcs_rp2_flash_t*)f->ctx;
    if (block >= f->block_count) return MCS_FLASH_EINVAL;
    rp2_flash_op_t op = { d->offset + block * FLASH_SECTOR_SIZE, NULL, FLASH_SECTOR_SIZE, 1 };
    return rp2_flash_run(&op);
}
int mcs_rp2_flash_init(mcs_rp2_flash_t* d, uint32_t offset, uint32_t size) {
    if (!offset && !size) {
        size = MCS_RP2_FS_SIZE; offset = PICO_FLASH_SIZE_BYTES - size;
#if MCS_RP2_BLE
        size -= MCS_RP2_BLE_FLASH;                 /* BTstack's bond store: the last 8 KB */
#endif
    }
    if (!size || offset % FLASH_SECTOR_SIZE || size % FLASH_SECTOR_SIZE || (uint64_t)offset + size > PICO_FLASH_SIZE_BYTES)
        return MCS_FLASH_EINVAL;
    uint32_t fw_end = (uint32_t)((uintptr_t)&__flash_binary_end - XIP_BASE);
    if (fw_end > offset) return MCS_FLASH_EINVAL;          /* would erase the firmware */
    memset(d, 0, sizeof *d);
    d->offset = offset; d->size = size;
    d->flash.type = MCS_FLASH_NOR;
    d->flash.page_size = FLASH_PAGE_SIZE;
    d->flash.block_size = FLASH_SECTOR_SIZE;
    d->flash.block_count = size / FLASH_SECTOR_SIZE;
    d->flash.read = rp2_fl_read;
    d->flash.prog = rp2_fl_prog;
    d->flash.erase = rp2_fl_erase;
    d->flash.ctx = d;
    return 0;
}
#endif
