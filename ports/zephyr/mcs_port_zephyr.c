/* MicroCS port for Zephyr RTOS - portable driver APIs only. See mcs_port_zephyr.h. */
#include "mcs_port_zephyr.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#if defined(CONFIG_I2C)
#include <zephyr/drivers/i2c.h>
#endif
#if defined(CONFIG_SPI)
#include <zephyr/drivers/spi.h>
#endif
#if defined(CONFIG_ADC)
#include <zephyr/drivers/adc.h>
#endif
#if defined(CONFIG_PWM)
#include <zephyr/drivers/pwm.h>
#endif
#if defined(CONFIG_DAC)
#include <zephyr/drivers/dac.h>
#endif
#if defined(CONFIG_CAN)
#include <zephyr/drivers/can.h>
#endif
#if defined(CONFIG_WATCHDOG)
#include <zephyr/drivers/watchdog.h>
#endif
#if defined(CONFIG_HWINFO)
#include <zephyr/drivers/hwinfo.h>
#endif
#if defined(CONFIG_REBOOT)
#include <zephyr/sys/reboot.h>
#endif
#if defined(CONFIG_I2S)
#include <zephyr/drivers/i2s.h>
#endif
#if defined(CONFIG_RTC)
#include <zephyr/drivers/rtc.h>
#endif
#if defined(CONFIG_FILE_SYSTEM) && MCS_ENABLE_FS
#include <zephyr/fs/fs.h>
#include <stdio.h>
#if defined(CONFIG_FILE_SYSTEM_LITTLEFS)
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#endif
#endif

#define DEV_OR_NULL(node) COND_CODE_1(DT_NODE_HAS_STATUS(node, okay), (DEVICE_DT_GET(node)), (NULL))
#define LBL(l) DEV_OR_NULL(DT_NODELABEL(l))
#define ALIAS_OR(a, fallback) COND_CODE_1(DT_NODE_EXISTS(DT_ALIAS(a)), (DEV_OR_NULL(DT_ALIAS(a))), (fallback))

static mcs_zephyr_cfg_t g_cfg;

static int z_rc(int e) {
    if (e >= 0) return e;
    switch (e) {
    case -ENOTSUP: case -ENOSYS: return MCS_HAL_ENOTSUP;
    case -ETIMEDOUT: case -EAGAIN: return MCS_HAL_ETIMEOUT;
    case -EBUSY: return MCS_HAL_EBUSY;
    case -EINVAL: return MCS_HAL_EINVAL;
    case -ENODEV: case -ENXIO: return MCS_HAL_ENODEV;
    default: return MCS_HAL_ERR;
    }
}

/* ------------------------------------------------------------------ GPIO */
#if DT_NODE_EXISTS(DT_NODELABEL(gpioa))
#define LETTER_PORTS 1
static const struct device* const g_ports[] = { LBL(gpioa), LBL(gpiob), LBL(gpioc), LBL(gpiod), LBL(gpioe), LBL(gpiof),
                                                LBL(gpiog), LBL(gpioh), LBL(gpioi), LBL(gpioj), LBL(gpiok) };
#else
#define LETTER_PORTS 0
static const struct device* const g_ports[] = { LBL(gpio0), LBL(gpio1), LBL(gpio2), LBL(gpio3), LBL(gpio4), LBL(gpio5),
                                                LBL(gpio6), LBL(gpio7) };
#endif
#define NPORTS ARRAY_SIZE(g_ports)
static const struct device* port_of(int pin) {
    if (pin < 0 || (unsigned)(pin >> 5) >= NPORTS) return NULL;
    return g_ports[pin >> 5];
}
static int z_pin_lookup(void* ctx, const char* s) {
    (void)ctx;
    /* "PA5" -> port A, "P0.13" -> port 0 (32 pins per port); else generic numbers */
    if ((s[0] == 'P' || s[0] == 'p') && ((s[1] >= 'A' && s[1] <= 'K') || (s[1] >= 'a' && s[1] <= 'k')) && s[2]) {
        int port = (s[1] | 0x20) - 'a', n = 0;
        for (const char* q = s + 2; *q; q++) { if (*q < '0' || *q > '9') return -1; n = n * 10 + (*q - '0'); }
        return n < 32 ? port * 32 + n : -1;
    }
    if ((s[0] == 'P' || s[0] == 'p') && s[1] >= '0' && s[1] <= '9' && s[2] == '.') {
        int n = 0;
        for (const char* q = s + 3; *q; q++) { if (*q < '0' || *q > '9') return -1; n = n * 10 + (*q - '0'); }
        return n < 32 ? (s[1] - '0') * 32 + n : -1;
    }
    return mcs_hal_parse_pin(s);
}
static int z_gpio_mode(void* ctx, int pin, int mode) {
    (void)ctx;
    const struct device* d = port_of(pin);
    if (!d) return MCS_HAL_EINVAL;
    gpio_flags_t f;
    switch (mode) {
    case MCS_GPIO_INPUT: f = GPIO_INPUT; break;
    case MCS_GPIO_INPUT_PULLUP: f = GPIO_INPUT | GPIO_PULL_UP; break;
    case MCS_GPIO_INPUT_PULLDOWN: f = GPIO_INPUT | GPIO_PULL_DOWN; break;
    case MCS_GPIO_OUTPUT: f = GPIO_OUTPUT_LOW | GPIO_INPUT; break;   /* readable output where supported */
    case MCS_GPIO_OPEN_DRAIN: f = GPIO_OUTPUT_HIGH | GPIO_OPEN_DRAIN | GPIO_PULL_UP | GPIO_INPUT; break;
    case MCS_GPIO_ANALOG: f = GPIO_DISCONNECTED; break;
    default: return MCS_HAL_EINVAL;
    }
    int r = gpio_pin_configure(d, (gpio_pin_t)(pin & 31), f);
    if (r == -ENOTSUP && (f & GPIO_OUTPUT)) r = gpio_pin_configure(d, (gpio_pin_t)(pin & 31), f & ~GPIO_INPUT);
    return z_rc(r);
}
static int z_gpio_write(void* ctx, int pin, int v) {
    (void)ctx;
    const struct device* d = port_of(pin);
    return d ? z_rc(gpio_pin_set_raw(d, (gpio_pin_t)(pin & 31), v ? 1 : 0)) : MCS_HAL_EINVAL;
}
static int z_gpio_read(void* ctx, int pin) {
    (void)ctx;
    const struct device* d = port_of(pin);
    return d ? z_rc(gpio_pin_get_raw(d, (gpio_pin_t)(pin & 31))) : MCS_HAL_EINVAL;
}
static struct gpio_callback g_gcb[NPORTS];
static gpio_port_pins_t g_gmask[NPORTS];
static void gpio_isr(const struct device* port, struct gpio_callback* cb, gpio_port_pins_t pins) {
    int p = (int)(cb - g_gcb);
    for (int i = 0; i < 32; i++)
        if (pins & BIT(i)) mcs_hal_post(MCS_HAL_EV_GPIO, p * 32 + i, gpio_pin_get_raw(port, (gpio_pin_t)i));
}
static int z_gpio_irq(void* ctx, int pin, int edge) {
    (void)ctx;
    const struct device* d = port_of(pin);
    if (!d) return MCS_HAL_EINVAL;
    int p = pin >> 5, n = pin & 31;
    gpio_flags_t f = edge == MCS_GPIO_EDGE_RISING ? GPIO_INT_EDGE_RISING : edge == MCS_GPIO_EDGE_FALLING ? GPIO_INT_EDGE_FALLING
                   : edge == MCS_GPIO_EDGE_BOTH ? GPIO_INT_EDGE_BOTH : GPIO_INT_DISABLE;
    if (edge) {
        if (!g_gmask[p]) { gpio_init_callback(&g_gcb[p], gpio_isr, 0); gpio_add_callback(d, &g_gcb[p]); }
        g_gmask[p] |= BIT(n);
    } else g_gmask[p] &= ~BIT(n);
    g_gcb[p].pin_mask = g_gmask[p];
    return z_rc(gpio_pin_interrupt_configure(d, (gpio_pin_t)n, f));
}

