/*
 * MicroCS - raw flash device layer (optional, MCS_ENABLE_FLASH; default = MCS_ENABLE_FS).
 *
 * One small struct describes a NOR or NAND chip; the LittleFS and YAFFS2
 * adapters (mcs_vfs.h) and the generic SPI drivers below all speak it, so a
 * filesystem can be put on any flash with a few lines of glue:
 *
 *   mcs_spinor_t nor;                                      SPI NOR (W25Q, MX25, GD25, IS25, ...)
 *   mcs_spinor_init(&nor, my_spi_xfer, &bus, 0, 4096);     size from the JEDEC id, 4 KB sectors
 *   mcs_flash_part_t part = { &nor.flash, 0, 0 };          whole chip
 *   struct lfs_config cfg; mcs_lfs_flash_config(&cfg, &part);
 *   lfs_mount(&lfs, &cfg) ...  mcs_vfs_mount(&vfs, "/flash", &mcs_lfs_ops, &lfs, 0);
 *
 *   mcs_spinand_t nand;                                    SPI NAND (W25N, MT29F, GD5F, ...)
 *   mcs_spinand_init(&nand, my_spi_xfer, &bus, 0);
 *   mcs_flash_part_t np = { &nand.flash, 0, 0 };
 *   mcs_yaffs_flash_dev(&dev, &np, "/nand");                YAFFS2 (or LittleFS) on it
 *   yaffs_mount("/nand"); mcs_vfs_mount(&vfs, "/nand", &mcs_yaffs_ops, &dev, 0);
 *
 * Addresses: NOR read/prog take a byte address. On NAND, read/prog take
 * page * page_size + column (whole pages for prog) and read_page/prog_page
 * give access to the spare (OOB) area. erase takes a block number.
 * Every function returns 0 (or the byte count where noted) on success and a
 * negative MCS_FLASH_E* code on failure.
 */
