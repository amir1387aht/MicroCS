# Filesystem (modules/fs, `MCS_ENABLE_FS`)

## VFS
```c
mcs_vfs_t vfs; mcs_vfs_init(&vfs);
static mcs_ramfs_t ram; mcs_ramfs_init(&ram, 32 * 1024, NULL, NULL);   /* quota, allocator */
mcs_vfs_mount(&vfs, "/", &mcs_ramfs_ops, &ram, 0);
mcs_vfs_mount(&vfs, "/flash", &mcs_lfs_ops, &lfs, 0);                 /* LittleFS          */
mcs_vfs_mount(&vfs, "/nand", &mcs_yaffs_ops, &yaffs_dev, 0);           /* YAFFS2            */
mcs_vfs_mount(&vfs, "/sys", &mcs_ramfs_ops, &sys, MCS_VFS_RDONLY | MCS_VFS_NOEXEC);
mcs_fs_open_lib(vm, &vfs);                                             /* C# File/Directory */
```
* Up to `MCS_VFS_MAX_MOUNTS` (4) mounts; longest prefix wins; mount points appear as
  directories in listings.
* Paths are normalised (`.`/`..`/`//`); `..` cannot climb above `/`, so scripts cannot
  reach host paths outside the mounted roots. Max length `MCS_VFS_PATH_MAX` (128).
* `MCS_VFS_RDONLY` rejects writes (UnauthorizedAccessException); `MCS_VFS_NOEXEC` lets
  scripts read files but `mcs_exec_file`/shell `run` refuse to execute them.
