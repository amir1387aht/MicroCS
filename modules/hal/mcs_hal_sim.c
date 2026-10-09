/* MicroCS - simulated board. Deterministic so tests can assert on it. */
#include "mcs_hal.h"
#include "mcs_driver.h"
#if MCS_ENABLE_HAL
#include <string.h>
#include <stdio.h>

#define SIM ((mcs_hal_sim_t*)ctx)
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

static void logf_(mcs_hal_sim_t* s, const char* fmt, int a, int b, int c) {
    if (!s->log) return;
    char line[96];
    int n = snprintf(line, sizeof line, fmt, a, b, c);
    if (n > 0) s->log(s->log_ud, line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
}
static uint32_t now_us(mcs_hal_sim_t* s) { return s->clock_us ? s->clock_us(s->clock_ud) : s->virt_us; }

/* ---------------------------------------------------------------- GPIO */
static int level(mcs_hal_sim_t* s, int pin) {
    switch (s->mode[pin]) {
    case MCS_GPIO_OUTPUT: case MCS_GPIO_OPEN_DRAIN: return s->out[pin];
    case MCS_GPIO_INPUT_PULLUP: return s->inputs[pin] == 2 ? 0 : 1;   /* 2 = driven low */
    default: return s->inputs[pin] == 1;
    }
}
static void edge(mcs_hal_sim_t* s, int pin, int before) {
    int after = level(s, pin);
    if (after == before || !s->irq[pin]) return;
    if ((after && (s->irq[pin] & MCS_GPIO_EDGE_RISING)) || (!after && (s->irq[pin] & MCS_GPIO_EDGE_FALLING)))
        mcs_hal_post(MCS_HAL_EV_GPIO, pin, after);
}
static int g_mode(void* ctx, int pin, int mode) {
    if (pin < 0 || pin >= 64 || mode < 0 || mode > MCS_GPIO_ANALOG) return MCS_HAL_ENOTSUP;
    SIM->mode[pin] = (uint8_t)mode;
    logf_(SIM, "[sim] gpio %d mode %d\n", pin, mode, 0);
    return 0;
}
static int g_write(void* ctx, int pin, int v) {
    if (pin < 0 || pin >= 64) return MCS_HAL_ENOTSUP;
    int before = level(SIM, pin);
    SIM->out[pin] = (uint8_t)(v != 0);
    logf_(SIM, "[sim] gpio %d <- %d\n", pin, v != 0, 0);
    edge(SIM, pin, before);
    return 0;
}
static int g_read(void* ctx, int pin) {
    if (pin < 0 || pin >= 64) return MCS_HAL_ENOTSUP;
    return level(SIM, pin);
}
static int g_irq(void* ctx, int pin, int e) {
    if (pin < 0 || pin >= 64 || e < 0 || e > 3) return MCS_HAL_ENOTSUP;
    SIM->irq[pin] = (uint8_t)e;
    logf_(SIM, "[sim] gpio %d irq %d\n", pin, e, 0);
    return 0;
}
void mcs_hal_sim_set_input(mcs_hal_sim_t* s, int pin, int lv) {
    if (pin < 0 || pin >= 64) return;
    int before = level(s, pin);
    s->inputs[pin] = lv ? 1 : 2;
    edge(s, pin, before);
}
static int g_lookup(void* ctx, const char* name) {
    if (!strcmp(name, "LED")) return 13;
    if (!strcmp(name, "BUTTON")) return 0;
    return -1;
}

/* ---------------------------------------------------------------- UART */
static int u_open(void* ctx, int port, uint32_t baud) {
    if (port < 0 || port >= 4) return MCS_HAL_ENOTSUP;
    SIM->uart_baud[port] = baud;
    return 0;
}
static int u_config(void* ctx, int port, const mcs_uart_cfg_t* c) {
    if (port < 0 || port >= 4 || c->data_bits < 7 || c->data_bits > 9) return MCS_HAL_ENOTSUP;
    SIM->uart_baud[port] = c->baud;
    SIM->uart_cfg[port] = *c;
    return 0;
}
static int u_write(void* ctx, int port, const uint8_t* d, size_t n) {
    if (port < 0 || port >= 4) return MCS_HAL_ENOTSUP;
    size_t i = 0;
    for (; i < n; i++) {
        uint16_t next = (uint16_t)((SIM->uart_head[port] + 1) % 256);
        if (next == SIM->uart_tail[port]) break;   /* RX FIFO full: bytes dropped, like hardware */
        SIM->uart_buf[port][SIM->uart_head[port]] = d[i];
        SIM->uart_head[port] = next;
    }
    logf_(SIM, "[sim] uart%d tx %d bytes\n", port, (int)n, 0);
    if (i) mcs_hal_post(MCS_HAL_EV_UART, port, (int32_t)i);
    return (int)i;
}
static int u_avail(void* ctx, int port) {
    if (port < 0 || port >= 4) return MCS_HAL_ENOTSUP;
    return (SIM->uart_head[port] - SIM->uart_tail[port] + 256) % 256;
}
static int u_read(void* ctx, int port, uint8_t* b, size_t n, uint32_t timeout) {
    int avail = u_avail(ctx, port);
    if (avail < 0) return avail;
    size_t k = 0;
    while (k < n && SIM->uart_tail[port] != SIM->uart_head[port]) {
        b[k++] = SIM->uart_buf[port][SIM->uart_tail[port]];
        SIM->uart_tail[port] = (uint16_t)((SIM->uart_tail[port] + 1) % 256);
    }
    if (k == 0 && n && timeout) return MCS_HAL_ETIMEOUT;
    return (int)k;
}

/* ---------------------------------------------------------------- I2C */
static int i_open(void* ctx, int bus, uint32_t f) { if (bus != 0) return MCS_HAL_ENOTSUP; SIM->i2c_freq = f; return 0; }
static int i_write(void* ctx, int bus, int addr, const uint8_t* d, size_t n) {
    if (bus != 0) return MCS_HAL_ENOTSUP;
    logf_(SIM, "[sim] i2c 0x%02x write %d bytes\n", addr, (int)n, 0);
    if (addr == 0x48) { if (n) SIM->temp_ptr = d[0]; return 0; }
    if (addr == 0x50) {
        if (!n) return 0;
        SIM->eeprom_ptr = d[0];
        for (size_t i = 1; i < n; i++) SIM->eeprom[SIM->eeprom_ptr++] = d[i];
        return 0;
    }
    if (addr == 0x68) {
        if (!n) return 0;
        SIM->reg_ptr = d[0] & 0x7F;
        for (size_t i = 1; i < n; i++) { if (SIM->reg_ptr != 0x75) SIM->regs[SIM->reg_ptr] = d[i]; SIM->reg_ptr = (SIM->reg_ptr + 1) & 0x7F; }
        return 0;
    }
    return MCS_HAL_ENODEV;
}
static int i_read(void* ctx, int bus, int addr, uint8_t* b, size_t n) {
    if (bus != 0) return MCS_HAL_ENOTSUP;
    if (addr == 0x48) {
        /* TMP102 layout: 12-bit two's complement, 0.0625 C/LSB, left aligned */
        int raw = (SIM->temp_centi * 16) / 100;
        uint8_t r[2] = { (uint8_t)(raw >> 4), (uint8_t)((raw & 0xF) << 4) };
        for (size_t i = 0; i < n; i++) b[i] = SIM->temp_ptr == 0 && i < 2 ? r[i] : 0;
        return 0;
    }
    if (addr == 0x50) { for (size_t i = 0; i < n; i++) b[i] = SIM->eeprom[SIM->eeprom_ptr++]; return 0; }
    if (addr == 0x68) { for (size_t i = 0; i < n; i++) { b[i] = SIM->regs[SIM->reg_ptr]; SIM->reg_ptr = (SIM->reg_ptr + 1) & 0x7F; } return 0; }
    return MCS_HAL_ENODEV;
}
static int i_probe(void* ctx, int bus, int addr) {
    if (bus != 0) return MCS_HAL_ENOTSUP;
    return addr == 0x48 || addr == 0x50 || addr == 0x68 ? 0 : MCS_HAL_ENODEV;
}

/* ---------------------------------------------------------------- SPI */
static int s_open(void* ctx, int bus, const mcs_spi_cfg_t* c) {
    if (bus != 0) return MCS_HAL_ENOTSUP;
    SIM->spi_cfg = *c;
    logf_(SIM, "[sim] spi%d %d Hz mode %d\n", bus, (int)c->freq_hz, c->mode);
    return 0;
}
static int s_xfer(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n) {
    if (bus != 0) return MCS_HAL_ENOTSUP;
    memcpy(rx, tx, n);
    logf_(SIM, "[sim] spi%d %d bytes\n", bus, (int)n, 0);
    return 0;
}

/* ---------------------------------------------------------------- ADC / DAC / PWM */
static int a_read(void* ctx, int ch) { return ch >= 0 && ch < 16 ? SIM->adc[ch] : MCS_HAL_ENOTSUP; }
static int d_write(void* ctx, int ch, uint32_t v) {
    if (ch < 0 || ch >= 4) return MCS_HAL_ENOTSUP;
    SIM->dac[ch] = (uint16_t)v;
    logf_(SIM, "[sim] dac%d <- %d\n", ch, (int)v, 0);
    return 0;
}
static int p_set(void* ctx, int ch, uint32_t f, uint16_t d) {
    if (ch < 0 || ch >= 8) return MCS_HAL_ENOTSUP;
    SIM->pwm_freq[ch] = f; SIM->pwm_duty[ch] = d;
    SIM->pwm_duty16[ch] = (uint16_t)((uint32_t)d * 65535u / 1000u);
    logf_(SIM, "[sim] pwm%d %d Hz duty %d/1000\n", ch, (int)f, d);
    return 0;
}
static int p_set16(void* ctx, int ch, uint32_t f, uint16_t d) {
    if (ch < 0 || ch >= 8) return MCS_HAL_ENOTSUP;
    SIM->pwm_freq[ch] = f; SIM->pwm_duty16[ch] = d;
    SIM->pwm_duty[ch] = (uint16_t)(((uint32_t)d * 1000u + 32767u) / 65535u);
    logf_(SIM, "[sim] pwm%d %d Hz duty %d/65535\n", ch, (int)f, d);
    return 0;
}
#if MCS_ENABLE_WS2812
static int l_write(void* ctx, int pin, const uint8_t* d, size_t n, int order) {
    if (pin < 0 || pin >= 64) return MCS_HAL_EINVAL;
    int bpp = order == MCS_LED_GRBW ? 4 : 3;
    int first = n >= 3 ? (order == MCS_LED_RGB ? (d[0] << 16 | d[1] << 8 | d[2]) : (d[1] << 16 | d[0] << 8 | d[2])) : 0;
    logf_(SIM, "[sim] ledstrip gpio %d: %d LEDs, first #%06X\n", pin, (int)n / bpp, first);
    return 0;
}
#endif
static int p_stop(void* ctx, int ch) {
    if (ch < 0 || ch >= 8) return MCS_HAL_ENOTSUP;
    SIM->pwm_duty[ch] = SIM->pwm_duty16[ch] = 0;
    return 0;
}

/* ---------------------------------------------------------------- timers */
static int t_start(void* ctx, int id, uint32_t us, int periodic) {
    if (id < 0 || id >= 4) return MCS_HAL_ENOTSUP;
    SIM->timer_period[id] = us; SIM->timer_periodic[id] = (uint8_t)(periodic != 0);
    SIM->timer_next[id] = now_us(SIM) + us; SIM->timer_on[id] = 1; SIM->timer_fired[id] = 0;
    return 0;
}
static int t_stop(void* ctx, int id) {
    if (id < 0 || id >= 4) return MCS_HAL_ENOTSUP;
    SIM->timer_on[id] = 0;
    return 0;
}
/* timers are evaluated whenever the VM polls for events */
static int sim_poll(void* ctx, mcs_hal_event_t* ev) {
    (void)ev;
    if (!SIM->clock_us) SIM->virt_us += 1000;
    uint32_t t = now_us(SIM);
    for (int i = 0; i < 4; i++) {
        if (!SIM->timer_on[i] || (int32_t)(t - SIM->timer_next[i]) < 0) continue;
        uint32_t n = 1;
        if (SIM->timer_periodic[i]) {
            n += (t - SIM->timer_next[i]) / SIM->timer_period[i];
            SIM->timer_next[i] += n * SIM->timer_period[i];
        } else SIM->timer_on[i] = 0;
        SIM->timer_fired[i] += n;
        mcs_hal_post(MCS_HAL_EV_TIMER, i, (int32_t)SIM->timer_fired[i]);
    }
    return 0;   /* events went through the shared queue */
}
static uint32_t sim_micros(void* ctx) { return now_us(SIM); }
static void sim_delay_us(void* ctx, uint32_t us) { if (!SIM->clock_us) SIM->virt_us += us; else { uint32_t t0 = now_us(SIM); while (now_us(SIM) - t0 < us) {} } }

/* ---------------------------------------------------------------- I2S (loopback) */
static int s2_open(void* ctx, int bus, const mcs_i2s_cfg_t* c) {
    if (bus != 0) return MCS_HAL_ENOTSUP;
    SIM->i2s_cfg = *c; SIM->i2s_len = 0;
    logf_(SIM, "[sim] i2s%d %d Hz %d bit\n", bus, (int)c->sample_rate, c->bits);
    return 0;
}
static int s2_write(void* ctx, int bus, const uint8_t* d, size_t n, uint32_t to) {
    if (bus != 0) return MCS_HAL_ENOTSUP;
    /* the simulated codec "plays" (drops) the oldest samples when the FIFO is full,
     * like a DMA stream that keeps draining; the newest data stays for loopback reads */
    size_t cap = sizeof SIM->i2s_buf, k = n < cap ? n : cap;
    if (SIM->i2s_len + k > cap) {
        size_t drop = SIM->i2s_len + k - cap;
        memmove(SIM->i2s_buf, SIM->i2s_buf + drop, SIM->i2s_len - drop);
        SIM->i2s_len = (uint16_t)(SIM->i2s_len - drop);
    }
    memcpy(SIM->i2s_buf + SIM->i2s_len, d, k);
    SIM->i2s_len = (uint16_t)(SIM->i2s_len + k);
    (void)to;
    return (int)k;
}
static int s2_read(void* ctx, int bus, uint8_t* b, size_t n, uint32_t to) {
    if (bus != 0) return MCS_HAL_ENOTSUP;
    size_t k = n < SIM->i2s_len ? n : SIM->i2s_len;
    memcpy(b, SIM->i2s_buf, k);
    memmove(SIM->i2s_buf, SIM->i2s_buf + k, SIM->i2s_len - k);
    SIM->i2s_len = (uint16_t)(SIM->i2s_len - k);
    return (int)k;
}

/* ---------------------------------------------------------------- QSPI NOR flash */
static int q_open(void* ctx, int bus, uint32_t f) { return bus == 0 ? 0 : MCS_HAL_ENOTSUP; }
static int q_cmd(void* ctx, int bus, const mcs_qspi_cmd_t* c, const uint8_t* tx, uint8_t* rx, size_t n) {
    if (bus != 0) return MCS_HAL_ENOTSUP;
    uint32_t a = c->address % MCS_HAL_SIM_FLASH;
    switch (c->instruction) {
    case 0x9F: { static const uint8_t id[3] = { 0xEF, 0x40, 0x16 }; for (size_t i = 0; i < n; i++) rx[i] = i < 3 ? id[i] : 0; return 0; }
    case 0x05: for (size_t i = 0; i < n; i++) rx[i] = (uint8_t)(SIM->flash_wel << 1); return 0;
    case 0x06: SIM->flash_wel = 1; return 0;
    case 0x04: SIM->flash_wel = 0; return 0;
    case 0x03: case 0x0B: case 0x3B: case 0x6B: case 0xBB: case 0xEB:
        if (!rx) return MCS_HAL_EINVAL;
        for (size_t i = 0; i < n; i++) rx[i] = SIM->flash[(a + i) % MCS_HAL_SIM_FLASH];
        return 0;
    case 0x02: case 0x32:
        if (!SIM->flash_wel || !tx) return MCS_HAL_ERR;
        for (size_t i = 0; i < n && i < 256; i++) SIM->flash[(a & ~0xFFu) + ((a + i) & 0xFF)] &= tx[i];   /* page wrap, NOR AND */
        SIM->flash_wel = 0; return 0;
    case 0x20:
        if (!SIM->flash_wel) return MCS_HAL_ERR;
        memset(SIM->flash + (a & ~0xFFFu) % MCS_HAL_SIM_FLASH, 0xFF, MCS_HAL_SIM_FLASH < 4096 ? MCS_HAL_SIM_FLASH : 4096);
        SIM->flash_wel = 0; return 0;
    default: return MCS_HAL_ENOTSUP;
    }
}

/* ---------------------------------------------------------------- CAN (loopback) */
static int c_open(void* ctx, int bus, uint32_t rate) { return bus == 0 ? 0 : MCS_HAL_ENOTSUP; }
static int c_send(void* ctx, int bus, const mcs_can_frame_t* f, uint32_t to) {
    if (bus != 0) return MCS_HAL_ENOTSUP;
    if (SIM->can_count >= 8) return MCS_HAL_EBUSY;
    SIM->can_q[(SIM->can_head + SIM->can_count) % 8] = *f;
    SIM->can_count++;
    mcs_hal_post(MCS_HAL_EV_CAN, bus, SIM->can_count);
    return 0;
}
static int c_recv(void* ctx, int bus, mcs_can_frame_t* f, uint32_t to) {
    if (bus != 0) return MCS_HAL_ENOTSUP;
    if (!SIM->can_count) return MCS_HAL_ETIMEOUT;
    *f = SIM->can_q[SIM->can_head];
    SIM->can_head = (uint8_t)((SIM->can_head + 1) % 8); SIM->can_count--;
    return 0;
}

/* ---------------------------------------------------------------- system */
static int w_start(void* ctx, uint32_t ms) { SIM->wdt_timeout = ms; return 0; }
static int w_feed(void* ctx) { SIM->wdt_feeds++; return 0; }
static int r_get(void* ctx, uint32_t* t) { *t = SIM->rtc_base + now_us(SIM) / 1000000u; return 0; }
static int r_set(void* ctx, uint32_t t) { SIM->rtc_base = t - now_us(SIM) / 1000000u; return 0; }
static int sys_reset(void* ctx) { SIM->reset_requested = 1; logf_(SIM, "[sim] reset\n", 0, 0, 0); return 0; }
static int sys_uid(void* ctx, uint8_t* b, size_t cap) {
    static const uint8_t id[8] = { 0x4D, 0x43, 0x53, 0x2D, 0x53, 0x49, 0x4D, 0x31 };
    size_t n = cap < 8 ? cap : 8;
    memcpy(b, id, n);
    return (int)n;
}

void mcs_hal_sim_init(mcs_hal_t* h, mcs_hal_sim_t* s) {
    mcs_write_fn log = s->log; void* lud = s->log_ud;
    uint32_t (*clk)(void*) = s->clock_us; void* cud = s->clock_ud;
    memset(s, 0, sizeof *s);
    s->log = log; s->log_ud = lud; s->clock_us = clk; s->clock_ud = cud;
    s->temp_centi = 2500;
    s->regs[0x75] = 0x68;
    for (int i = 0; i < 16; i++) s->adc[i] = (uint16_t)(i * 256 + 100);
    memset(s->flash, 0xFF, sizeof s->flash);
    memset(h, 0, sizeof *h);
    h->board = "sim";
    h->ctx = s;
    h->gpio_mode = g_mode; h->gpio_write = g_write; h->gpio_read = g_read; h->gpio_irq = g_irq; h->pin_lookup = g_lookup;
    h->uart_open = u_open; h->uart_config = u_config; h->uart_write = u_write; h->uart_read = u_read; h->uart_available = u_avail;
    h->i2c_open = i_open; h->i2c_write = i_write; h->i2c_read = i_read; h->i2c_probe = i_probe;
    h->spi_open = s_open; h->spi_transfer = s_xfer;
    h->adc_read = a_read; h->adc_bits = 12; h->adc_vref_mv = 3300;
    h->dac_write = d_write; h->dac_bits = 12;
    h->pwm_set = p_set; h->pwm_set16 = p_set16; h->pwm_stop = p_stop;
    h->timer_start = t_start; h->timer_stop = t_stop;
    h->i2s_open = s2_open; h->i2s_write = s2_write; h->i2s_read = s2_read;
    h->qspi_open = q_open; h->qspi_command = q_cmd;
    h->can_open = c_open; h->can_send = c_send; h->can_recv = c_recv;
    h->wdt_start = w_start; h->wdt_feed = w_feed;
    h->rtc_get = r_get; h->rtc_set = r_set;
#if MCS_ENABLE_WS2812
    static const mcs_ws2812_ops_t ws_ops = { l_write };
    static mcs_driver_t ws = MCS_WS2812_DRIVER(&ws_ops, NULL);
    ws.ctx = s;                                   /* the "ws2812" driver logs frames */
    mcs_driver_register(&ws);
#endif
    h->micros = sim_micros; h->delay_us = sim_delay_us; h->reset = sys_reset; h->unique_id = sys_uid;
    h->cpu_hz = 160000000u;
    h->poll_event = sim_poll;
}
#endif