/* ------------------------------------------------------------------ UART */
#ifndef MCS_ZEPHYR_UART_RXBUF
#define MCS_ZEPHYR_UART_RXBUF 256
#endif
#if DT_HAS_CHOSEN(zephyr_console)
#define CONSOLE_DEV DEV_OR_NULL(DT_CHOSEN(zephyr_console))
#else
#define CONSOLE_DEV NULL
#endif
static const struct device* const g_uart[] = { ALIAS_OR(mcs_uart0, CONSOLE_DEV), ALIAS_OR(mcs_uart1, NULL),
                                               ALIAS_OR(mcs_uart2, NULL), ALIAS_OR(mcs_uart3, NULL) };
typedef struct { uint8_t buf[MCS_ZEPHYR_UART_RXBUF]; volatile uint16_t head, tail; bool on, irq; } rxring_t;
static rxring_t g_rx[ARRAY_SIZE(g_uart)];
static const struct device* uart_of(int port) { return port >= 0 && port < (int)ARRAY_SIZE(g_uart) ? g_uart[port] : NULL; }
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
static void uart_isr(const struct device* dev, void* ud) {
    rxring_t* r = (rxring_t*)ud;
    uint8_t c;
    bool was_empty = r->head == r->tail, got = false;
    while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
        while (uart_fifo_read(dev, &c, 1) == 1) {
            uint16_t next = (uint16_t)((r->head + 1) % MCS_ZEPHYR_UART_RXBUF);
            if (next != r->tail) { r->buf[r->head] = c; r->head = next; got = true; }
        }
    }
    /* UART.OnReceive: one event when data arrives in an empty buffer */
    if (got && was_empty)
        mcs_hal_post(MCS_HAL_EV_UART, (int)(r - g_rx), (int32_t)((r->head + MCS_ZEPHYR_UART_RXBUF - r->tail) % MCS_ZEPHYR_UART_RXBUF));
}
#endif
static int uart_start(int port) {
    const struct device* d = uart_of(port);
    if (!d || !device_is_ready(d)) return MCS_HAL_ENOTSUP;
    if (g_rx[port].on) return 0;
    g_rx[port].head = g_rx[port].tail = 0;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    /* drivers without interrupt support (e.g. some USB / emulated UARTs) fall back to polling */
    g_rx[port].irq = uart_irq_callback_user_data_set(d, uart_isr, &g_rx[port]) == 0;
    if (g_rx[port].irq) uart_irq_rx_enable(d);
#endif
    g_rx[port].on = true;
    return 0;
}
static int z_uart_config(void* ctx, int port, const mcs_uart_cfg_t* c) {
    (void)ctx;
    const struct device* d = uart_of(port);
    if (!d || !device_is_ready(d)) return MCS_HAL_ENOTSUP;
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
    struct uart_config u = {
        .baudrate = c->baud,
        .parity = c->parity == MCS_UART_PARITY_ODD ? UART_CFG_PARITY_ODD : c->parity == MCS_UART_PARITY_EVEN ? UART_CFG_PARITY_EVEN : UART_CFG_PARITY_NONE,
        .stop_bits = c->stop_bits == 2 ? UART_CFG_STOP_BITS_2 : UART_CFG_STOP_BITS_1,
        .data_bits = c->data_bits == 5 ? UART_CFG_DATA_BITS_5 : c->data_bits == 6 ? UART_CFG_DATA_BITS_6
                   : c->data_bits == 7 ? UART_CFG_DATA_BITS_7 : c->data_bits == 9 ? UART_CFG_DATA_BITS_9 : UART_CFG_DATA_BITS_8,
        .flow_ctrl = c->flow_control ? UART_CFG_FLOW_CTRL_RTS_CTS : UART_CFG_FLOW_CTRL_NONE,
    };
    int r = uart_configure(d, &u);
    if (r && r != -ENOSYS) return z_rc(r);
#else
    (void)c;      /* settings come from the devicetree (current-speed) */
#endif
    return uart_start(port);
}
static int z_uart_open(void* ctx, int port, uint32_t baud) {
    mcs_uart_cfg_t c = { baud, 8, MCS_UART_PARITY_NONE, 1, 0 };
    return z_uart_config(ctx, port, &c);
}
static int z_uart_close(void* ctx, int port) {
    (void)ctx;
    const struct device* d = uart_of(port);
    if (!d) return MCS_HAL_ENOTSUP;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    if (g_rx[port].irq) uart_irq_rx_disable(d);
#endif
    g_rx[port].on = g_rx[port].irq = false;
    return 0;
}
static int z_uart_write(void* ctx, int port, const uint8_t* s, size_t n) {
    (void)ctx;
    const struct device* d = uart_of(port);
    if (!d || !device_is_ready(d)) return MCS_HAL_ENOTSUP;
    for (size_t i = 0; i < n; i++) uart_poll_out(d, s[i]);
    return (int)n;
}
static int z_uart_read(void* ctx, int port, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    (void)ctx;
    const struct device* d = uart_of(port);
    int r = uart_start(port);
    if (r) return r;
    int64_t until = k_uptime_get() + timeout_ms;
    size_t k = 0;
    rxring_t* q = &g_rx[port];
    while (k < n) {
        if (q->irq) {
            if (q->tail != q->head) {
                buf[k++] = q->buf[q->tail];
                q->tail = (uint16_t)((q->tail + 1) % MCS_ZEPHYR_UART_RXBUF);
                continue;
            }
        } else {
            unsigned char c;
            if (uart_poll_in(d, &c) == 0) { buf[k++] = c; continue; }
        }
        if (k || k_uptime_get() >= until) break;
        k_msleep(1);
    }
    return (int)k;
}
static int z_uart_available(void* ctx, int port) {
    (void)ctx;
    int r = uart_start(port);
    if (r) return r;
    if (!g_rx[port].irq) return 0;          /* polled driver: unknown until read */
    return (int)((g_rx[port].head + MCS_ZEPHYR_UART_RXBUF - g_rx[port].tail) % MCS_ZEPHYR_UART_RXBUF);
}