#ifndef MCS_FLASH_H
#define MCS_FLASH_H
#include "mcs.h"
#ifdef __cplusplus
extern "C" {
#endif

enum {
    MCS_FLASH_OK = 0,
    MCS_FLASH_EIO = -1,       /* bus error / timeout                          */
    MCS_FLASH_EPROG = -2,     /* program or erase reported failure (wear-out) */
    MCS_FLASH_EECC = -3,      /* uncorrectable ECC error on read              */
    MCS_FLASH_EBAD = -4,      /* block is marked bad                          */
    MCS_FLASH_EINVAL = -5,    /* address/size out of range or misaligned      */
    MCS_FLASH_ENODEV = -6     /* no chip answered / unknown id                */
};
#define MCS_FLASH_FIXED 1     /* read_page: data OK, ECC corrected bit errors */

#define MCS_FLASH_NOR  0
#define MCS_FLASH_NAND 1

typedef struct mcs_flash mcs_flash_t;
struct mcs_flash {
    uint8_t type;             /* MCS_FLASH_NOR / MCS_FLASH_NAND */
    uint32_t page_size;       /* program unit: NOR page (256), NAND page (2048) */
    uint32_t spare_size;      /* NAND spare bytes per page (0 on NOR) */
    uint32_t block_size;      /* erase unit in bytes (NOR sector, NAND block) */
    uint32_t block_count;
    int (*read)(mcs_flash_t* f, uint32_t addr, void* buf, uint32_t n);
    int (*prog)(mcs_flash_t* f, uint32_t addr, const void* buf, uint32_t n);
    int (*erase)(mcs_flash_t* f, uint32_t block);
    int (*sync)(mcs_flash_t* f);                                   /* may be NULL */
    /* NAND only (NULL on NOR). spare_off/spare_n select bytes of the spare area. */
    int (*read_page)(mcs_flash_t* f, uint32_t page, void* data, uint32_t n,
                     void* spare, uint32_t spare_off, uint32_t spare_n); /* 0, MCS_FLASH_FIXED or <0 */
    int (*prog_page)(mcs_flash_t* f, uint32_t page, const void* data, uint32_t n,
                     const void* spare, uint32_t spare_off, uint32_t spare_n);
    int (*is_bad)(mcs_flash_t* f, uint32_t block);                 /* 1 = bad */
    int (*mark_bad)(mcs_flash_t* f, uint32_t block);
    void* ctx;
    /* smallest program unit in bytes (0 = any size): internal MCU flash
     * programs whole half/double/quad/flash words exactly once per erase */
    uint32_t write_size;
};

/* A range of erase blocks handed to a filesystem (LittleFS / YAFFS2 adapters).
 * block_count = 0 means "to the end of the chip". Must outlive the mount. */
typedef struct { mcs_flash_t* flash; uint32_t first_block; uint32_t block_count; } mcs_flash_part_t;

/* ---- SPI bus glue ----
 * One chip-select-framed transaction: send ncmd bytes of cmd, then either
 * send n bytes of tx (rx == NULL) or receive n bytes into rx (tx == NULL).
 * Return 0 on success. */
typedef int (*mcs_spi_xfer_fn)(void* ud, const uint8_t* cmd, size_t ncmd,
                               const uint8_t* tx, uint8_t* rx, size_t n);
/* optional: called while waiting for the chip (sleep / yield). */
typedef void (*mcs_flash_wait_fn)(void* ud, uint32_t us);

/* ---- generic SPI NOR (JEDEC 9Fh id, 03h read, 02h page program, 20h/D8h erase).
 * size = 0 takes the capacity from the JEDEC id; erase_size = 4096 (20h) or
 * 65536 (D8h). Chips over 16 MB use the 4-byte-address opcodes. */
typedef struct {
    mcs_flash_t flash;
    mcs_spi_xfer_fn xfer;
    void* ud;
    mcs_flash_wait_fn wait;   /* NULL = busy-poll */
    uint32_t jedec_id;        /* manufacturer << 16 | type << 8 | capacity */
    uint32_t size;
    uint8_t addr4;
    uint8_t erase_op;
} mcs_spinor_t;
int mcs_spinor_init(mcs_spinor_t* d, mcs_spi_xfer_fn xfer, void* ud, uint32_t size, uint32_t erase_size);

/* ---- generic SPI NAND (W25N01GV/W25N02KV, MT29F1G01, GD5F1GQ, TC58CVG, XT26G ...):
 * 13h page read to cache, 03h read cache, 02h program load, 10h program
 * execute, D8h block erase, 0Fh/1Fh features; on-die ECC on, block protection
 * cleared. blocks = 0 picks 2048 for known 2 Gbit ids, else 1024 (1 Gbit,
 * 2048 + 64 byte pages, 64 pages per block). Factory bad blocks are marked by
 * a non-FF first spare byte of the block's first page. */
typedef struct {
    mcs_flash_t flash;
    mcs_spi_xfer_fn xfer;
    void* ud;
    mcs_flash_wait_fn wait;
    uint32_t jedec_id;        /* manufacturer << 16 | device id */
    uint8_t col_dummy_first;  /* 1: 03h <dummy> <col> (some GigaDevice parts) */
    uint32_t loaded_page;     /* page currently in the chip's cache (~0 = none) */
    int8_t loaded_ecc;        /* its ECC status */
} mcs_spinand_t;
int mcs_spinand_init(mcs_spinand_t* d, mcs_spi_xfer_fn xfer, void* ud, uint32_t blocks);

#if MCS_ENABLE_HAL
/* SPI glue over a MicroCS HAL: bus + GPIO chip select (active low). */
#include "mcs_hal.h"
typedef struct { const mcs_hal_t* hal; int bus; int cs_pin; } mcs_flash_hal_spi_t;
int mcs_flash_hal_xfer(void* ud, const uint8_t* cmd, size_t ncmd, const uint8_t* tx, uint8_t* rx, size_t n);
#endif

#ifdef __cplusplus
}
#endif
#endif
