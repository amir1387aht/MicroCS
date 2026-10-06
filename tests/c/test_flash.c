/* MicroCS flash layer tests: generic SPI NOR / SPI NAND drivers against the
 * simulated chips in flash_sim.h (no third-party code; part of `make test`). */
#include <stdio.h>
#include "flash_sim.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL %s\n", what); } } while (0)

static void test_nor(void) {
    sim_nor_t s; mcs_spinor_t d;
    sim_nor_init(&s, 2u << 20, 0x15, 0x7);                 /* 2 MB, powered up write-protected */
    memset(&d, 0, sizeof d);
    CHECK(mcs_spinor_init(&d, sim_nor_xfer, &s, 0, 4096) == 0, "nor: init from JEDEC id");
    CHECK(d.jedec_id == 0xEF4015 && d.size == (2u << 20) && !d.addr4, "nor: id + size");
    CHECK(!(s.sr & 0x3C), "nor: block protection cleared");
    mcs_flash_t* f = &d.flash;
    CHECK(f->type == MCS_FLASH_NOR && f->block_size == 4096 && f->block_count == 512 && f->page_size == 256, "nor: geometry");
    static uint8_t buf[1000], back[1000];
    for (int i = 0; i < 1000; i++) buf[i] = (uint8_t)(i * 7 + 3);
    CHECK(f->erase(f, 1) == 0, "nor: erase sector");
    CHECK(f->prog(f, 4096 + 200, buf, 1000) == 0, "nor: program across page boundaries");
    CHECK(f->read(f, 4096 + 200, back, 1000) == 0 && !memcmp(buf, back, 1000), "nor: read back");
    CHECK(s.progs == 5, "nor: split into 5 page programs");
    CHECK(f->read(f, 4096, back, 200) == 0 && back[0] == 0xFF && back[199] == 0xFF, "nor: bytes before stay erased");
    CHECK(f->erase(f, 1) == 0 && f->read(f, 4096 + 200, back, 4) == 0 && back[0] == 0xFF, "nor: erase resets to FF");
    CHECK(s.overwrite_violations == 0 && s.unlocked_writes == 0, "nor: never programs 0->1, always WREN");
    CHECK(f->read(f, (2u << 20) - 2, back, 4) == MCS_FLASH_EINVAL, "nor: out of range rejected");
    CHECK(f->erase(f, 512) == MCS_FLASH_EINVAL, "nor: erase out of range rejected");
    free(s.mem);

    /* 32 MB part: 4-byte addressing, 64 KB blocks */
    sim_nor_init(&s, 32u << 20, 0x19, 0);
    memset(&d, 0, sizeof d);
    CHECK(mcs_spinor_init(&d, sim_nor_xfer, &s, 0, 65536) == 0 && d.addr4 && f->block_count == 512, "nor: 32 MB uses 4-byte addresses");
    CHECK(f->erase(f, 400) == 0 && f->prog(f, 400u * 65536 + 17, buf, 300) == 0 &&
          f->read(f, 400u * 65536 + 17, back, 300) == 0 && !memcmp(buf, back, 300), "nor: program/read above 16 MB");
    CHECK(s.mem[400u * 65536 + 17] == buf[0], "nor: lands at the right 32-bit address");
    free(s.mem);

    sim_nor_init(&s, 1u << 20, 0x14, 0);
    s.jedec[0] = 0xFF;
    memset(&d, 0, sizeof d);
    CHECK(mcs_spinor_init(&d, sim_nor_xfer, &s, 0, 4096) == MCS_FLASH_ENODEV, "nor: no chip -> ENODEV");
    CHECK(mcs_spinor_init(&d, sim_nor_xfer, &s, 1u << 20, 4096) == 0 && d.flash.block_count == 256, "nor: explicit size skips the id");
    free(s.mem);
}

