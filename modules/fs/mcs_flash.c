/*
 * MicroCS - raw flash layer: generic SPI NOR and SPI NAND drivers (mcs_flash.h).
 * No dependencies beyond the SPI transfer callback; the filesystems that sit
 * on top (LittleFS, YAFFS2) are optional and live in mcs_vfs_lfs.c / mcs_vfs_yaffs.c.
 */
#include "mcs_flash.h"
#if MCS_ENABLE_FLASH
#include <string.h>

#ifndef MCS_FLASH_POLL_MAX
#define MCS_FLASH_POLL_MAX 20000000UL   /* status polls before giving up (no wait fn) */
#endif
#ifndef MCS_FLASH_WAIT_US
#define MCS_FLASH_WAIT_US 50            /* wait fn period */
#endif

/* ===================================================================== */
/* SPI NOR                                                               */
/* ===================================================================== */
#define NOR(f) ((mcs_spinor_t*)(f)->ctx)

static int nor_cmd(mcs_spinor_t* d, uint8_t op) { return d->xfer(d->ud, &op, 1, NULL, NULL, 0) ? MCS_FLASH_EIO : 0; }
static int nor_status(mcs_spinor_t* d, uint8_t* sr) {
    uint8_t op = 0x05;
    return d->xfer(d->ud, &op, 1, NULL, sr, 1) ? MCS_FLASH_EIO : 0;
}
static int nor_wait(mcs_spinor_t* d) {
    unsigned long limit = d->wait ? 10UL * 1000 * 1000 / MCS_FLASH_WAIT_US : MCS_FLASH_POLL_MAX; /* 10 s */
    for (unsigned long i = 0; i < limit; i++) {
        uint8_t sr;
        if (nor_status(d, &sr)) return MCS_FLASH_EIO;
        if (!(sr & 1)) return 0;
        if (d->wait) d->wait(d->ud, MCS_FLASH_WAIT_US);
    }
    return MCS_FLASH_EIO;
}
static size_t nor_addr(mcs_spinor_t* d, uint8_t* c, uint8_t op, uint32_t a) {
    size_t n = 0;
    c[n++] = op;
    if (d->addr4) c[n++] = (uint8_t)(a >> 24);
    c[n++] = (uint8_t)(a >> 16); c[n++] = (uint8_t)(a >> 8); c[n++] = (uint8_t)a;
    return n;
}
static int nor_read(mcs_flash_t* f, uint32_t addr, void* buf, uint32_t n) {
    mcs_spinor_t* d = NOR(f);
    if (addr > d->size || n > d->size - addr) return MCS_FLASH_EINVAL;
    uint8_t c[5];
    size_t k = nor_addr(d, c, d->addr4 ? 0x13 : 0x03, addr);
    return d->xfer(d->ud, c, k, NULL, (uint8_t*)buf, n) ? MCS_FLASH_EIO : 0;
}
static int nor_prog(mcs_flash_t* f, uint32_t addr, const void* buf, uint32_t n) {
    mcs_spinor_t* d = NOR(f);
    if (addr > d->size || n > d->size - addr) return MCS_FLASH_EINVAL;
    const uint8_t* p = (const uint8_t*)buf;
    while (n) {
        uint32_t chunk = f->page_size - (addr % f->page_size);   /* never cross a page */
        if (chunk > n) chunk = n;
        uint8_t c[5];
        size_t k = nor_addr(d, c, d->addr4 ? 0x12 : 0x02, addr);
        int e = nor_cmd(d, 0x06);
        if (!e) e = d->xfer(d->ud, c, k, p, NULL, chunk) ? MCS_FLASH_EIO : 0;
        if (!e) e = nor_wait(d);
        if (e) return e;
        addr += chunk; p += chunk; n -= chunk;
    }
    return 0;
}
static int nor_erase(mcs_flash_t* f, uint32_t block) {
    mcs_spinor_t* d = NOR(f);
    if (block >= f->block_count) return MCS_FLASH_EINVAL;
    uint8_t op = d->erase_op;
    if (d->addr4) op = op == 0x20 ? 0x21 : 0xDC;
    uint8_t c[5];
    size_t k = nor_addr(d, c, op, block * f->block_size);
    int e = nor_cmd(d, 0x06);
    if (!e) e = d->xfer(d->ud, c, k, NULL, NULL, 0) ? MCS_FLASH_EIO : 0;
    return e ? e : nor_wait(d);
}

