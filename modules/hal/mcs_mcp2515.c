/* MCP2515 datasheet DS20001801: SPI instructions, bit timing and RX/TX buffers.
 * Polling avoids an IRQ pin requirement on Pico, STM32, ESP32 and Arduino alike. */
#include "mcs_mcp2515.h"
#include "mcs_can_timing.h"
#if MCS_ENABLE_HAL
#include <string.h>
static int transfer(mcs_mcp2515_t* d, const uint8_t* tx, uint8_t* rx, size_t n) {
    const mcs_hal_t* h = d->hal;
    int r = h->gpio_write(h->ctx, d->cs, 0);
    if (r >= 0) { r = h->spi_transfer(h->ctx, d->spi_bus, tx, rx, n); if (r >= 0 && (size_t)r != n) r = MCS_HAL_ERR; }
    int end = h->gpio_write(h->ctx, d->cs, 1);
    return r < 0 ? r : end < 0 ? end : 0;
}
static int read_regs(mcs_mcp2515_t* d, uint8_t reg, uint8_t* data, size_t n) {
    uint8_t tx[16] = { 0x03, reg }, rx[16];
    if (n > 14) return MCS_HAL_EINVAL;
    int r = transfer(d, tx, rx, n + 2);
    if (!r) memcpy(data, rx + 2, n);
    return r;
}
static int write_regs(mcs_mcp2515_t* d, uint8_t reg, const uint8_t* data, size_t n) {
    uint8_t tx[16] = { 0x02, reg }, rx[16];
    if (n > 14) return MCS_HAL_EINVAL;
    memcpy(tx + 2, data, n);
    return transfer(d, tx, rx, n + 2);
}
static int modify(mcs_mcp2515_t* d, uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t tx[4] = { 0x05, reg, mask, value }, rx[4];
    return transfer(d, tx, rx, sizeof tx);
}
static int set_mode(mcs_mcp2515_t* d, uint8_t mode) {
    int r = modify(d, 0x0f, 0xe0, mode);
    if (r) return r;
    for (int i = 0; i < 100; i++) {
        uint8_t status;
        if ((r = read_regs(d, 0x0e, &status, 1))) return r;
        if ((status & 0xe0) == mode) return 0;
        d->hal->delay_us(d->hal->ctx, 100);
    }
    return MCS_HAL_ENODEV;                      /* absent device / mode transition failed */
}
int mcs_mcp2515_open(mcs_mcp2515_t* d, const mcs_hal_t* h, int bus, int cs,
                    uint32_t oscillator, uint32_t rate, int mode) {
    mcs_can_timing_t t = { 0 };
    if (!d || !h || !h->spi_transfer || !h->gpio_mode || !h->gpio_write || !h->delay_us) return MCS_HAL_ENOTSUP;
    if (bus < 0 || bus > 32767 || cs < 0 || cs > 32767 || mode < 0 || mode > MCS_CAN_LOOPBACK) return MCS_HAL_EINVAL;
    if ((oscillator != 8000000 && oscillator != 16000000 && oscillator != 20000000) ||
        !mcs_can_timing(oscillator / 2, rate, 64, 16, 8, 2, &t)) return MCS_HAL_EINVAL;
    memset(d, 0, sizeof *d); d->hal = h; d->spi_bus = (int16_t)bus; d->cs = (int16_t)cs; d->mode = (uint8_t)mode;
    int r = h->gpio_write(h->ctx, cs, 1);        /* avoid a CS-low glitch */
    if (r >= 0) r = h->gpio_mode(h->ctx, cs, MCS_GPIO_OUTPUT);
    if (r >= 0) r = h->gpio_write(h->ctx, cs, 1);
    if (r < 0) return r;
    uint8_t tx = 0xc0, rx;                     /* RESET */
    if ((r = transfer(d, &tx, &rx, 1))) return r;
    h->delay_us(h->ctx, 1000);                 /* oscillator startup after reset */
    if ((r = set_mode(d, 0x80))) return r;
    unsigned prop = t.seg1 / 2, phase1 = t.seg1 - prop;
    uint8_t cnf[3] = { (uint8_t)(t.seg2 - 1), (uint8_t)(0x80 | ((phase1 - 1) << 3) | (prop - 1)), (uint8_t)(t.prescaler - 1) };
    if ((r = write_regs(d, 0x28, cnf, 3))) return r; /* CNF3, CNF2, CNF1; SJW = 1 TQ */
    uint8_t control = 0x64;                    /* RXM=11: any valid frame, BUKT rollover */
    if ((r = write_regs(d, 0x60, &control, 1))) return r;
    control = 0x60;
    if ((r = write_regs(d, 0x70, &control, 1))) return r;
    control = 0;                              /* polling, no enabled interrupts */
    if ((r = write_regs(d, 0x2b, &control, 1)) || (r = write_regs(d, 0x2c, &control, 1))) return r;
    if ((r = set_mode(d, mode == MCS_CAN_LOOPBACK ? 0x40 : mode == MCS_CAN_LISTEN_ONLY ? 0x60 : 0))) return r;
    d->opened = 1; return 0;
}
int mcs_mcp2515_close(mcs_mcp2515_t* d) {
    if (!d || !d->opened) return 0;
    int r = modify(d, 0x0f, 0x10, 0x10);       /* ABAT: abort pending transmissions */
    if (!r) r = set_mode(d, 0x20);             /* controller sleep, TX stays recessive */
    if (!r) d->opened = 0;
    return r;
}
int mcs_mcp2515_try_send(mcs_mcp2515_t* d, const mcs_can_frame_t* f) {
    if (!d || !d->opened) return MCS_HAL_ENODEV;
    if (!f || f->len > 8 || f->id > (f->extended ? 0x1fffffffu : 0x7ffu)) return MCS_HAL_EINVAL;
    if (d->mode == MCS_CAN_LISTEN_ONLY) return MCS_HAL_ENOTSUP;
    uint8_t eflg;
    int r = read_regs(d, 0x2d, &eflg, 1);
    if (r) return r;
    if (eflg & 0x20) return MCS_HAL_ERR;         /* bus-off: caller must close/reopen */
    for (int i = 0; i < 3; i++) {
        uint8_t base = (uint8_t)(0x30 + 16 * i), ctrl;
        if ((r = read_regs(d, base, &ctrl, 1))) return r;
        if (ctrl & 8) continue;                /* TXREQ */
        uint8_t b[13] = { 0 };
        uint32_t sid = f->extended ? f->id >> 18 : f->id;
        b[0] = (uint8_t)(sid >> 3); b[1] = (uint8_t)((sid & 7) << 5);
        if (f->extended) { b[1] |= (uint8_t)(8 | ((f->id >> 16) & 3)); b[2] = (uint8_t)(f->id >> 8); b[3] = (uint8_t)f->id; }
        b[4] = (uint8_t)(f->len | (f->rtr ? 0x40 : 0));
        if (!f->rtr) memcpy(b + 5, f->data, f->len);
        if ((r = write_regs(d, (uint8_t)(base + 1), b, sizeof b))) return r;
        uint8_t tx = (uint8_t)(0x80 | (1 << i)), rx; /* RTS */
        return transfer(d, &tx, &rx, 1);
    }
    return MCS_HAL_ETIMEOUT;
}
int mcs_mcp2515_try_recv(mcs_mcp2515_t* d, mcs_can_frame_t* f) {
    if (!d || !d->opened) return MCS_HAL_ENODEV;
    if (!f) return MCS_HAL_EINVAL;
    uint8_t flags;
    int r = read_regs(d, 0x2c, &flags, 1);
    if (r) return r;
    int buf = flags & 1 ? 0 : flags & 2 ? 1 : -1;
    if (buf < 0) return MCS_HAL_ETIMEOUT;
    uint8_t b[13], ctrl;
    if ((r = read_regs(d, (uint8_t)(0x60 + buf * 16), &ctrl, 1))) return r;
    if ((r = read_regs(d, (uint8_t)(0x61 + buf * 16), b, sizeof b))) return r;
    if ((r = modify(d, 0x2c, (uint8_t)(1 << buf), 0))) return r;
    memset(f, 0, sizeof *f);
    f->extended = (b[1] & 8) != 0;
    f->id = ((uint32_t)b[0] << 3) | (b[1] >> 5);
    if (f->extended) f->id = (f->id << 18) | ((uint32_t)(b[1] & 3) << 16) | ((uint32_t)b[2] << 8) | b[3];
    f->rtr = (ctrl & 8) != 0;                  /* RXRTR, valid for standard and extended */
    f->len = (uint8_t)((b[4] & 15) > 8 ? 8 : b[4] & 15);
    if (!f->rtr) memcpy(f->data, b + 5, f->len);
    return 0;
}
#endif
