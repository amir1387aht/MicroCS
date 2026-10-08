/* Simulated SPI flash chips for the MicroCS flash tests: a W25Q-style SPI NOR
 * and a W25N-style SPI NAND (2048+64 byte pages, 64 pages/block), driven
 * through the same mcs_spi_xfer_fn callback a real board would provide.
 * Physics are enforced: programming only clears bits, NAND pages may be
 * programmed once per erase and in order inside a block, bad blocks refuse
 * erase/program, and injected bit errors are reported through the ECC bits. */
#ifndef FLASH_SIM_H
#define FLASH_SIM_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mcs_flash.h"

/* ---------------- SPI NOR ---------------- */
typedef struct {
    uint8_t* mem; uint32_t size;
    uint8_t jedec[3];
    uint8_t sr;          /* bit0 BUSY, bit1 WEL, bits 2-5 BP */
    int busy_polls;      /* status reads that still report BUSY */
    uint8_t addr4;
    long progs, erases, overwrite_violations, unlocked_writes;
} sim_nor_t;

static void sim_nor_init(sim_nor_t* s, uint32_t size, uint8_t cap_code, uint8_t bp) {
    memset(s, 0, sizeof *s);
    s->mem = (uint8_t*)malloc(size); memset(s->mem, 0xFF, size); s->size = size;
    s->jedec[0] = 0xEF; s->jedec[1] = 0x40; s->jedec[2] = cap_code;
    s->sr = (uint8_t)(bp << 2);
}
static uint32_t nor_get_addr(sim_nor_t* s, const uint8_t* c, size_t n, int four) {
    uint32_t a = 0; int k = four ? 4 : 3;
    if (n < (size_t)(1 + k)) return 0;
    for (int i = 0; i < k; i++) a = a << 8 | c[1 + i];
    return a % s->size;
}
static int sim_nor_xfer(void* ud, const uint8_t* cmd, size_t ncmd, const uint8_t* tx, uint8_t* rx, size_t n) {
    sim_nor_t* s = (sim_nor_t*)ud;
    uint8_t op = cmd[0];
    if (op != 0x05 && (s->sr & 1)) return -1;                /* only status reads while busy */
    switch (op) {
    case 0x9F:
        for (size_t i = 0; i < n; i++) rx[i] = i < 3 ? s->jedec[i] : 0;
        return 0;
    case 0xAB: case 0xB7: if (op == 0xB7) s->addr4 = 1; return 0;
    case 0x05:
        if (s->busy_polls > 0 && --s->busy_polls == 0) s->sr &= (uint8_t)~1;
        if (n) rx[0] = s->sr;
        return 0;
    case 0x06: s->sr |= 2; return 0;
    case 0x01: if (!(s->sr & 2)) return -1; s->sr = (uint8_t)(cmd[1] & 0x3C); return 0;
    case 0x03: case 0x13: {
        uint32_t a = nor_get_addr(s, cmd, ncmd, op == 0x13 || s->addr4);
        for (size_t i = 0; i < n; i++) rx[i] = s->mem[(a + i) % s->size];
        return 0; }
    case 0x02: case 0x12: {
        if (!(s->sr & 2)) { s->unlocked_writes++; return 0; }
        if (s->sr & 0x3C) { s->sr &= (uint8_t)~2; return 0; } /* protected: ignored */
        uint32_t a = nor_get_addr(s, cmd, ncmd, op == 0x12 || s->addr4);
        if (n > 256) return -1;
        for (size_t i = 0; i < n; i++) {
            uint32_t at = (a & ~255u) | ((a + (uint32_t)i) & 255u);   /* wraps inside the page */
            if ((s->mem[at] & tx[i]) != tx[i]) s->overwrite_violations++;
            s->mem[at] &= tx[i];
        }
        s->progs++; s->sr = (uint8_t)((s->sr & ~2) | 1); s->busy_polls = 2;
        return 0; }
    case 0x20: case 0x21: case 0xD8: case 0xDC: {
        if (!(s->sr & 2)) { s->unlocked_writes++; return 0; }
        uint32_t a = nor_get_addr(s, cmd, ncmd, op == 0x21 || op == 0xDC || s->addr4);
        uint32_t sz = (op == 0x20 || op == 0x21) ? 4096 : 65536;
        memset(s->mem + (a & ~(sz - 1)), 0xFF, sz);
        s->erases++; s->sr = (uint8_t)((s->sr & ~2) | 1); s->busy_polls = 5;
        return 0; }
    }
    return -1;
}