int mcs_spinor_init(mcs_spinor_t* d, mcs_spi_xfer_fn xfer, void* ud, uint32_t size, uint32_t erase_size) {
    mcs_flash_wait_fn wait = d->wait;            /* may be set before init */
    memset(d, 0, sizeof *d);
    d->xfer = xfer; d->ud = ud; d->wait = wait;
    if (erase_size != 65536) erase_size = 4096;
    d->erase_op = erase_size == 4096 ? 0x20 : 0xD8;
    nor_cmd(d, 0xAB);                            /* release from deep power-down */
    uint8_t op = 0x9F, id[3];
    if (d->xfer(d->ud, &op, 1, NULL, id, 3)) return MCS_FLASH_EIO;
    d->jedec_id = (uint32_t)id[0] << 16 | (uint32_t)id[1] << 8 | id[2];
    bool valid = id[0] != 0x00 && id[0] != 0xFF;
    if (!size) {
        if (!valid) return MCS_FLASH_ENODEV;
        uint8_t c = id[2];
        if (c >= 0x10 && c <= 0x1F) size = 1UL << c;             /* most vendors: 2^c bytes */
        else if (c >= 0x20 && c <= 0x25) size = 1UL << (c - 6);  /* Micron/ISSI 512 Mbit+ */
        else return MCS_FLASH_ENODEV;
        if (c >= 0x20 && size > (1UL << 28)) size = 1UL << 28;   /* keep in 32 bits */
    }
    if (size < erase_size) return MCS_FLASH_EINVAL;
    d->size = size;
    d->addr4 = size > (16UL << 20);
    if (d->addr4) nor_cmd(d, 0xB7);              /* enter 4-byte mode (harmless with 4-byte opcodes) */
    /* clear block protection if the chip powered up locked */
    if (valid && id[0] == 0xBF) { nor_cmd(d, 0x06); nor_cmd(d, 0x98); }   /* SST26: global unlock */
    else {
        uint8_t sr;
        if (!nor_status(d, &sr) && (sr & 0x3C)) {
            uint8_t w[2] = { 0x01, (uint8_t)(sr & ~0x3C) };
            nor_cmd(d, 0x06);
            d->xfer(d->ud, w, 2, NULL, NULL, 0);
            nor_wait(d);
        }
    }
    mcs_flash_t* f = &d->flash;
    f->type = MCS_FLASH_NOR;
    f->page_size = 256;
    f->spare_size = 0;
    f->block_size = erase_size;
    f->block_count = size / erase_size;
    f->read = nor_read; f->prog = nor_prog; f->erase = nor_erase;
    f->ctx = d;
    return 0;
}

/* ===================================================================== */
/* SPI NAND                                                              */
/* ===================================================================== */
#define NAND(f) ((mcs_spinand_t*)(f)->ctx)
#define PAGES_PER_BLOCK(f) ((f)->block_size / (f)->page_size)