static void test_nand(void) {
    sim_nand_t s; mcs_spinand_t d;
    sim_nand_init(&s, 64);
    sim_nand_factory_bad(&s, 5);
    memset(&d, 0, sizeof d);
    CHECK(mcs_spinand_init(&d, sim_nand_xfer, &s, 64) == 0, "nand: init");
    CHECK(d.jedec_id == 0xEFAA21 && s.prot == 0 && (s.cfg & 0x10), "nand: id, unprotected, ECC on");
    mcs_flash_t* f = &d.flash;
    CHECK(f->type == MCS_FLASH_NAND && f->page_size == 2048 && f->spare_size == 64 && f->block_size == 131072 &&
          f->block_count == 64, "nand: geometry");
    static uint8_t page[2048], back[2048];
    uint8_t spare[16], sback[16];
    for (int i = 0; i < 2048; i++) page[i] = (uint8_t)(i ^ 0x5A);
    for (int i = 0; i < 16; i++) spare[i] = (uint8_t)(0xA0 + i);
    CHECK(f->erase(f, 2) == 0, "nand: erase block");
    CHECK(f->prog_page(f, 2 * 64, page, 2048, spare, 4, 16) == 0, "nand: program page + spare");
    CHECK(f->read_page(f, 2 * 64, back, 2048, sback, 4, 16) == 0 && !memcmp(page, back, 2048) && !memcmp(spare, sback, 16),
          "nand: read page + spare");
    CHECK(f->read_page(f, 2 * 64, NULL, 0, sback, 4, 16) == 0 && !memcmp(spare, sback, 16), "nand: spare-only read");
    CHECK(f->read(f, 2 * 131072 + 100, back, 300) == 0 && !memcmp(page + 100, back, 300), "nand: byte read inside a page");
    static uint8_t two[4096], back2[1000];
    for (int i = 0; i < 4096; i++) two[i] = (uint8_t)(i * 13 + i / 256);
    CHECK(f->prog(f, 2 * 131072 + 2048, two, 4096) == 0, "nand: byte-addressed program of 2 pages");
    CHECK(f->read(f, 2 * 131072 + 4000, back2, 1000) == 0 && !memcmp(two + 4000 - 2048, back2, 1000), "nand: byte read across pages");
    CHECK(f->prog(f, 2 * 131072 + 3 * 2048 + 1, page, 16) == MCS_FLASH_EINVAL, "nand: unaligned program rejected");
    CHECK(f->is_bad(f, 5) == 1 && f->is_bad(f, 2) == 0, "nand: factory bad block detected");
    s.worn[7] = 1;
    CHECK(f->erase(f, 7) == MCS_FLASH_EPROG, "nand: erase failure -> EPROG");
    CHECK(f->prog_page(f, 7 * 64, page, 2048, NULL, 0, 0) == MCS_FLASH_EPROG, "nand: program failure -> EPROG");
    CHECK(f->mark_bad(f, 7) == 0 && f->is_bad(f, 7) == 1, "nand: mark bad");
    s.flip_page = 2 * 64; s.flip_bits = 2;
    d.loaded_page = ~0u;
    CHECK(f->read_page(f, 2 * 64, back, 2048, NULL, 0, 0) == MCS_FLASH_FIXED && !memcmp(page, back, 2048), "nand: corrected bit flips -> FIXED");
    s.flip_bits = 9; d.loaded_page = ~0u;
    CHECK(f->read_page(f, 2 * 64, back, 2048, NULL, 0, 0) == MCS_FLASH_EECC, "nand: uncorrectable -> EECC");
    d.loaded_page = ~0u;
    CHECK(f->read(f, 2 * 131072, back, 16) == MCS_FLASH_EECC, "nand: byte read reports EECC");
    s.flip_page = -1;
    long r0 = s.reads;
    d.loaded_page = ~0u;
    f->read(f, 2 * 131072, back, 16); f->read(f, 2 * 131072 + 16, back, 16);
    CHECK(s.reads - r0 == 1, "nand: cached page not re-read");
    CHECK(s.nop_violations == 0 && s.order_violations == 0 && s.overwrite_violations == 0, "nand: one in-order program per page");
    CHECK(f->read_page(f, 64 * 64, back, 16, NULL, 0, 0) == MCS_FLASH_EINVAL, "nand: page out of range");
    free(s.mem); free(s.prog_count); free(s.next_page); free(s.worn);
}

#if MCS_ENABLE_HAL
static uint8_t g_sent[64]; static size_t g_nsent; static int g_cs = 1, g_cs_edges;
static int fake_gpio(void* ctx, int pin, int v) { (void)ctx; if (pin == 9) { g_cs = v; g_cs_edges++; } return 0; }
static int fake_spi(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n) {
    (void)ctx; (void)bus;
    if (g_cs) return -1;
    for (size_t i = 0; i < n; i++) { if (g_nsent < sizeof g_sent) g_sent[g_nsent++] = tx[i]; rx[i] = (uint8_t)(0x40 + i); }
    return (int)n;
}
static void test_hal_bridge(void) {
    mcs_hal_t h; memset(&h, 0, sizeof h);
    h.gpio_write = fake_gpio; h.spi_transfer = fake_spi;
    mcs_flash_hal_spi_t b = { &h, 1, 9 };
    uint8_t cmd[2] = { 0x0F, 0xC0 }, rx[3];
    CHECK(mcs_flash_hal_xfer(&b, cmd, 2, NULL, rx, 3) == 0, "hal: transfer");
    CHECK(g_nsent == 5 && g_sent[0] == 0x0F && g_sent[1] == 0xC0 && g_sent[2] == 0xFF && rx[0] == 0x40, "hal: cmd then clocked-in bytes");
    CHECK(g_cs == 1 && g_cs_edges == 2, "hal: chip select framed");
}
#endif

int main(void) {
    test_nor();
    test_nand();
#if MCS_ENABLE_HAL
    test_hal_bridge();
#endif
    printf("flash: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
