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
* `mcs_vfs_statfs(&vfs, path, &st)` gives the size (`st.total`), free bytes (`st.free`),
  format and mount prefix of the filesystem holding `path` — C# `DriveInfo`, shell `df`.
  A backend reports it through the optional `statfs` op (all built-in backends have one;
  custom backends may leave it NULL → `MCS_VFS_EINVAL`).

## Backends
| Backend | Use | Notes |
|---|---|---|
| `mcs_ramfs_ops` | tests, scratch, MCUs without flash FS | byte quota, custom allocator (e.g. the VM pool), ≤64 entries listed per dir |
| `mcs_posixfs_ops` | host CLI (`--fs DIR`); ESP-IDF VFS mounts | confined to the directory; sorted listings. On ESP32 `mcs_esp32_littlefs()` uses it on top of the `joltwallet/littlefs` component (LittleFS on a flash partition, [ports/esp32](../ports/esp32/README.md#files-on-flash-littlefs)) |
| `mcs_lfs_ops` | MCU flash (NOR, NAND) | LittleFS v2; `lfs_t` owned by the port; atomic rename; file handles from a fixed table (`MCS_LFS_MAX_FILES`=4, process-wide); LittleFS allocates its per-file cache with `lfs_malloc` unless built with `LFS_NO_MALLOC` |
| `mcs_tinyfs_ops` | the MCU's own flash (a few KB), small SPI NOR | **TinyFS**, built into MicroCS (`MCS_ENABLE_TINYFS=1`, MIT, no extra sources, ~5 KB of Thumb-2 code); power-fail safe log of CRC'd records, wear levelling across the erase blocks, RAM index of `MCS_TINYFS_MAX_FILES`=16 entries; ctx = `mcs_tinyfs_t*` |
| `mcs_yaffs_ops` | MCU flash, NAND first (also NOR) | YAFFS2 "direct"; ctx = a mounted `struct yaffs_dev*`; uses the `*_reldev` API so the VFS prefix and YAFFS device name are independent; bad-block management, wear levelling; **GPLv2** (or commercial licence from Aleph One) |

TinyFS is part of MicroCS (`modules/fs/mcs_vfs_tinyfs.c`). LittleFS and YAFFS2 are not
bundled: MicroCS only includes their headers (`lfs.h`, `yaffsfs.h`/`yaffs_guts.h`) and
you add their sources to your build.

| | TinyFS (`MCS_ENABLE_TINYFS=1`) | LittleFS (`MCS_ENABLE_LFS=1`) | YAFFS2 (`MCS_ENABLE_YAFFS=1`) |
|---|---|---|---|
| Licence | MIT (MicroCS) | BSD-3-Clause | GPLv2 or commercial |
| Best for | the MCU's own flash: 4–64 KB of a 64 KB–2 MB part (STM32 without an external chip), small SPI NOR | SPI NOR, small NAND partitions | SPI/parallel NAND, large partitions |
| Sources to add | none | `lfs.c lfs_util.c` | yaffs2 `direct/` + `core/` (copied with `direct/handle_common.sh copy`, or the `make yaffs-test` recipe) |
| Defines | – | – | `CONFIG_YAFFS_DIRECT CONFIG_YAFFS_YAFFS2 CONFIG_YAFFS_PROVIDE_DEFS CONFIG_YAFFSFS_PROVIDE_VALUES CONFIG_YAFFS_DEFINES_TYPES` |
| OS glue | – | – | `yaffs_osglue.h` functions; `-DMCS_YAFFS_OSGLUE=1` compiles a single-threaded one (malloc, no locks) |
| Smallest region | 2 erase blocks (e.g. 8 KB = 8 pages of 1 KB); with n blocks about (n−1)/n holds data | ~4 blocks of ≥ 4 KB | 6 erase blocks |
| Power loss | every record CRC'd; garbage collection finishes with a commit record — the test cuts power at every flash operation | copy-on-write metadata | log-structured |
| Limits | files written sequentially (create/truncate, then append); ≤ 32 erase blocks; paths ≤ 127 bytes | – | – |
| Bad blocks | – (NOR) | reported as `LFS_ERR_CORRUPT` → LittleFS relocates; failing blocks get marked bad. Partition blocks 0 and 1 (superblock) must be good | full NAND bad-block management (factory markers, retire on erase/program failure) |
| RAM | static: `mcs_tinyfs_t` ≈ 24 B × `MCS_TINYFS_MAX_FILES` + (`MCS_TINYFS_CHUNK` + 16) × `MCS_TINYFS_HANDLES` + ~0.4 KB (1064 bytes with the defaults on Cortex-M), no heap | read + prog cache (one page each on NAND) + one cache per open file | heap grows with partition size: ~65 KB high-water for the test's two 8 MB NAND + one 2 MB NOR partitions (`MCS_YAFFS_CACHES`=4) |

TinyFS options: `MCS_TINYFS_MAX_FILES` (16, files + directories), `MCS_TINYFS_CHUNK` (128,
write buffer per open file = largest data record), `MCS_TINYFS_HANDLES` (2, files open at
the same time), `MCS_TINYFS_MAX_UNIT` (64, largest flash program unit). How much of the
internal flash it gets: `MCS_INTFLASH_SIZE` (bytes; CMake `-DMICROCS_FS_SIZE=8192`; 0 = the
port's default region).

## Files on the chip's own flash (every port)

Every port ships an internal-flash driver that fills an `mcs_flash_t`, and
`mcs_flashfs_mount()` (`modules/fs/mcs_flashfs.c`) puts LittleFS or YAFFS2 on it
(or TinyFS) on it in one call — formatting a blank partition the first time:

```c
static mcs_flashfs_t fs;
if (mcs_flashfs_mount(&fs, &my_flash, 0, 0, MCS_FLASHFS_DEFAULT, MCS_FLASHFS_FORMAT_IF_NEEDED) == 0) {
    cfg.fs_ops = fs.ops; cfg.fs_ctx = fs.ctx;          /* mcs_runtime_cfg_t, or mcs_vfs_mount(&vfs, "/", fs.ops, fs.ctx, 0) */
}
```

| Port | Driver | Default region | Notes |
|---|---|---|---|
| RP2040 / RP2350 | `mcs_rp2_flash_init(&f, 0, 0)` | top of the QSPI flash: `MCS_RP2_FS_SIZE` (= `MCS_INTFLASH_SIZE` when set; else last 1 MB on 2 MB boards, all but 1 MB on ≥4 MB boards) | 4 KB sectors, 256-byte pages; erase/program run through `flash_safe_execute` (interrupts and the other core paused) |
| STM32 (every family: F0–F7, G0, G4, H5, H7, L0–L5, U0, U5, C0, WB, WL) | `mcs_stm32_flash_init(&f, 0, 0)` | `MCS_INTFLASH_SIZE` bytes at the top of the flash (`MCS_STM32_FS_SIZE`; default a quarter of the flash, ≥2 erase units), refused if it overlaps the firmware; with TinyFS alone each flash page is one block (8 KB = 8 × 1 KB pages on F1), `MCS_STM32_FS_BLOCK` overrides | 2–8 KB pages, or 128/256 KB sectors on F2/F4/F7/H7; the program unit (8/16/32-byte flash words) is reported in `write_size`; L0/L1 flash erases to 0x00, the driver inverts |
| ESP32 (all) | `mcs_esp32_partition_flash(&f, "storage")` | a data partition from the partition table | `mcs_esp32_flash_fs()` picks YAFFS2 when it is compiled in (`MICROCS_FS` = YAFFS2 in menuconfig), else the `esp_littlefs` component |
| Zephyr | `mcs_zephyr_flash_area_init(&f, FIXED_PARTITION_ID(storage_partition))` | `storage_partition` | `mcs_zephyr_fs_mount()` (Zephyr's own LittleFS) stays the default; `-DEXTRA_CONF_FILE=overlay-yaffs2.conf` (`CONFIG_MICROCS_YAFFS2`) uses YAFFS2 through the driver |
| Arduino | the core's `LittleFS` / `SD` (`fs::FS`) | core partition | `mcs_arduino_fs.cpp` |
| any board | `mcs_spinor_init` / `mcs_spinand_init` | external SPI chip | below |

`mcs_flash_t.write_size` tells the LittleFS adapter the smallest program unit
(`prog_size` = max(16, `write_size`)) so ECC flash that can be written only once
per flash word (STM32 L4/G4/H7, ...) works; YAFFS2 writes whole chunks and needs at
least 6 erase blocks.

**Choosing the filesystem at build time** — like LittleFS and YAFFS2, TinyFS is one
choice (the port examples default to LittleFS; `MCS_FLASHFS_DEFAULT` is the first one
compiled in: LittleFS, YAFFS2, TinyFS):

| Build | TinyFS | LittleFS | YAFFS2 |
|---|---|---|---|
| CMake (`add_subdirectory(MicroCS)`, Pico SDK, STM32CubeIDE CMake) | `-DMICROCS_FS=tinyfs` (+ `-DMICROCS_FS_SIZE=8192`) | `-DMICROCS_FS=littlefs` | `-DMICROCS_FS=yaffs2` |
| ESP-IDF | menuconfig → MicroCS → *Filesystem on the "storage" partition* → TinyFS (`CONFIG_MICROCS_FS_TINYFS=y`) | → LittleFS | → YAFFS2 (`CONFIG_MICROCS_FS_YAFFS2=y`) |
| Zephyr | `-DEXTRA_CONF_FILE=overlay-tinyfs.conf` | default (Zephyr's own LittleFS module) | `-DEXTRA_CONF_FILE=overlay-yaffs2.conf` |
| Arduino IDE | `tools/make_arduino.py --fs tinyfs` | the core's `LittleFS` (ESP32, RP2040) via `mcs_arduino_fs`; other boards: `tools/make_arduino.py --fs littlefs` | `tools/make_arduino.py --fs yaffs2` |
| Makefile / other | `MCS_ENABLE_TINYFS 1` (in `mcs_user_config.h` or `-D`), nothing to add | add `lfs.c lfs_util.c`, `MCS_ENABLE_LFS 1` | add yaffs2 `direct/` + `core/`, `MCS_ENABLE_YAFFS 1` + the defines below |

```c
/* STM32F103C8 (64 KB flash, no external chip): 8 KB at the top for files */
#define MCS_ENABLE_TINYFS 1        /* mcs_user_config.h, or -DMICROCS_FS=tinyfs */
#define MCS_INTFLASH_SIZE 8192     /* or -DMICROCS_FS_SIZE=8192 */

static mcs_stm32_flash_t flash;
static mcs_flashfs_t fs;
if (mcs_stm32_flash_init(&flash, 0, 0) == 0 &&               /* top MCS_INTFLASH_SIZE bytes */
    mcs_flashfs_mount(&fs, &flash.flash, 0, 0, MCS_FLASHFS_TINYFS, MCS_FLASHFS_FORMAT_IF_NEEDED) == 0) {
    cfg.fs_ops = fs.ops; cfg.fs_ctx = fs.ctx;
}
```

On page-flash STM32s (F0/F1/F3/G0/G4/L0/L1/L4/L5/U0/U5/C0/WB/WL) TinyFS uses each 1–8 KB page
as a block, so 8 KB is plenty for a few scripts. On F2/F4/F7 the smallest erasable unit of the
top of the flash is a 128/256 KB sector, so TinyFS there needs two of them (or an SPI NOR).

### Where the sources come from

TinyFS needs nothing. LittleFS and YAFFS2 are not bundled with MicroCS, and **nothing is
downloaded unless you ask for it.** `cmake/MicroCSFS.cmake` (CMake, pico-sdk, STM32CubeMX, ESP-IDF's YAFFS2,
Zephyr's YAFFS2) looks in this order:

1. `MICROCS_LITTLEFS_DIR` / `MICROCS_YAFFS2_DIR` — a CMake or environment variable with the
   folder holding `lfs.c` + `lfs.h`, or the yaffs2 checkout with `direct/` and `core/`
   (configuring stops if the folder does not have them);
2. a copy next to your project or MicroCS: `<project>/littlefs`, `lib/`, `libs/`,
   `third_party/`, `thirdparty/`, `external/`, `extern/`, `vendor/`, `deps/`, `components/`,
   `Middlewares/Third_Party/` and `<project>/..`; `<MicroCS>/third_party/` and `<MicroCS>/..`;
   the west workspace (`modules/fs/littlefs`); `MicroCS/build/third_party` (`make fetch-lfs` /
   `make fetch-yaffs`) — the same names with `yaffs2`;
3. an earlier download in `<build>/_microcs_deps`;
4. with `-DMICROCS_FS_DOWNLOAD=ON` (or the environment variable `MICROCS_FS_DOWNLOAD=1`, handy
   for `idf.py`): download LittleFS v2.9.3 / the pinned yaffs2 revision once into
   `<build>/_microcs_deps`.

If none applies, configuring stops and tells you which variable to set. The configure
log names the folder used (`-- MicroCS: LittleFS from ...`); any LittleFS v2.x works.

```sh
git clone -b v2.9.3 https://github.com/littlefs-project/littlefs ~/src/littlefs
cmake -B build -DPICO_BOARD=pico -DMICROCS_LITTLEFS_DIR=~/src/littlefs    # or: -DMICROCS_FS_DOWNLOAD=ON
```

ESP-IDF's LittleFS comes from the `joltwallet/littlefs` component (`idf_component.yml`,
fetched by the IDF component manager) and Zephyr's from its `littlefs` module, so only
their YAFFS2 option uses the lookup above.

### Every port, and new MCUs

| Port | TinyFS | LittleFS | YAFFS2 | Flash driver |
|---|---|---|---|---|
| RP2040 / RP2350 (pico-sdk) | ✓ `-DMICROCS_FS=tinyfs` | ✓ `-DMICROCS_FS=littlefs` (example default) | ✓ `-DMICROCS_FS=yaffs2` | `mcs_rp2_flash_init` |
| STM32 (CubeMX CMake / Make) | ✓ (best for a few KB: one page per block) | ✓ | ✓ (better on 128 KB sectors) | `mcs_stm32_flash_init` |
| ESP32 (ESP-IDF) | ✓ menuconfig | ✓ esp_littlefs component (default) | ✓ menuconfig | `mcs_esp32_partition_flash` |
| Zephyr | ✓ `overlay-tinyfs.conf` | ✓ Zephyr LittleFS (default) | ✓ `overlay-yaffs2.conf` | `mcs_zephyr_flash_area_init` |
| Arduino | ✓ (`--fs tinyfs`) | ✓ core LittleFS / SD, or bundled (`--fs littlefs`, boards whose core has no LittleFS) | ✓ bundled (`--fs yaffs2`) | any `mcs_flash_t` (SPI NOR / NAND drivers) |
| any other MCU (`ports/template`) | ✓ | ✓ | ✓ | a flash port: `read` / `write` / `erase` in a `mcs_flash_port_t` (below) |

The three adapters only talk to `mcs_flash_t`, so a new MCU needs nothing
filesystem-specific. For its internal flash, fill in a **flash port** — the region size,
erase unit, program unit and three functions — and `mcs_intflash_init()` turns it into
a `mcs_flash_t` (`include/mcs_flash.h`):

```c
static int my_read(void* c, uint32_t off, void* b, uint32_t n)        { memcpy(b, (const void*)(MY_FS_BASE + off), n); return 0; }
static int my_write(void* c, uint32_t off, const void* b, uint32_t n) { return my_flash_program(MY_FS_BASE + off, b, n); }
static int my_erase(void* c, uint32_t off)                            { return my_flash_erase_page(MY_FS_BASE + off); }
static const mcs_flash_port_t port = { 8 * 1024, 1024, 4, my_read, my_write, my_erase, NULL };  /* size, erase, write unit */
static mcs_intflash_t flash;
static mcs_flashfs_t fs;

mcs_intflash_init(&flash, &port);
mcs_flashfs_mount(&fs, &flash.flash, 0, 0, MCS_FLASHFS_TINYFS, MCS_FLASHFS_FORMAT_IF_NEEDED);
```

Offsets are relative to the region; return 0 or a negative error. `write()` gets offsets
and lengths that are multiples of the program unit, each unit is written once between
erases, and erased flash reads 0xFF (invert in `read`/`write` if yours erases to 0x00).
LittleFS and YAFFS2 run on the same adapter. The skeleton is in
`ports/template/mcs_port_template.c`; the STM32, RP2, ESP32 and Zephyr ports already have
theirs. On Arduino the core's own LittleFS
(ESP32, RP2040) already links `lfs_*`; do not bundle a second copy there (use
`mcs_arduino_fs` or `--fs yaffs2`).

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
* Both tests also run the `mcs_flashfs_mount()` path on simulated *internal*
  flash (`sim_iflash_t`): an L4-like part (2 KB pages, 8-byte double words), an
  H7-like part (128 KB sectors, 32-byte flash words) and an RP2-like part (4 KB
  sectors, 256-byte pages), each checking that a program unit is never written
  twice between erases.
* `make tinyfs-test` (part of `make test`, nothing to download) runs
  `tests/c/test_tinyfs.c` on simulated internal flash reached through
  `mcs_intflash_init()` (1 KB pages with 2-byte units like an F1, 2 KB / 8-byte like
  an L4, 0x00-erased like an L0, ...): the C# File/Directory API, flash physics
  (program only clears bits, each unit programmed once per erase), a randomised model
  check against a RAM copy, wear spread across the blocks, and a **power cut at every
  single program/erase step** of a long scenario with garbage collection, each followed
  by a remount and a consistency check (~900 checks, a few seconds).

CI builds the port examples with the filesystem on internal flash (Pico /
Pico 2, ESP-IDF targets, 18 STM32 boards with LittleFS, YAFFS2 and TinyFS) and runs the
Zephyr `native_sim` firmware with LittleFS and with YAFFS2. For LittleFS and YAFFS2,
power-cut testing relies on their own power-loss guarantees.

## C# API
`File`: ReadAllText, WriteAllText, AppendAllText, ReadAllLines, WriteAllLines,
AppendAllLines, ReadAllBytes, WriteAllBytes, Exists, Delete, Copy(src, dst[, overwrite]),
Move, GetLength (MicroCS extension; .NET uses `FileInfo.Length`).
`Directory`: Exists, CreateDirectory (recursive), GetFiles, GetDirectories,
GetFileSystemEntries, Delete(path[, recursive]), GetCurrentDirectory (always `/`).
`Path`: Combine, GetFileName, GetExtension, GetFileNameWithoutExtension,
GetDirectoryName, GetFullPath.
`DriveInfo`: `new DriveInfo("/")` (any path; describes the mount that holds it),
`DriveInfo.GetDrives()` (one per mount); properties TotalSize, AvailableFreeSpace,
TotalFreeSpace, DriveFormat (`littlefs`, `yaffs2`, `ramfs`, `posix`), Name, IsReady.
Values are read live from the backend, in bytes:
```csharp
var d = new DriveInfo("/");
Console.WriteLine($"{d.AvailableFreeSpace / 1024} of {d.TotalSize / 1024} KB free ({d.DriveFormat})");
```
LittleFS counts whole blocks (4 KB on ESP32), so free space moves in block steps and
includes a little metadata overhead. A RAM filesystem without a quota reports 2 GB.
Errors map to .NET types and messages: FileNotFoundException,
DirectoryNotFoundException, UnauthorizedAccessException, IOException,
DriveNotFoundException.
There are no streams (`FileStream`) yet — whole-file operations only (planned).