static int nand_get_feature(mcs_spinand_t* d, uint8_t reg, uint8_t* v) {
    uint8_t c[2] = { 0x0F, reg };
    return d->xfer(d->ud, c, 2, NULL, v, 1) ? MCS_FLASH_EIO : 0;
}
static int nand_set_feature(mcs_spinand_t* d, uint8_t reg, uint8_t v) {
    uint8_t c[3] = { 0x1F, reg, v };
    return d->xfer(d->ud, c, 3, NULL, NULL, 0) ? MCS_FLASH_EIO : 0;
}
static int nand_cmd(mcs_spinand_t* d, uint8_t op) { return d->xfer(d->ud, &op, 1, NULL, NULL, 0) ? MCS_FLASH_EIO : 0; }
static int nand_row_cmd(mcs_spinand_t* d, uint8_t op, uint32_t row) {
    uint8_t c[4] = { op, (uint8_t)(row >> 16), (uint8_t)(row >> 8), (uint8_t)row };
    return d->xfer(d->ud, c, 4, NULL, NULL, 0) ? MCS_FLASH_EIO : 0;
}
/* wait until not busy; returns the status register or <0 */
static int nand_wait(mcs_spinand_t* d) {
    unsigned long limit = d->wait ? 10UL * 1000 * 1000 / MCS_FLASH_WAIT_US : MCS_FLASH_POLL_MAX;
    for (unsigned long i = 0; i < limit; i++) {
        uint8_t sr;
        if (nand_get_feature(d, 0xC0, &sr)) return MCS_FLASH_EIO;
        if (!(sr & 1)) return sr;
        if (d->wait) d->wait(d->ud, MCS_FLASH_WAIT_US);
    }
    return MCS_FLASH_EIO;
}
/* load `page` into the chip's cache; 0, MCS_FLASH_FIXED or <0 */
static int nand_load(mcs_spinand_t* d, uint32_t page) {
    if (d->loaded_page == page) return d->loaded_ecc;
    d->loaded_page = ~0u;
    int sr = nand_row_cmd(d, 0x13, page);
    if (!sr) sr = nand_wait(d);
    if (sr < 0) return sr;
    int ecc = (sr >> 4) & 3, r = 0;
    if (ecc == 2 || (ecc == 3 && (d->jedec_id >> 16) == 0xEF)) r = MCS_FLASH_EECC;
    else if (ecc) r = MCS_FLASH_FIXED;
    if (r >= 0) { d->loaded_page = page; d->loaded_ecc = (int8_t)r; }
    return r;
}
static int nand_read_cache(mcs_spinand_t* d, uint32_t col, void* buf, uint32_t n) {
    uint8_t c[4] = { 0x03, (uint8_t)(col >> 8), (uint8_t)col, 0 };
    if (d->col_dummy_first) { c[1] = 0; c[2] = (uint8_t)(col >> 8); c[3] = (uint8_t)col; }
    return d->xfer(d->ud, c, 4, NULL, (uint8_t*)buf, n) ? MCS_FLASH_EIO : 0;
}
static int nand_read_page(mcs_flash_t* f, uint32_t page, void* data, uint32_t n,
                          void* spare, uint32_t spare_off, uint32_t spare_n) {
    mcs_spinand_t* d = NAND(f);
    if (page >= f->block_count * PAGES_PER_BLOCK(f) || n > f->page_size || spare_off + spare_n > f->spare_size)
        return MCS_FLASH_EINVAL;
    int r = nand_load(d, page);
    if (r < 0 && r != MCS_FLASH_EECC) return r;
    int e = 0;
    if (data && n) e = nand_read_cache(d, 0, data, n);
    if (!e && spare && spare_n) e = nand_read_cache(d, f->page_size + spare_off, spare, spare_n);
    return e ? e : r;
}
static int nand_prog_page(mcs_flash_t* f, uint32_t page, const void* data, uint32_t n,
                          const void* spare, uint32_t spare_off, uint32_t spare_n) {
    mcs_spinand_t* d = NAND(f);
    if (page >= f->block_count * PAGES_PER_BLOCK(f) || n > f->page_size || spare_off + spare_n > f->spare_size)
        return MCS_FLASH_EINVAL;
    d->loaded_page = ~0u;
    int e = nand_cmd(d, 0x06);
    bool loaded = false;
    if (!e && data && n) {                        /* 02h: load + fill the rest of the buffer with FF */
        uint8_t c[3] = { 0x02, 0, 0 };
        e = d->xfer(d->ud, c, 3, (const uint8_t*)data, NULL, n) ? MCS_FLASH_EIO : 0;
        loaded = true;
    }
    if (!e && spare && spare_n) {
        uint32_t col = f->page_size + spare_off;
        uint8_t c[3] = { (uint8_t)(loaded ? 0x84 : 0x02), (uint8_t)(col >> 8), (uint8_t)col };
        e = d->xfer(d->ud, c, 3, (const uint8_t*)spare, NULL, spare_n) ? MCS_FLASH_EIO : 0;
    }
    if (!e) e = nand_row_cmd(d, 0x10, page);
    if (e) return e;
    int sr = nand_wait(d);
    if (sr < 0) return sr;
    return (sr & 0x08) ? MCS_FLASH_EPROG : 0;    /* P-FAIL */
}
static int nand_erase(mcs_flash_t* f, uint32_t block) {
    mcs_spinand_t* d = NAND(f);
    if (block >= f->block_count) return MCS_FLASH_EINVAL;
    d->loaded_page = ~0u;
    int e = nand_cmd(d, 0x06);
    if (!e) e = nand_row_cmd(d, 0xD8, block * PAGES_PER_BLOCK(f));
    if (e) return e;
    int sr = nand_wait(d);
    if (sr < 0) return sr;
    return (sr & 0x04) ? MCS_FLASH_EPROG : 0;    /* E-FAIL */
}
static int nand_is_bad(mcs_flash_t* f, uint32_t block) {
    uint8_t m = 0xFF;
    if (block >= f->block_count) return MCS_FLASH_EINVAL;
    int r = nand_read_page(f, block * PAGES_PER_BLOCK(f), NULL, 0, &m, 0, 1);
    if (r < 0 && r != MCS_FLASH_EECC) return r;
    return m != 0xFF;
}
static int nand_mark_bad(mcs_flash_t* f, uint32_t block) {
    static const uint8_t z[2] = { 0, 0 };
    if (block >= f->block_count) return MCS_FLASH_EINVAL;
    nand_erase(f, block);                         /* best effort: a worn block may refuse */
    return nand_prog_page(f, block * PAGES_PER_BLOCK(f), NULL, 0, z, 0, 2);
}
/* byte-addressed view (LittleFS): any range for reads, whole pages from column 0 for programs */
static int nand_read(mcs_flash_t* f, uint32_t addr, void* buf, uint32_t n) {
    mcs_spinand_t* d = NAND(f);
    uint8_t* p = (uint8_t*)buf;
    if (addr > f->block_count * f->block_size || n > f->block_count * f->block_size - addr) return MCS_FLASH_EINVAL;
    while (n) {
        uint32_t page = addr / f->page_size, col = addr % f->page_size;
        uint32_t chunk = f->page_size - col;
        if (chunk > n) chunk = n;
        int r = nand_load(d, page);
        if (r < 0) return r;
        if (nand_read_cache(d, col, p, chunk)) return MCS_FLASH_EIO;
        addr += chunk; p += chunk; n -= chunk;
    }
    return 0;
}
static int nand_prog(mcs_flash_t* f, uint32_t addr, const void* buf, uint32_t n) {
    const uint8_t* p = (const uint8_t*)buf;
    if (addr % f->page_size) return MCS_FLASH_EINVAL;
    if (addr > f->block_count * f->block_size || n > f->block_count * f->block_size - addr) return MCS_FLASH_EINVAL;
    while (n) {
        uint32_t chunk = n < f->page_size ? n : f->page_size;
        int e = nand_prog_page(f, addr / f->page_size, p, chunk, NULL, 0, 0);
        if (e) return e;
        addr += chunk; p += chunk; n -= chunk;
    }
    return 0;
}