/* ---------------- SPI NAND ---------------- */
#define SN_PAGE 2048
#define SN_SPARE 64
#define SN_PPB 64
typedef struct {
    uint8_t* mem;        /* blocks * 64 * (2048 + 64) */
    uint8_t* prog_count; /* per page programs since erase */
    int8_t* next_page;   /* per block next programmable page */
    uint8_t* worn;       /* per block: erase/program fails */
    uint32_t blocks;
    uint8_t cache[SN_PAGE + SN_SPARE];
    uint8_t prot, cfg, sr;
    int busy_polls;
    int flip_page, flip_bits;      /* inject bit errors on reads of a page */
    long reads, progs, erases, nop_violations, order_violations, overwrite_violations;
} sim_nand_t;

static uint8_t* sn_page(sim_nand_t* s, uint32_t page) { return s->mem + (size_t)page * (SN_PAGE + SN_SPARE); }
static void sim_nand_init(sim_nand_t* s, uint32_t blocks) {
    memset(s, 0, sizeof *s);
    s->blocks = blocks;
    size_t total = (size_t)blocks * SN_PPB * (SN_PAGE + SN_SPARE);
    s->mem = (uint8_t*)malloc(total); memset(s->mem, 0xFF, total);
    s->prog_count = (uint8_t*)calloc((size_t)blocks * SN_PPB, 1);
    s->next_page = (int8_t*)calloc(blocks, 1);
    s->worn = (uint8_t*)calloc(blocks, 1);
    s->prot = 0x7C; s->cfg = 0x18; s->flip_page = -1;
}
static void sim_nand_factory_bad(sim_nand_t* s, uint32_t block) {
    sn_page(s, block * SN_PPB)[SN_PAGE] = 0x00;
    s->worn[block] = 1;
}
static int sim_nand_xfer(void* ud, const uint8_t* cmd, size_t ncmd, const uint8_t* tx, uint8_t* rx, size_t n) {
    sim_nand_t* s = (sim_nand_t*)ud;
    uint8_t op = cmd[0];
    if (!(op == 0x0F && ncmd >= 2 && cmd[1] == 0xC0) && (s->sr & 1)) return -1;
    switch (op) {
    case 0xFF: s->sr = 1; s->busy_polls = 3; s->prot = 0x7C; return 0;
    case 0x9F: { static const uint8_t id[3] = { 0xEF, 0xAA, 0x21 };
        if (ncmd != 2) return -1;
        for (size_t i = 0; i < n; i++) rx[i] = i < 3 ? id[i] : 0;
        return 0; }
    case 0x0F:
        if (ncmd < 2 || !n) return -1;
        if (cmd[1] == 0xC0) { if (s->busy_polls > 0 && --s->busy_polls == 0) s->sr &= (uint8_t)~1; rx[0] = s->sr; }
        else if (cmd[1] == 0xA0) rx[0] = s->prot;
        else if (cmd[1] == 0xB0) rx[0] = s->cfg;
        else return -1;
        return 0;
    case 0x1F:
        if (ncmd < 3) return -1;
        if (cmd[1] == 0xA0) s->prot = cmd[2]; else if (cmd[1] == 0xB0) s->cfg = cmd[2]; else return -1;
        return 0;
    case 0x06: s->sr |= 2; return 0;
    case 0x13: {
        uint32_t page = (uint32_t)cmd[1] << 16 | (uint32_t)cmd[2] << 8 | cmd[3];
        if (page >= s->blocks * SN_PPB) return -1;
        memcpy(s->cache, sn_page(s, page), SN_PAGE + SN_SPARE);
        s->sr &= (uint8_t)~0x30;
        if ((int)page == s->flip_page && (s->cfg & 0x10)) {
            if (s->flip_bits <= 4) s->sr |= 0x10;        /* corrected */
            else { s->sr |= 0x20; s->cache[0] ^= 0xFF; } /* uncorrectable */
        }
        s->reads++; s->sr |= 1; s->busy_polls = 2;
        return 0; }
    case 0x03: {
        if (ncmd != 4) return -1;
        uint32_t col = (uint32_t)cmd[1] << 8 | cmd[2];
        for (size_t i = 0; i < n; i++) rx[i] = col + i < sizeof s->cache ? s->cache[col + i] : 0xFF;
        return 0; }
    case 0x02: case 0x84: {
        if (!(s->sr & 2)) return -1;
        uint32_t col = (uint32_t)cmd[1] << 8 | cmd[2];
        if (op == 0x02) memset(s->cache, 0xFF, sizeof s->cache);
        if (col + n > sizeof s->cache) return -1;
        memcpy(s->cache + col, tx, n);
        return 0; }
    case 0x10: {
        uint32_t page = (uint32_t)cmd[1] << 16 | (uint32_t)cmd[2] << 8 | cmd[3];
        if (!(s->sr & 2) || page >= s->blocks * SN_PPB) return -1;
        uint32_t blk = page / SN_PPB, idx = page % SN_PPB;
        s->sr = (uint8_t)(s->sr & ~(2 | 0x08)) | 1; s->busy_polls = 3;
        if (s->prot & 0x7C) { s->sr |= 0x08; return 0; }
        bool data_written = false;
        for (int i = 0; i < SN_PAGE; i++) if (s->cache[i] != 0xFF) { data_written = true; break; }
        if (s->worn[blk] && data_written) { s->sr |= 0x08; return 0; }
        if (data_written) {   /* bad-block marking writes only the spare and is exempt */
            if (s->prog_count[page]++) s->nop_violations++;
            if ((int)idx < s->next_page[blk]) s->order_violations++;
            s->next_page[blk] = (int8_t)(idx + 1);
        }
        uint8_t* p = sn_page(s, page);
        for (int i = 0; i < SN_PAGE + SN_SPARE; i++) {
            if (s->cache[i] != 0xFF && (p[i] & s->cache[i]) != s->cache[i]) s->overwrite_violations++;
            p[i] &= s->cache[i];
        }
        s->progs++;
        return 0; }
    case 0xD8: {
        uint32_t page = (uint32_t)cmd[1] << 16 | (uint32_t)cmd[2] << 8 | cmd[3];
        if (!(s->sr & 2) || page >= s->blocks * SN_PPB) return -1;
        uint32_t blk = page / SN_PPB;
        s->sr = (uint8_t)(s->sr & ~(2 | 0x04)) | 1; s->busy_polls = 4;
        if ((s->prot & 0x7C) || s->worn[blk]) { s->sr |= 0x04; return 0; }
        memset(sn_page(s, blk * SN_PPB), 0xFF, (size_t)SN_PPB * (SN_PAGE + SN_SPARE));
        memset(s->prog_count + blk * SN_PPB, 0, SN_PPB);
        s->next_page[blk] = 0;
        s->erases++;
        return 0; }
    }
    return -1;
}