* Rename across mounts returns EINVAL (C# `IOException`).
* Whole-file helpers `mcs_vfs_read_file/write_file` and `mcs_exec_file` (source or image).

## Backends
| Backend | Use | Notes |
|---|---|---|
| `mcs_ramfs_ops` | tests, scratch, MCUs without flash FS | byte quota, custom allocator (e.g. the VM pool), ≤64 entries listed per dir |
| `mcs_posixfs_ops` | host CLI (`--fs DIR`); ESP-IDF VFS mounts | confined to the directory; sorted listings. On ESP32 `mcs_esp32_littlefs()` uses it on top of the `joltwallet/littlefs` component (LittleFS on a flash partition, [ports/esp32](../ports/esp32/README.md#files-on-flash-littlefs)) |
| `mcs_lfs_ops` | MCU flash (NOR, NAND) | LittleFS v2; `lfs_t` owned by the port; atomic rename; file handles from a fixed table (`MCS_LFS_MAX_FILES`=4, process-wide); LittleFS allocates its per-file cache with `lfs_malloc` unless built with `LFS_NO_MALLOC` |
| `mcs_yaffs_ops` | MCU flash, NAND first (also NOR) | YAFFS2 "direct"; ctx = a mounted `struct yaffs_dev*`; uses the `*_reldev` API so the VFS prefix and YAFFS device name are independent; bad-block management, wear levelling; **GPLv2** (or commercial licence from Aleph One) |

Neither LittleFS nor YAFFS2 is bundled: MicroCS only includes their headers
(`lfs.h`, `yaffsfs.h`/`yaffs_guts.h`) and you add their sources to your build.

| | LittleFS (`MCS_ENABLE_LFS=1`) | YAFFS2 (`MCS_ENABLE_YAFFS=1`) |
|---|---|---|
| Licence | BSD-3-Clause | GPLv2 or commercial |
| Best for | SPI NOR, small NAND partitions | SPI/parallel NAND, large partitions |
| Sources to add | `lfs.c lfs_util.c` | yaffs2 `direct/` + `core/` (copied with `direct/handle_common.sh copy`, or the `make yaffs-test` recipe) |
| Defines | – | `CONFIG_YAFFS_DIRECT CONFIG_YAFFS_YAFFS2 CONFIG_YAFFS_PROVIDE_DEFS CONFIG_YAFFSFS_PROVIDE_VALUES CONFIG_YAFFS_DEFINES_TYPES` |
| OS glue | – | `yaffs_osglue.h` functions; `-DMCS_YAFFS_OSGLUE=1` compiles a single-threaded one (malloc, no locks) |
| Bad blocks | reported as `LFS_ERR_CORRUPT` → LittleFS relocates; failing blocks get marked bad. Partition blocks 0 and 1 (superblock) must be good | full NAND bad-block management (factory markers, retire on erase/program failure) |
| RAM | read + prog cache (one page each on NAND) + one cache per open file | heap grows with partition size: ~65 KB high-water for the test's two 8 MB NAND + one 2 MB NOR partitions (`MCS_YAFFS_CACHES`=4) |

## Raw flash: NOR and NAND (`mcs_flash.h`, `MCS_ENABLE_FLASH`)
`mcs_flash_t` describes a chip (page, spare, erase-block size, block count;
`read`/`prog`/`erase`, plus `read_page`/`prog_page`/`is_bad`/`mark_bad` on NAND).
Both filesystem adapters and the generic SPI drivers speak it, so a filesystem
on external flash is a few lines of glue:

```c
/* 1. SPI transaction: CS low, send cmd, then send tx[n] or receive rx[n], CS high */
static int my_xfer(void* ud, const uint8_t* cmd, size_t ncmd, const uint8_t* tx, uint8_t* rx, size_t n);
/* or use the MicroCS HAL: mcs_flash_hal_spi_t bus = { &hal, 0, CS_PIN }; xfer = mcs_flash_hal_xfer */

/* 2a. SPI NOR (W25Q, MX25, GD25, IS25, SST26 ...) + LittleFS */
static mcs_spinor_t nor;
mcs_spinor_init(&nor, my_xfer, &bus, 0, 4096);          /* size from JEDEC id, 4 KB sectors */
static mcs_flash_part_t part = { &nor.flash, 0, 0 };    /* first block, count (0 = rest) */
static struct lfs_config cfg; static lfs_t lfs;
mcs_lfs_flash_config(&cfg, &part);
if (lfs_mount(&lfs, &cfg)) { lfs_format(&lfs, &cfg); lfs_mount(&lfs, &cfg); }
mcs_vfs_mount(&vfs, "/flash", &mcs_lfs_ops, &lfs, 0);

/* 2b. SPI NAND (W25N01/02, MT29F1G01, GD5F1GQ, TC58CVG ...) + YAFFS2 */
static mcs_spinand_t nand;
mcs_spinand_init(&nand, my_xfer, &bus, 0);              /* 2048+64 B pages, 64 pages/block */
static mcs_flash_part_t np = { &nand.flash, 0, 0 };
static struct yaffs_dev dev;
mcs_yaffs_flash_dev(&dev, &np, "/nand");                /* geometry + driver callbacks */
yaffs_mount("/nand");                                   /* formats a blank partition */
mcs_vfs_mount(&vfs, "/nand", &mcs_yaffs_ops, &dev, 0);
```

* **SPI NOR driver**: JEDEC `9Fh` id → capacity; `03h`/`02h`/`20h` or `D8h`;
  page programs split at 256-byte boundaries; chips above 16 MB use the 4-byte
  opcodes; power-up block protection is cleared (`98h` global unlock on SST26).
* **SPI NAND driver**: `13h` page read to cache + `03h` cache read (the last page
  stays cached), `02h`/`84h` program load + `10h` execute, `D8h` erase, status
  P-FAIL/E-FAIL → `MCS_FLASH_EPROG`, on-die ECC on (corrected → `MCS_FLASH_FIXED`,
  uncorrectable → `MCS_FLASH_EECC`), protection cleared. Set `col_dummy_first = 1`
  before init for parts that want `03h <dummy> <col>`; pass `blocks` for sizes
  other than 1 Gbit (W25N02KV is detected). Two-plane parts (MT29F2G) are not handled.
* **YAFFS2 layout**: NOR uses 512-byte chunks (2048 on 64 KB blocks) with in-band
  tags; NAND uses page-sized chunks with in-band tags by default
  (`MCS_YAFFS_NAND_INBAND=1`, every byte under the chip's data ECC). Set
  `dev.param.inband_tags = 0` after `mcs_yaffs_flash_dev` to keep tags in the spare
  area at `MCS_YAFFS_OOB_OFFSET` (2) — only if your chip's free OOB bytes are
  contiguous there (W25N with ECC on is not).
* Code size: drivers ~2.6 KB, LittleFS adapter ~0.9 KB (Cortex-M0, `-Os`).

### Tests
* `make test` runs `tests/c/test_flash.c`: both drivers against simulated chips
  (`tests/c/flash_sim.h` — real command sets, program-only-clears-bits, one
  in-order program per NAND page, factory and worn-out bad blocks, injected ECC
  errors, protection at power-up, 4-byte addressing).
* `make lfs-test` downloads LittleFS v2.9.3 and runs `tests/c/test_lfs.c`: RAM block
  device, then LittleFS on the simulated SPI NOR (1 MB partition) and SPI NAND
  (bad superblock block refused, factory bad blocks, a block wearing out) — C#
  File/Directory API, remount persistence ("reboot"), atomic replace via rename,
  device-full → `IOException`, space reclamation, write churn.
* `make yaffs-test` downloads a pinned yaffs2 revision (only the test binary links
  it) and runs `tests/c/test_yaffs.c`: YAFFS2 on the simulated SPI NAND with in-band
  and spare-area tags, a block that wears out mid-churn (retired, bad-block marker
  written), and on SPI NOR.

None of this has been run on real chips or under power-cut testing yet; the
designs rely on LittleFS's and YAFFS2's own power-loss guarantees.

## C# API
`File`: ReadAllText, WriteAllText, AppendAllText, ReadAllLines, WriteAllLines,
AppendAllLines, ReadAllBytes, WriteAllBytes, Exists, Delete, Copy(src, dst[, overwrite]),
Move, GetLength (MicroCS extension; .NET uses `FileInfo.Length`).
`Directory`: Exists, CreateDirectory (recursive), GetFiles, GetDirectories,
GetFileSystemEntries, Delete(path[, recursive]), GetCurrentDirectory (always `/`).
`Path`: Combine, GetFileName, GetExtension, GetFileNameWithoutExtension,
GetDirectoryName, GetFullPath.
Errors map to .NET types and messages: FileNotFoundException,
DirectoryNotFoundException, UnauthorizedAccessException, IOException.
There are no streams (`FileStream`) yet — whole-file operations only (planned).