int mcs_spinand_init(mcs_spinand_t* d, mcs_spi_xfer_fn xfer, void* ud, uint32_t blocks) {
    mcs_flash_wait_fn wait = d->wait;
    uint8_t dummy_first = d->col_dummy_first;
    memset(d, 0, sizeof *d);
    d->xfer = xfer; d->ud = ud; d->wait = wait; d->col_dummy_first = dummy_first;
    d->loaded_page = ~0u;
    if (nand_cmd(d, 0xFF)) return MCS_FLASH_EIO;               /* reset */
    if (nand_wait(d) < 0) return MCS_FLASH_EIO;
    uint8_t c[2] = { 0x9F, 0 }, id[3];
    if (d->xfer(d->ud, c, 2, NULL, id, 3)) return MCS_FLASH_EIO;
    if (id[0] == 0x00 || id[0] == 0xFF) return MCS_FLASH_ENODEV;
    d->jedec_id = (uint32_t)id[0] << 16 | (uint32_t)id[1] << 8 | id[2];
    if (!blocks) blocks = (d->jedec_id == 0xEFAA22) ? 2048 : 1024;   /* W25N02KV : 1 Gbit */
    uint8_t cfg;
    if (nand_set_feature(d, 0xA0, 0x00)) return MCS_FLASH_EIO;        /* clear block protection */
    if (nand_get_feature(d, 0xB0, &cfg)) return MCS_FLASH_EIO;
    if (nand_set_feature(d, 0xB0, (uint8_t)(cfg | 0x10))) return MCS_FLASH_EIO; /* ECC-E on */
    mcs_flash_t* f = &d->flash;
    f->type = MCS_FLASH_NAND;
    f->page_size = 2048;
    f->spare_size = 64;
    f->block_size = 64 * 2048;
    f->block_count = blocks;
    f->read = nand_read; f->prog = nand_prog; f->erase = nand_erase;
    f->read_page = nand_read_page; f->prog_page = nand_prog_page;
    f->is_bad = nand_is_bad; f->mark_bad = nand_mark_bad;
    f->ctx = d;
    return 0;
}