/* ------------------------------------------------------------------ I2C */
#if defined(CONFIG_I2C)
static const struct device* const g_i2c[] = { ALIAS_OR(mcs_i2c0, LBL(i2c0)), ALIAS_OR(mcs_i2c1, LBL(i2c1)) };
static const struct device* i2c_of(int bus) {
    const struct device* d = bus >= 0 && bus < (int)ARRAY_SIZE(g_i2c) ? g_i2c[bus] : NULL;
    return d && device_is_ready(d) ? d : NULL;
}
#ifdef I2C_MODE_CONTROLLER
#define MCS_I2C_MODE I2C_MODE_CONTROLLER
#else
#define MCS_I2C_MODE I2C_MODE_MASTER
#endif
static int i2c_rc(int e) { return e == -EIO ? MCS_HAL_ENODEV : z_rc(e); }
static int z_i2c_open(void* ctx, int bus, uint32_t freq) {
    (void)ctx;
    const struct device* d = i2c_of(bus);
    if (!d) return MCS_HAL_ENOTSUP;
    if (!freq) return 0;
    uint32_t sp = freq >= 1000000 ? I2C_SPEED_FAST_PLUS : freq >= 400000 ? I2C_SPEED_FAST : I2C_SPEED_STANDARD;
    int r = i2c_configure(d, MCS_I2C_MODE | I2C_SPEED_SET(sp));
    return r == -ENOSYS ? 0 : z_rc(r);
}
static int z_i2c_write(void* ctx, int bus, int addr, const uint8_t* s, size_t n) {
    (void)ctx; const struct device* d = i2c_of(bus);
    return d ? i2c_rc(i2c_write(d, s, (uint32_t)n, (uint16_t)addr)) : MCS_HAL_ENOTSUP;
}
static int z_i2c_read(void* ctx, int bus, int addr, uint8_t* buf, size_t n) {
    (void)ctx; const struct device* d = i2c_of(bus);
    return d ? i2c_rc(i2c_read(d, buf, (uint32_t)n, (uint16_t)addr)) : MCS_HAL_ENOTSUP;
}
static int z_i2c_write_read(void* ctx, int bus, int addr, const uint8_t* tx, size_t tn, uint8_t* rx, size_t rn) {
    (void)ctx; const struct device* d = i2c_of(bus);
    return d ? i2c_rc(i2c_write_read(d, (uint16_t)addr, tx, tn, rx, rn)) : MCS_HAL_ENOTSUP;
}
static int z_i2c_probe(void* ctx, int bus, int addr) {
    uint8_t b;
    return z_i2c_read(ctx, bus, addr, &b, 1);
}
#endif

/* ------------------------------------------------------------------ SPI */
#if defined(CONFIG_SPI)
static const struct device* const g_spi[] = { ALIAS_OR(mcs_spi0, LBL(spi1)), ALIAS_OR(mcs_spi1, LBL(spi2)) };
static struct spi_config g_spicfg[ARRAY_SIZE(g_spi)];
static const struct device* spi_of(int bus) {
    const struct device* d = bus >= 0 && bus < (int)ARRAY_SIZE(g_spi) ? g_spi[bus] : NULL;
    return d && device_is_ready(d) ? d : NULL;
}
static int z_spi_open(void* ctx, int bus, const mcs_spi_cfg_t* c) {
    (void)ctx;
    if (!spi_of(bus)) return MCS_HAL_ENOTSUP;
    struct spi_config* s = &g_spicfg[bus];
    memset(s, 0, sizeof *s);           /* no hardware CS: SpiDevice drives a GPIO */
    s->frequency = c->freq_hz ? c->freq_hz : 1000000;
    s->operation = (spi_operation_t)(SPI_OP_MODE_MASTER | SPI_WORD_SET(c->bits ? c->bits : 8)
                 | (c->lsb_first ? SPI_TRANSFER_LSB : SPI_TRANSFER_MSB)
                 | ((c->mode & 2) ? SPI_MODE_CPOL : 0) | ((c->mode & 1) ? SPI_MODE_CPHA : 0));
    return 0;
}
static int z_spi_transfer(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n) {
    const struct device* d = spi_of(bus);
    if (!d) return MCS_HAL_ENOTSUP;
    if (!g_spicfg[bus].frequency) { mcs_spi_cfg_t c = { 1000000, 0, 8, 0, 0 }; z_spi_open(ctx, bus, &c); }
    struct spi_buf tb = { .buf = (void*)(uintptr_t)tx, .len = n }, rb = { .buf = rx, .len = n };
    struct spi_buf_set ts = { .buffers = &tb, .count = 1 }, rs = { .buffers = &rb, .count = 1 };
    return z_rc(spi_transceive(d, &g_spicfg[bus], tx ? &ts : NULL, rx ? &rs : NULL));
}
#endif

