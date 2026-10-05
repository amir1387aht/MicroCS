/* MicroCS - simulated board. Deterministic so tests can assert on it. */
#include "mcs_hal.h"
#if MCS_ENABLE_HAL
#include <string.h>
#include <stdio.h>

#define SIM ((mcs_hal_sim_t*)ctx)

static void logf_(mcs_hal_sim_t* s, const char* fmt, int a, int b, int c) {
    if (!s->log) return;
    char line[96];
    int n = snprintf(line, sizeof line, fmt, a, b, c);
    if (n > 0) s->log(s->log_ud, line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
}

static int g_mode(void* ctx, int pin, int mode) {
    if (pin < 0 || pin >= 64 || mode < 0 || mode > 4) return MCS_HAL_ENOTSUP;
    SIM->mode[pin] = (uint8_t)mode;
    logf_(SIM, "[sim] gpio %d mode %d\n", pin, mode, 0);
    return 0;
}
static int g_write(void* ctx, int pin, int v) {
    if (pin < 0 || pin >= 64) return MCS_HAL_ENOTSUP;
    SIM->out[pin] = (uint8_t)(v != 0);
    logf_(SIM, "[sim] gpio %d <- %d\n", pin, v != 0, 0);
    return 0;
}
static int g_read(void* ctx, int pin) {
    if (pin < 0 || pin >= 64) return MCS_HAL_ENOTSUP;
    switch (SIM->mode[pin]) {
    case MCS_GPIO_OUTPUT: case MCS_GPIO_OPEN_DRAIN: return SIM->out[pin];
    case MCS_GPIO_INPUT_PULLUP: return SIM->inputs[pin] == 2 ? 0 : 1;   /* 2 = driven low */
    default: return SIM->inputs[pin] == 1;
    }
}

static int u_open(void* ctx, int port, uint32_t baud) {
    if (port < 0 || port >= 4) return MCS_HAL_ENOTSUP;
    SIM->uart_baud[port] = baud;
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
    return (int)i;
}
static int u_avail(void* ctx, int port) {
    if (port < 0 || port >= 4) return MCS_HAL_ENOTSUP;
    return (SIM->uart_head[port] - SIM->uart_tail[port] + 256) % 256;
}
static int u_read(void* ctx, int port, uint8_t* b, size_t n, uint32_t timeout) {
    (void)timeout;
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
    return MCS_HAL_ENODEV;
}
static int s_xfer(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n) {
    if (bus != 0) return MCS_HAL_ENOTSUP;
    memcpy(rx, tx, n);
    logf_(SIM, "[sim] spi%d %d bytes\n", bus, (int)n, 0);
    return 0;
}
static int a_read(void* ctx, int ch) { return ch >= 0 && ch < 16 ? SIM->adc[ch] : MCS_HAL_ENOTSUP; }
static int p_set(void* ctx, int ch, uint32_t f, uint16_t d) {
    if (ch < 0 || ch >= 8) return MCS_HAL_ENOTSUP;
    SIM->pwm_freq[ch] = f; SIM->pwm_duty[ch] = d;
    logf_(SIM, "[sim] pwm%d %d Hz duty %d/1000\n", ch, (int)f, d);
    return 0;
}

void mcs_hal_sim_init(mcs_hal_t* h, mcs_hal_sim_t* s) {
    mcs_write_fn log = s->log; void* lud = s->log_ud;
    memset(s, 0, sizeof *s);
    s->log = log; s->log_ud = lud;
    s->temp_centi = 2500;
    for (int i = 0; i < 16; i++) s->adc[i] = (uint16_t)(i * 256 + 100);
    memset(h, 0, sizeof *h);
    h->board = "sim";
    h->ctx = s;
    h->gpio_mode = g_mode; h->gpio_write = g_write; h->gpio_read = g_read;
    h->uart_open = u_open; h->uart_write = u_write; h->uart_read = u_read; h->uart_available = u_avail;
    h->i2c_write = i_write; h->i2c_read = i_read;
    h->spi_transfer = s_xfer;
    h->adc_read = a_read; h->adc_bits = 12;
    h->pwm_set = p_set;
}
#endif