/* ---------------- internal MCU flash (memory-mapped, like STM32 / RP2 / nRF) ----------------
 * write_size-byte program units (STM32L4/G4: 8, U5/H5: 16, H7: 32) that may be
 * programmed once per erase (ECC parts fault on a second program), erase in
 * block_size units; mirrors what the port drivers (mcs_stm32_flash_init ...) expose. */
typedef struct {
    mcs_flash_t flash;
    uint8_t* mem; uint8_t* programmed;   /* one flag per write unit */
    long progs, erases, align_violations, reprogram_violations;
} sim_iflash_t;
static int sif_read(mcs_flash_t* f, uint32_t a, void* b, uint32_t n) {
    sim_iflash_t* s = (sim_iflash_t*)f->ctx;
    if ((uint64_t)a + n > (uint64_t)f->block_size * f->block_count) return MCS_FLASH_EINVAL;
    memcpy(b, s->mem + a, n); return 0;
}
static int sif_prog(mcs_flash_t* f, uint32_t a, const void* b, uint32_t n) {
    sim_iflash_t* s = (sim_iflash_t*)f->ctx;
    uint32_t w = f->write_size ? f->write_size : 1;
    if (a % w || n % w) { s->align_violations++; return MCS_FLASH_EINVAL; }
    for (uint32_t u = a / w; u < (a + n) / w; u++) {
        if (s->programmed[u]) s->reprogram_violations++;
        s->programmed[u] = 1;
    }
    const uint8_t* p = (const uint8_t*)b;
    for (uint32_t i = 0; i < n; i++) s->mem[a + i] &= p[i];
    s->progs++; return 0;
}
static int sif_erase(mcs_flash_t* f, uint32_t blk) {
    sim_iflash_t* s = (sim_iflash_t*)f->ctx;
    if (blk >= f->block_count) return MCS_FLASH_EINVAL;
    uint32_t w = f->write_size ? f->write_size : 1;
    memset(s->mem + (size_t)blk * f->block_size, 0xFF, f->block_size);
    memset(s->programmed + (size_t)blk * f->block_size / w, 0, f->block_size / w);
    s->erases++; return 0;
}
static void sim_iflash_init(sim_iflash_t* s, uint32_t block_size, uint32_t blocks, uint32_t write_size) {
    memset(s, 0, sizeof *s);
    size_t size = (size_t)block_size * blocks;
    s->mem = (uint8_t*)malloc(size); memset(s->mem, 0xFF, size);
    s->programmed = (uint8_t*)calloc(size / (write_size ? write_size : 1), 1);
    s->flash.type = MCS_FLASH_NOR; s->flash.page_size = 256; s->flash.block_size = block_size;
    s->flash.block_count = blocks; s->flash.write_size = write_size;
    s->flash.read = sif_read; s->flash.prog = sif_prog; s->flash.erase = sif_erase; s->flash.ctx = s;
}
static void sim_iflash_free(sim_iflash_t* s) { free(s->mem); free(s->programmed); }
#endif