/* ------------------------------------------------------------------ ADC / PWM / DAC from the devicetree */
#define ZUSER DT_PATH(zephyr_user)
#if defined(CONFIG_ADC) && DT_NODE_HAS_PROP(ZUSER, io_channels)
#define MCS_Z_ADC 1
#define ADC_SPEC(node, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node, idx),
static const struct adc_dt_spec g_adc[] = { DT_FOREACH_PROP_ELEM(ZUSER, io_channels, ADC_SPEC) };
static uint32_t g_adc_ready;
static int adc_sample(int ch, int32_t* raw) {
    if (ch < 0 || ch >= (int)ARRAY_SIZE(g_adc) || !adc_is_ready_dt(&g_adc[ch])) return MCS_HAL_ENOTSUP;
    if (!(g_adc_ready & BIT(ch))) {
        int r = adc_channel_setup_dt(&g_adc[ch]);
        if (r) return z_rc(r);
        g_adc_ready |= BIT(ch);
    }
    int32_t buf = 0;   /* big enough for 16- and 32-bit samples */
    struct adc_sequence seq = { .buffer = &buf, .buffer_size = sizeof buf };
    int r = adc_sequence_init_dt(&g_adc[ch], &seq);
    if (!r) r = adc_read_dt(&g_adc[ch], &seq);
    if (r) return z_rc(r);
    *raw = g_adc[ch].resolution > 16 ? buf : (int32_t)(*(int16_t*)&buf);
    return 0;
}
static int z_adc_read(void* ctx, int ch) {
    (void)ctx; int32_t v; int r = adc_sample(ch, &v);
    return r ? r : (int)v;
}
static int z_adc_read_mv(void* ctx, int ch) {
    (void)ctx; int32_t v; int r = adc_sample(ch, &v);
    if (r) return r;
    r = adc_raw_to_millivolts_dt(&g_adc[ch], &v);
    return r ? z_rc(r) : (int)v;
}
#endif
#if defined(CONFIG_PWM) && DT_NODE_HAS_PROP(ZUSER, pwms)
#define MCS_Z_PWM 1
#define PWM_SPEC(node, prop, idx) PWM_DT_SPEC_GET_BY_IDX(node, idx),
static const struct pwm_dt_spec g_pwm[] = { DT_FOREACH_PROP_ELEM(ZUSER, pwms, PWM_SPEC) };
static int z_pwm_set16(void* ctx, int ch, uint32_t freq, uint16_t duty) {
    (void)ctx;
    if (ch < 0 || ch >= (int)ARRAY_SIZE(g_pwm) || !pwm_is_ready_dt(&g_pwm[ch])) return MCS_HAL_ENOTSUP;
    if (!freq) return MCS_HAL_EINVAL;
    uint32_t period = (uint32_t)(1000000000ull / freq);
    uint32_t pulse = (uint32_t)((uint64_t)period * duty / 65535u);
    return z_rc(pwm_set(g_pwm[ch].dev, g_pwm[ch].channel, period, pulse, g_pwm[ch].flags));
}
static int z_pwm_set(void* ctx, int ch, uint32_t freq, uint16_t permille) {
    return z_pwm_set16(ctx, ch, freq, (uint16_t)((uint32_t)permille * 65535u / 1000u));
}
static int z_pwm_stop(void* ctx, int ch) {
    (void)ctx;
    if (ch < 0 || ch >= (int)ARRAY_SIZE(g_pwm)) return MCS_HAL_ENOTSUP;
    return z_rc(pwm_set(g_pwm[ch].dev, g_pwm[ch].channel, g_pwm[ch].period ? g_pwm[ch].period : 1000000, 0, g_pwm[ch].flags));
}
#endif
#if defined(CONFIG_DAC) && DT_NODE_EXISTS(DT_ALIAS(mcs_dac))
#define MCS_Z_DAC 1
#ifndef MCS_ZEPHYR_DAC_BITS
#define MCS_ZEPHYR_DAC_BITS 12
#endif
static uint8_t g_dac_ready;
static int z_dac_write(void* ctx, int ch, uint32_t v) {
    (void)ctx;
    const struct device* d = DEV_OR_NULL(DT_ALIAS(mcs_dac));
    if (!d || !device_is_ready(d) || ch < 0 || ch > 7) return MCS_HAL_ENOTSUP;
    if (!(g_dac_ready & BIT(ch))) {
        struct dac_channel_cfg c = { .channel_id = (uint8_t)ch, .resolution = MCS_ZEPHYR_DAC_BITS };
        int r = dac_channel_setup(d, &c);
        if (r) return z_rc(r);
        g_dac_ready |= (uint8_t)BIT(ch);
    }
    return z_rc(dac_write_value(d, (uint8_t)ch, v));
}
#endif

/* ------------------------------------------------------------------ timers (k_timer) */
#ifndef MCS_ZEPHYR_TIMERS
#define MCS_ZEPHYR_TIMERS 4
#endif
static struct k_timer g_tim[MCS_ZEPHYR_TIMERS];
static uint32_t g_tcount[MCS_ZEPHYR_TIMERS];
static bool g_tinit[MCS_ZEPHYR_TIMERS];
static void tim_expiry(struct k_timer* t) {
    int i = (int)(t - g_tim);
    mcs_hal_post(MCS_HAL_EV_TIMER, i, (int32_t)++g_tcount[i]);
}
static int z_timer_start(void* ctx, int i, uint32_t period_us, int periodic) {
    (void)ctx;
    if (i < 0 || i >= MCS_ZEPHYR_TIMERS) return MCS_HAL_ENOTSUP;
    if (!period_us) return MCS_HAL_EINVAL;
    if (!g_tinit[i]) { k_timer_init(&g_tim[i], tim_expiry, NULL); g_tinit[i] = true; }
    g_tcount[i] = 0;
    k_timer_start(&g_tim[i], K_USEC(period_us), periodic ? K_USEC(period_us) : K_NO_WAIT);
    return 0;
}
static int z_timer_stop(void* ctx, int i) {
    (void)ctx;
    if (i < 0 || i >= MCS_ZEPHYR_TIMERS) return MCS_HAL_ENOTSUP;
    if (g_tinit[i]) k_timer_stop(&g_tim[i]);
    return 0;
}

/* ------------------------------------------------------------------ CAN */
/* CAN.Open(0) = alias mcs-can0 (fallback: chosen zephyr,canbus), CAN.Open(1) = alias mcs-can1 */
#if DT_NODE_EXISTS(DT_ALIAS(mcs_can0))
#define CAN0_NODE DT_ALIAS(mcs_can0)
#elif DT_HAS_CHOSEN(zephyr_canbus)
#define CAN0_NODE DT_CHOSEN(zephyr_canbus)
#endif
#if DT_NODE_EXISTS(DT_ALIAS(mcs_can1))
#define CAN1_NODE DT_ALIAS(mcs_can1)
#endif
#if defined(CONFIG_CAN) && (defined(CAN0_NODE) || defined(CAN1_NODE))
#define MCS_Z_CAN 1
#ifdef CAN0_NODE
#define CAN0_DEV DEV_OR_NULL(CAN0_NODE)
#define CAN0_LB DT_NODE_HAS_COMPAT(CAN0_NODE, zephyr_can_loopback)
#else
#define CAN0_DEV NULL
#define CAN0_LB 0
#endif
#ifdef CAN1_NODE
#define CAN1_DEV DEV_OR_NULL(CAN1_NODE)
#define CAN1_LB DT_NODE_HAS_COMPAT(CAN1_NODE, zephyr_can_loopback)
#else
#define CAN1_DEV NULL
#define CAN1_LB 0
#endif
static const struct device* const g_can[] = { CAN0_DEV, CAN1_DEV };
static const bool g_can_lb[] = { CAN0_LB, CAN1_LB };   /* the software loopback controller only works in loopback mode */
CAN_MSGQ_DEFINE(g_can_q0, 16);
CAN_MSGQ_DEFINE(g_can_q1, 16);
static struct k_msgq* const g_can_q[] = { &g_can_q0, &g_can_q1 };
static void can_rx_cb(const struct device* dev, struct can_frame* f, void* ud) {   /* ISR context */
    (void)dev;
    int bus = (int)(intptr_t)ud;
    if (k_msgq_put(g_can_q[bus], f, K_NO_WAIT) == 0)
        mcs_hal_post(MCS_HAL_EV_CAN, bus, (int32_t)k_msgq_num_used_get(g_can_q[bus]));   /* CAN.OnReceive */
}
static bool g_can_on[2], g_can_filt[2];
static const struct device* can_of(int bus) { return bus >= 0 && bus < 2 && g_can[bus] && device_is_ready(g_can[bus]) ? g_can[bus] : NULL; }
static int z_can_open(void* ctx, int bus, uint32_t bitrate) {
    (void)ctx;
    const struct device* d = can_of(bus);
    if (!d) return MCS_HAL_ENOTSUP;
    if (g_can_on[bus]) { can_stop(d); g_can_on[bus] = false; }
    int r = bitrate ? can_set_bitrate(d, bitrate) : 0;
    if (r) return z_rc(r);
    if (g_can_lb[bus] && (r = can_set_mode(d, CAN_MODE_LOOPBACK)) != 0) return z_rc(r);
    if (!g_can_filt[bus]) {
        struct can_filter f = { .id = 0, .mask = 0, .flags = 0 };
#ifdef CAN_FILTER_DATA
        f.flags = CAN_FILTER_DATA | CAN_FILTER_RTR;
#endif
        if ((r = can_add_rx_filter(d, can_rx_cb, (void*)(intptr_t)bus, &f)) < 0) return z_rc(r);
        f.flags |= CAN_FILTER_IDE;
        if ((r = can_add_rx_filter(d, can_rx_cb, (void*)(intptr_t)bus, &f)) < 0) return z_rc(r);
        g_can_filt[bus] = true;
    }
    r = can_start(d);
    g_can_on[bus] = r == 0 || r == -EALREADY;
    return g_can_on[bus] ? 0 : z_rc(r);
}
static int z_can_send(void* ctx, int bus, const mcs_can_frame_t* fr, uint32_t timeout_ms) {
    (void)ctx;
    const struct device* d = can_of(bus);
    if (!d || !g_can_on[bus]) return MCS_HAL_ENOTSUP;
    struct can_frame f;
    memset(&f, 0, sizeof f);
    f.id = fr->id;
    f.dlc = fr->len > 8 ? 8 : fr->len;
    f.flags = (fr->extended ? CAN_FRAME_IDE : 0) | (fr->rtr ? CAN_FRAME_RTR : 0);
    memcpy(f.data, fr->data, f.dlc);
    return z_rc(can_send(d, &f, K_MSEC(timeout_ms), NULL, NULL));
}
static int z_can_recv(void* ctx, int bus, mcs_can_frame_t* fr, uint32_t timeout_ms) {
    (void)ctx;
    if (!can_of(bus) || !g_can_on[bus]) return MCS_HAL_ENOTSUP;
    struct can_frame f;
    if (k_msgq_get(g_can_q[bus], &f, K_MSEC(timeout_ms))) return MCS_HAL_ETIMEOUT;
    fr->id = f.id;
    fr->extended = (f.flags & CAN_FRAME_IDE) != 0;
    fr->rtr = (f.flags & CAN_FRAME_RTR) != 0;
    fr->len = f.dlc > 8 ? 8 : f.dlc;
    memcpy(fr->data, f.data, fr->len);
    return 0;
}
#endif