/* ===================================================================== */
/* SPI over a MicroCS HAL                                                */
/* ===================================================================== */
#if MCS_ENABLE_HAL
int mcs_flash_hal_xfer(void* ud, const uint8_t* cmd, size_t ncmd, const uint8_t* tx, uint8_t* rx, size_t n) {
    mcs_flash_hal_spi_t* s = (mcs_flash_hal_spi_t*)ud;
    const mcs_hal_t* h = s->hal;
    uint8_t scratch[32];
    int e = 0;
    if (!h->spi_transfer || !h->gpio_write) return MCS_FLASH_EIO;
    h->gpio_write(h->ctx, s->cs_pin, 0);
    for (size_t i = 0; i < ncmd && !e; i += sizeof scratch) {
        size_t k = ncmd - i < sizeof scratch ? ncmd - i : sizeof scratch;
        e = h->spi_transfer(h->ctx, s->bus, cmd + i, scratch, k) < 0;
    }
    if (tx) {
        for (size_t i = 0; i < n && !e; i += sizeof scratch) {
            size_t k = n - i < sizeof scratch ? n - i : sizeof scratch;
            e = h->spi_transfer(h->ctx, s->bus, tx + i, scratch, k) < 0;
        }
    } else if (rx) {
        memset(scratch, 0xFF, sizeof scratch);
        for (size_t i = 0; i < n && !e; i += sizeof scratch) {
            size_t k = n - i < sizeof scratch ? n - i : sizeof scratch;
            e = h->spi_transfer(h->ctx, s->bus, scratch, rx + i, k) < 0;
            memset(scratch, 0xFF, sizeof scratch);
        }
    }
    h->gpio_write(h->ctx, s->cs_pin, 1);
    return e ? MCS_FLASH_EIO : 0;
}
#endif
#endif