/* ------------------------------------------------------------------ I2S */
/* I2S.Open(0) = alias mcs-i2s0 (fallback: node label i2s0), I2S.Open(1) = alias mcs-i2s1.
 * Audio moves through a memory slab of MCS_ZEPHYR_I2S_BLOCKS blocks; the stream
 * starts once the first TX block is queued (or on the first read) and drains on Close. */
#if defined(CONFIG_I2S) && (DT_NODE_EXISTS(DT_ALIAS(mcs_i2s0)) || DT_NODE_EXISTS(DT_NODELABEL(i2s0)) || DT_NODE_EXISTS(DT_ALIAS(mcs_i2s1)))
#define MCS_Z_I2S 1
#ifndef MCS_ZEPHYR_I2S_BLOCK
#define MCS_ZEPHYR_I2S_BLOCK 1024      /* bytes per DMA block (a multiple of the frame size) */
#endif
#ifndef MCS_ZEPHYR_I2S_BLOCKS
#define MCS_ZEPHYR_I2S_BLOCKS 4
#endif
static const struct device* const g_i2s[] = { ALIAS_OR(mcs_i2s0, LBL(i2s0)), ALIAS_OR(mcs_i2s1, NULL) };
K_MEM_SLAB_DEFINE_STATIC(g_i2s_slab0, MCS_ZEPHYR_I2S_BLOCK, MCS_ZEPHYR_I2S_BLOCKS, 4);
K_MEM_SLAB_DEFINE_STATIC(g_i2s_slab1, MCS_ZEPHYR_I2S_BLOCK, MCS_ZEPHYR_I2S_BLOCKS, 4);
static struct k_mem_slab* const g_i2s_slab[] = { &g_i2s_slab0, &g_i2s_slab1 };
static struct {
    uint8_t dir;                  /* MCS_I2S_TX / RX / DUPLEX, 0 = closed */
    bool tx_on, rx_on;
    uint16_t frame;               /* bytes per frame */
    uint8_t txb[MCS_ZEPHYR_I2S_BLOCK]; size_t txn;     /* partial TX block */
    uint8_t rxb[MCS_ZEPHYR_I2S_BLOCK]; size_t rxn, rxp; /* unread part of the last RX block */
} g_i2st[2];
static int i2s_push(const struct device* d, int bus, uint32_t timeout_ms);
static const struct device* i2s_of(int bus) { return bus >= 0 && bus < 2 && g_i2s[bus] && device_is_ready(g_i2s[bus]) ? g_i2s[bus] : NULL; }
static int z_i2s_close(void* ctx, int bus) {
    (void)ctx;
    const struct device* d = i2s_of(bus);
    if (!d) return MCS_HAL_ENOTSUP;
    if (g_i2st[bus].txn) {                         /* pad and send the last partial block */
        memset(g_i2st[bus].txb + g_i2st[bus].txn, 0, MCS_ZEPHYR_I2S_BLOCK - g_i2st[bus].txn);
        g_i2st[bus].txn = MCS_ZEPHYR_I2S_BLOCK;
        i2s_push(d, bus, 1000);
    }
    if (g_i2st[bus].tx_on) i2s_trigger(d, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
    if (g_i2st[bus].rx_on) i2s_trigger(d, I2S_DIR_RX, I2S_TRIGGER_STOP);
    if (g_i2st[bus].rx_on) i2s_trigger(d, I2S_DIR_RX, I2S_TRIGGER_DROP);
    g_i2st[bus].dir = 0; g_i2st[bus].tx_on = g_i2st[bus].rx_on = false;
    g_i2st[bus].txn = g_i2st[bus].rxn = g_i2st[bus].rxp = 0;
    return 0;
}
static int z_i2s_open(void* ctx, int bus, const mcs_i2s_cfg_t* c) {
    const struct device* d = i2s_of(bus);
    if (!d) return MCS_HAL_ENOTSUP;
    if (g_i2st[bus].dir) z_i2s_close(ctx, bus);
    struct i2s_config ic;
    memset(&ic, 0, sizeof ic);
    ic.word_size = c->bits ? c->bits : 16;
    ic.channels = c->channels ? c->channels : 2;
    ic.format = c->format == MCS_I2S_MSB ? I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED
              : c->format == MCS_I2S_PCM ? I2S_FMT_DATA_FORMAT_PCM_SHORT : I2S_FMT_DATA_FORMAT_I2S;
    ic.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER;
    ic.frame_clk_freq = c->sample_rate;
    ic.mem_slab = g_i2s_slab[bus];
    ic.block_size = MCS_ZEPHYR_I2S_BLOCK;
    ic.timeout = 1000;
    uint8_t dir = c->direction ? c->direction : MCS_I2S_TX;
    int r;
    if (dir == MCS_I2S_DUPLEX) {
        r = i2s_configure(d, I2S_DIR_BOTH, &ic);
        if (r == -ENOSYS || r == -ENOTSUP) {           /* drivers that only configure one direction at a time */
            r = i2s_configure(d, I2S_DIR_TX, &ic);
            if (!r) r = i2s_configure(d, I2S_DIR_RX, &ic);
        }
    } else r = i2s_configure(d, dir == MCS_I2S_RX ? I2S_DIR_RX : I2S_DIR_TX, &ic);
    if (r) return z_rc(r);
    g_i2st[bus].dir = dir;
    g_i2st[bus].frame = (uint16_t)(((ic.word_size + 7) / 8) * ic.channels);
    return 0;
}
static int i2s_push(const struct device* d, int bus, uint32_t timeout_ms) {
    (void)timeout_ms;
    int r = i2s_buf_write(d, g_i2st[bus].txb, g_i2st[bus].txn);
    if (r) return z_rc(r);
    g_i2st[bus].txn = 0;
    if (!g_i2st[bus].tx_on) {
        r = i2s_trigger(d, I2S_DIR_TX, I2S_TRIGGER_START);
        if (r) return z_rc(r);
        g_i2st[bus].tx_on = true;
    }
    return 0;
}
static int z_i2s_write(void* ctx, int bus, const uint8_t* data, size_t n, uint32_t timeout_ms) {
    (void)ctx;
    const struct device* d = i2s_of(bus);
    if (!d || !(g_i2st[bus].dir & MCS_I2S_TX)) return MCS_HAL_ENOTSUP;
    size_t done = 0;
    while (done < n) {
        size_t k = MIN(n - done, (size_t)MCS_ZEPHYR_I2S_BLOCK - g_i2st[bus].txn);
        memcpy(g_i2st[bus].txb + g_i2st[bus].txn, data + done, k);
        g_i2st[bus].txn += k; done += k;
        if (g_i2st[bus].txn == MCS_ZEPHYR_I2S_BLOCK) {
            int r = i2s_push(d, bus, timeout_ms);
            if (r) return done > k ? (int)(done - k) : r;
        }
    }
    return (int)done;
}
static int z_i2s_read(void* ctx, int bus, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    (void)ctx; (void)timeout_ms;
    const struct device* d = i2s_of(bus);
    if (!d || !(g_i2st[bus].dir & MCS_I2S_RX)) return MCS_HAL_ENOTSUP;
    if (!g_i2st[bus].rx_on) {
        int r = i2s_trigger(d, I2S_DIR_RX, I2S_TRIGGER_START);
        if (r) return z_rc(r);
        g_i2st[bus].rx_on = true;
    }
    size_t done = 0;
    while (done < n) {
        if (g_i2st[bus].rxp >= g_i2st[bus].rxn) {
            size_t sz = sizeof g_i2st[bus].rxb;
            int r = i2s_buf_read(d, g_i2st[bus].rxb, &sz);
            if (r) return done ? (int)done : z_rc(r);
            g_i2st[bus].rxn = sz; g_i2st[bus].rxp = 0;
        }
        size_t k = MIN(n - done, g_i2st[bus].rxn - g_i2st[bus].rxp);
        memcpy(buf + done, g_i2st[bus].rxb + g_i2st[bus].rxp, k);
        g_i2st[bus].rxp += k; done += k;
    }
    return (int)done;
}
#endif

/* ------------------------------------------------------------------ watchdog / clock / system */
/* Watchdog: alias watchdog0 (fallback: node labels wdt0, wdt, iwdg) */
#if DT_NODE_EXISTS(DT_ALIAS(watchdog0))
#define WDT_NODE DT_ALIAS(watchdog0)
#elif DT_NODE_EXISTS(DT_NODELABEL(wdt0))
#define WDT_NODE DT_NODELABEL(wdt0)
#elif DT_NODE_EXISTS(DT_NODELABEL(wdt))
#define WDT_NODE DT_NODELABEL(wdt)
#elif DT_NODE_EXISTS(DT_NODELABEL(iwdg))
#define WDT_NODE DT_NODELABEL(iwdg)
#endif
#if defined(CONFIG_WATCHDOG) && defined(WDT_NODE)
#define MCS_Z_WDT 1
static int g_wdt_ch = -1;
static int z_wdt_start(void* ctx, uint32_t ms) {
    (void)ctx;
    const struct device* d = DEV_OR_NULL(WDT_NODE);
    if (!d || !device_is_ready(d)) return MCS_HAL_ENOTSUP;
    if (g_wdt_ch >= 0) return MCS_HAL_EBUSY;     /* most watchdogs cannot be changed once running */
    struct wdt_timeout_cfg c = { .window = { .min = 0, .max = ms }, .callback = NULL, .flags = WDT_FLAG_RESET_SOC };
    int ch = wdt_install_timeout(d, &c);
    if (ch < 0) return z_rc(ch);
    int r = wdt_setup(d, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (r) return z_rc(r);
    g_wdt_ch = ch;
    return 0;
}
static int z_wdt_feed(void* ctx) {
    (void)ctx;
    if (g_wdt_ch < 0) return MCS_HAL_ERR;
    return z_rc(wdt_feed(DEV_OR_NULL(WDT_NODE), g_wdt_ch));
}
#endif
/* RTC: a real clock (alias rtc, CONFIG_RTC=y) when the board has one, else a software clock from uptime */
static int64_t g_rtc_offset;
#if defined(CONFIG_RTC) && DT_NODE_EXISTS(DT_ALIAS(rtc))
#include <time.h>
static int64_t days_from_civil(int y, unsigned m, unsigned d) {   /* H. Hinnant's algorithm */
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}
static int z_rtc_get(void* ctx, uint32_t* s) {
    (void)ctx;
    const struct device* d = DEV_OR_NULL(DT_ALIAS(rtc));
    struct rtc_time t;
    if (d && device_is_ready(d) && rtc_get_time(d, &t) == 0) {
        *s = (uint32_t)(days_from_civil(t.tm_year + 1900, (unsigned)t.tm_mon + 1, (unsigned)t.tm_mday) * 86400
                        + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);
        return 0;
    }
    *s = (uint32_t)(g_rtc_offset + k_uptime_get() / 1000);
    return 0;
}
static int z_rtc_set(void* ctx, uint32_t s) {
    (void)ctx;
    g_rtc_offset = (int64_t)s - k_uptime_get() / 1000;
    const struct device* d = DEV_OR_NULL(DT_ALIAS(rtc));
    if (!d || !device_is_ready(d)) return 0;
    time_t tt = (time_t)s;
    struct tm tm;
    gmtime_r(&tt, &tm);
    struct rtc_time t;
    memset(&t, 0, sizeof t);
    t.tm_sec = tm.tm_sec; t.tm_min = tm.tm_min; t.tm_hour = tm.tm_hour; t.tm_mday = tm.tm_mday;
    t.tm_mon = tm.tm_mon; t.tm_year = tm.tm_year; t.tm_wday = tm.tm_wday; t.tm_yday = tm.tm_yday; t.tm_isdst = -1;
    return z_rc(rtc_set_time(d, &t));
}
#else
static int z_rtc_get(void* ctx, uint32_t* s) { (void)ctx; *s = (uint32_t)(g_rtc_offset + k_uptime_get() / 1000); return 0; }
static int z_rtc_set(void* ctx, uint32_t s) { (void)ctx; g_rtc_offset = (int64_t)s - k_uptime_get() / 1000; return 0; }
#endif
static uint32_t z_micros(void* ctx) {
    (void)ctx;
#ifdef CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER
    return (uint32_t)k_cyc_to_us_floor64(k_cycle_get_64());
#else
    return (uint32_t)k_ticks_to_us_floor64(k_uptime_ticks());
#endif
}
static void z_delay_us(void* ctx, uint32_t us) { (void)ctx; k_busy_wait(us); }
static int z_reset(void* ctx) {
    (void)ctx;
#if defined(CONFIG_REBOOT)
    sys_reboot(SYS_REBOOT_COLD);
#endif
    return MCS_HAL_ENOTSUP;
}
static int z_unique_id(void* ctx, uint8_t* buf, size_t cap) {
    (void)ctx;
#if defined(CONFIG_HWINFO)
    ssize_t n = hwinfo_get_device_id(buf, cap);
    return n < 0 ? z_rc((int)n) : (int)n;
#else
    (void)buf; (void)cap;
    return MCS_HAL_ENOTSUP;
#endif
}

/* ------------------------------------------------------------------ filesystem */
#if defined(CONFIG_FILE_SYSTEM) && MCS_ENABLE_FS
#ifndef MCS_ZEPHYR_FS_FILES
#define MCS_ZEPHYR_FS_FILES 4               /* files C# may keep open at the same time */
#endif
static struct { struct fs_file_t f; bool used; } g_files[MCS_ZEPHYR_FS_FILES];

static int fs_err(int e) {
    switch (e) {
    case -ENOENT: return MCS_VFS_ENOENT;
    case -EEXIST: return MCS_VFS_EEXIST;
    case -ENOTDIR: return MCS_VFS_ENOTDIR;
    case -EISDIR: return MCS_VFS_EISDIR;
    case -ENOSPC: return MCS_VFS_ENOSPC;
    case -EACCES: case -EPERM: case -EROFS: return MCS_VFS_EACCES;
    case -ENOTEMPTY: return MCS_VFS_ENOTEMPTY;
    case -ENOMEM: return MCS_VFS_ENOMEM;
    case -ENAMETOOLONG: return MCS_VFS_ENAMETOOLONG;
    case -EINVAL: return MCS_VFS_EINVAL;
    default: return e < 0 ? MCS_VFS_EIO : MCS_VFS_OK;
    }
}
static int zpath(void* ctx, const char* path, char* out, size_t cap) {
    const mcs_zephyr_fs_t* fs = (const mcs_zephyr_fs_t*)ctx;
    int n = snprintf(out, cap, "%s%s", fs->root, strcmp(path, "/") ? path : "");
    return n < 0 || (size_t)n >= cap ? MCS_VFS_ENAMETOOLONG : MCS_VFS_OK;
}
#define ZP(p) char zp[MCS_VFS_PATH_MAX + 40]; { int e_ = zpath(ctx, p, zp, sizeof zp); if (e_) return e_; }

static int zf_open(void* ctx, const char* path, int flags, void** fh) {
    ZP(path);
    struct fs_dirent st;
    if (fs_stat(zp, &st) == 0 && st.type == FS_DIR_ENTRY_DIR) return MCS_VFS_EISDIR;
    int slot = -1;
    for (int i = 0; i < MCS_ZEPHYR_FS_FILES; i++) if (!g_files[i].used) { slot = i; break; }
    if (slot < 0) return MCS_VFS_ENOMEM;
    struct fs_file_t* f = &g_files[slot].f;
    fs_file_t_init(f);
    fs_mode_t m = (flags & (MCS_VFS_WRITE | MCS_VFS_APPEND)) ? (FS_O_WRITE | FS_O_CREATE | ((flags & MCS_VFS_APPEND) ? FS_O_APPEND : 0)) : FS_O_READ;
    int r = fs_open(f, zp, m);
    if (r) return fs_err(r);
    if ((flags & MCS_VFS_WRITE) && !(flags & MCS_VFS_APPEND) && (r = fs_truncate(f, 0)) != 0) { fs_close(f); return fs_err(r); }
    g_files[slot].used = true;
    *fh = f;
    return MCS_VFS_OK;
}
static int zf_read(void* ctx, void* fh, void* buf, size_t n) {
    (void)ctx;
    ssize_t r = fs_read((struct fs_file_t*)fh, buf, n > 0x7fffffff ? 0x7fffffff : n);
    return r < 0 ? fs_err((int)r) : (int)r;
}
static int zf_write(void* ctx, void* fh, const void* buf, size_t n) {
    (void)ctx;
    ssize_t r = fs_write((struct fs_file_t*)fh, buf, n > 0x7fffffff ? 0x7fffffff : n);
    return r < 0 ? fs_err((int)r) : (int)r;
}
static int zf_close(void* ctx, void* fh) {
    (void)ctx;
    int r = fs_close((struct fs_file_t*)fh);
    for (int i = 0; i < MCS_ZEPHYR_FS_FILES; i++) if (&g_files[i].f == fh) g_files[i].used = false;
    return fs_err(r);
}
static int zf_stat(void* ctx, const char* path, mcs_vfs_stat_t* out) {
    if (!strcmp(path, "/")) { out->is_dir = true; out->size = 0; return MCS_VFS_OK; }   /* mount points have no dirent */
    ZP(path);
    struct fs_dirent st;
    int r = fs_stat(zp, &st);
    if (r) return fs_err(r);
    out->is_dir = st.type == FS_DIR_ENTRY_DIR;
    out->size = out->is_dir ? 0 : (uint32_t)st.size;
    return MCS_VFS_OK;
}
static int zf_remove(void* ctx, const char* path) { ZP(path); return fs_err(fs_unlink(zp)); }
static int zf_mkdir(void* ctx, const char* path) { ZP(path); return fs_err(fs_mkdir(zp)); }
static int zf_rename(void* ctx, const char* from, const char* to) {
    char zp2[MCS_VFS_PATH_MAX + 40];
    if (zpath(ctx, to, zp2, sizeof zp2)) return MCS_VFS_ENAMETOOLONG;
    ZP(from);
    struct fs_dirent st;
    if (fs_stat(zp2, &st) == 0) return MCS_VFS_EEXIST;
    return fs_err(fs_rename(zp, zp2));
}
static int zf_list(void* ctx, const char* path, mcs_vfs_list_cb cb, void* ud) {
    ZP(path);
    struct fs_dir_t d;
    fs_dir_t_init(&d);
    int r = fs_opendir(&d, zp);
    if (r) return fs_err(r);
    struct fs_dirent e;
    while (fs_readdir(&d, &e) == 0 && e.name[0]) {
        if (!strcmp(e.name, ".") || !strcmp(e.name, "..")) continue;
        mcs_vfs_stat_t vs = { e.type == FS_DIR_ENTRY_DIR ? 0 : (uint32_t)e.size, e.type == FS_DIR_ENTRY_DIR };
        if (cb(ud, e.name, &vs)) break;
    }
    fs_closedir(&d);
    return MCS_VFS_OK;
}
static int zf_statfs(void* ctx, mcs_vfs_statfs_t* st) {
    const mcs_zephyr_fs_t* fs = (const mcs_zephyr_fs_t*)ctx;
    struct fs_statvfs v;
    int r = fs_statvfs(fs->root, &v);
    if (r) return fs_err(r);
    st->total = (uint64_t)v.f_blocks * v.f_frsize;
    st->free = (uint64_t)v.f_bfree * v.f_frsize;
    st->format = fs->format ? fs->format : "zephyr";
    return MCS_VFS_OK;
}
const mcs_vfs_ops_t mcs_zephyr_fs_ops = { zf_open, zf_read, zf_write, zf_close, zf_stat, zf_remove, zf_mkdir, zf_rename, zf_list, zf_statfs };

int mcs_zephyr_fs_init(mcs_zephyr_fs_t* fs, const char* mount_point, const char* format) {
    size_t n = strlen(mount_point);
    while (n > 1 && mount_point[n - 1] == '/') n--;
    if (n + 1 >= sizeof fs->root) return MCS_VFS_ENAMETOOLONG;
    memcpy(fs->root, mount_point, n); fs->root[n] = 0;
    fs->format = format;
    struct fs_statvfs v;
    return fs_statvfs(fs->root, &v) ? MCS_VFS_ENOENT : MCS_VFS_OK;
}

#if defined(CONFIG_FILE_SYSTEM_LITTLEFS) && FIXED_PARTITION_EXISTS(storage_partition)
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(g_lfs_cfg);
static struct fs_mount_t g_lfs_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &g_lfs_cfg,
    .storage_dev = (void*)FIXED_PARTITION_ID(storage_partition),
    .mnt_point = MCS_ZEPHYR_FS_MOUNT,
};
#endif
int mcs_zephyr_fs_mount(mcs_zephyr_fs_t* fs) {
#if defined(CONFIG_FILE_SYSTEM_LITTLEFS) && FIXED_PARTITION_EXISTS(storage_partition)
    struct fs_statvfs v;
    if (fs_statvfs(MCS_ZEPHYR_FS_MOUNT, &v) != 0) {       /* not mounted yet (no fstab automount) */
        int r = fs_mount(&g_lfs_mnt);                      /* formats the partition on first use */
        if (r && r != -EBUSY) return fs_err(r);
    }
    return mcs_zephyr_fs_init(fs, MCS_ZEPHYR_FS_MOUNT, "littlefs");
#else
    (void)fs;
    return MCS_VFS_ENOENT;          /* no `storage` partition or CONFIG_FILE_SYSTEM_LITTLEFS=n */
#endif
}
#endif

uint32_t mcs_zephyr_ticks(void* ud) { (void)ud; return k_uptime_get_32(); }
void mcs_zephyr_delay(void* ud, uint32_t ms) { (void)ud; k_msleep((int32_t)ms); }

static int con_read(void* ud, uint8_t* b, size_t n, uint32_t t) { (void)ud; return z_uart_read(NULL, 0, b, n, t); }
static void con_write(void* ud, const char* s, size_t n) { (void)ud; z_uart_write(NULL, 0, (const uint8_t*)s, n); }
mcs_transport_t mcs_zephyr_console(void) {
    uart_start(0);
    mcs_transport_t t = { con_read, con_write, NULL };
    return t;
}

void mcs_zephyr_hal_init(mcs_hal_t* hal, const mcs_zephyr_cfg_t* cfg) {
    if (cfg) g_cfg = *cfg;
    memset(hal, 0, sizeof *hal);
    hal->board = g_cfg.name ? g_cfg.name : CONFIG_BOARD;
    hal->ctx = &g_cfg;
    hal->pin_lookup = z_pin_lookup;
    hal->gpio_mode = z_gpio_mode;
    hal->gpio_write = z_gpio_write;
    hal->gpio_read = z_gpio_read;
    hal->gpio_irq = z_gpio_irq;
    hal->uart_open = z_uart_open;
    hal->uart_config = z_uart_config;
    hal->uart_close = z_uart_close;
    hal->uart_write = z_uart_write;
    hal->uart_read = z_uart_read;
    hal->uart_available = z_uart_available;
#if defined(CONFIG_I2C)
    hal->i2c_open = z_i2c_open;
    hal->i2c_write = z_i2c_write;
    hal->i2c_read = z_i2c_read;
    hal->i2c_write_read = z_i2c_write_read;
    hal->i2c_probe = z_i2c_probe;
#endif
#if defined(CONFIG_SPI)
    hal->spi_open = z_spi_open;
    hal->spi_transfer = z_spi_transfer;
#endif
#ifdef MCS_Z_ADC
    hal->adc_read = z_adc_read;
    hal->adc_read_mv = z_adc_read_mv;
    hal->adc_bits = g_adc[0].resolution ? g_adc[0].resolution : 12;
#endif
#ifdef MCS_Z_PWM
    hal->pwm_set = z_pwm_set;
    hal->pwm_set16 = z_pwm_set16;
    hal->pwm_stop = z_pwm_stop;
#endif
#ifdef MCS_Z_DAC
    hal->dac_write = z_dac_write;
    hal->dac_bits = MCS_ZEPHYR_DAC_BITS;
#endif
    hal->timer_start = z_timer_start;
    hal->timer_stop = z_timer_stop;
#ifdef MCS_Z_CAN
    hal->can_open = z_can_open;
    hal->can_send = z_can_send;
    hal->can_recv = z_can_recv;
#endif
#ifdef MCS_Z_I2S
    hal->i2s_open = z_i2s_open;
    hal->i2s_write = z_i2s_write;
    hal->i2s_read = z_i2s_read;
    hal->i2s_close = z_i2s_close;
#endif
#ifdef MCS_Z_WDT
    hal->wdt_start = z_wdt_start;
    hal->wdt_feed = z_wdt_feed;
#endif
    hal->rtc_get = z_rtc_get;
    hal->rtc_set = z_rtc_set;
    hal->micros = z_micros;
    hal->delay_us = z_delay_us;
    hal->reset = z_reset;
    hal->unique_id = z_unique_id;
    hal->cpu_hz = (uint32_t)sys_clock_hw_cycles_per_sec();
}
