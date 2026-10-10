/*
 * MicroCS VFS backend for YAFFS2 "direct" (https://github.com/Aleph-One-Ltd/yaffs2).
 *
 * YAFFS2 is not bundled - and it is GPLv2 (or a commercial licence from Aleph
 * One): linking it into a firmware makes that firmware a derived work. Build
 * with MCS_ENABLE_YAFFS 1 (mcs_user_config.h or -D), the yaffs2 direct + core
 * sources and their usual defines (CONFIG_YAFFS_DIRECT CONFIG_YAFFS_YAFFS2 CONFIG_YAFFS_PROVIDE_DEFS
 * CONFIG_YAFFSFS_PROVIDE_VALUES CONFIG_YAFFS_DEFINES_TYPES); `make yaffs-test`
 * downloads a pinned revision into build/third_party and does exactly that.
 *
 *   struct yaffs_dev dev;  mcs_flash_part_t part = { &nand.flash, 0, 0 };
 *   mcs_yaffs_flash_dev(&dev, &part, "/nand");   geometry + driver (mcs_flash.h)
 *   yaffs_mount("/nand");                        (format happens on first mount)
 *   mcs_vfs_mount(&vfs, "/nand", &mcs_yaffs_ops, &dev, 0);
 *
 * ctx is the struct yaffs_dev; paths are passed to the *_reldev API, so the
 * VFS prefix and the YAFFS device name are independent. YAFFS needs an OS glue
 * layer (yaffs_osglue.h: lock, malloc, time, errno); MCS_YAFFS_OSGLUE 1
 * compiles a single-threaded one from this file.
 */
#include "mcs_vfs.h"
#if MCS_ENABLE_FS && MCS_ENABLE_YAFFS
#include <string.h>
#include <stdint.h>
#include <sys/types.h>     /* mode_t, off_t (newlib) before the yaffs headers */
#include "yaffsfs.h"
#include "yaffs_guts.h"
#include "yaffs_packedtags2.h"
#include "yaffs_trace.h"
#include "yaffs_osglue.h"

#ifndef MCS_YAFFS_OSGLUE
#define MCS_YAFFS_OSGLUE 0
#endif
#ifndef MCS_YAFFS_NAND_INBAND
#define MCS_YAFFS_NAND_INBAND 1    /* tags inside the page data (covered by the chip's data ECC) */
#endif
#ifndef MCS_YAFFS_OOB_OFFSET
#define MCS_YAFFS_OOB_OFFSET 2     /* spare-area tags start after the bad-block marker */
#endif
#ifndef MCS_YAFFS_CACHES
#define MCS_YAFFS_CACHES 4         /* short-op cache entries (one chunk of RAM each) */
#endif
#ifndef MCS_YAFFS_RESERVED_BLOCKS
#define MCS_YAFFS_RESERVED_BLOCKS 5
#endif

static int map_err(void) {
    int e = yaffsfs_GetLastError();
    if (e < 0) e = -e;
    switch (e) {
    case ENOENT: return MCS_VFS_ENOENT;
    case EEXIST: return MCS_VFS_EEXIST;
    case ENOTDIR: return MCS_VFS_ENOTDIR;
    case EISDIR: return MCS_VFS_EISDIR;
    case ENOTEMPTY: return MCS_VFS_ENOTEMPTY;
    case ENOSPC: return MCS_VFS_ENOSPC;
    case ENOMEM: return MCS_VFS_ENOMEM;
    case ENAMETOOLONG: return MCS_VFS_ENAMETOOLONG;
    case EINVAL: return MCS_VFS_EINVAL;
    case EACCES: case EROFS: return MCS_VFS_EACCES;
    default: return MCS_VFS_EIO;
    }
}
#define DEV(ctx) ((struct yaffs_dev*)(ctx))
#define FD(fh) ((int)(intptr_t)(fh) - 1)

static int y_open(void* ctx, const char* path, int flags, void** fh) {
    struct yaffs_stat st;
    if (yaffs_stat_reldev(DEV(ctx), path, &st) == 0 && S_ISDIR(st.st_mode)) return MCS_VFS_EISDIR;
    int of = O_RDONLY;
    if (flags & MCS_VFS_WRITE) of = O_WRONLY | O_CREAT | O_TRUNC;
    else if (flags & MCS_VFS_APPEND) of = O_WRONLY | O_CREAT | O_APPEND;
    int fd = yaffs_open_reldev(DEV(ctx), path, of, S_IREAD | S_IWRITE);
    if (fd < 0) return map_err();
    *fh = (void*)(intptr_t)(fd + 1);
    return MCS_VFS_OK;
}
static int y_read(void* ctx, void* fh, void* buf, size_t n) {
    (void)ctx;
    int r = yaffs_read(FD(fh), buf, (unsigned)n);
    return r < 0 ? map_err() : r;
}
static int y_write(void* ctx, void* fh, const void* buf, size_t n) {
    (void)ctx;
    int r = yaffs_write(FD(fh), buf, (unsigned)n);
    if (r < 0) return map_err();
    return (size_t)r < n ? MCS_VFS_ENOSPC : r;   /* short write = device full */
}
static int y_close(void* ctx, void* fh) {
    (void)ctx;
    return yaffs_close(FD(fh)) < 0 ? map_err() : MCS_VFS_OK;
}
static int y_stat(void* ctx, const char* path, mcs_vfs_stat_t* out) {
    struct yaffs_stat st;
    if (yaffs_stat_reldev(DEV(ctx), path, &st) < 0) return map_err();
    out->is_dir = S_ISDIR(st.st_mode);
    out->size = out->is_dir ? 0 : (uint32_t)st.st_size;
    return MCS_VFS_OK;
}
static int y_remove(void* ctx, const char* path) {
    struct yaffs_stat st;
    if (yaffs_stat_reldev(DEV(ctx), path, &st) < 0) return map_err();
    int r = S_ISDIR(st.st_mode) ? yaffs_rmdir_reldev(DEV(ctx), path) : yaffs_unlink_reldev(DEV(ctx), path);
    return r < 0 ? map_err() : MCS_VFS_OK;
}
static int y_mkdir(void* ctx, const char* path) {
    return yaffs_mkdir_reldev(DEV(ctx), path, S_IREAD | S_IWRITE | S_IEXEC) < 0 ? map_err() : MCS_VFS_OK;
}
static int y_rename(void* ctx, const char* from, const char* to) {
    /* YAFFS replaces an existing destination file in one object-header write */
    return yaffs_rename_reldev(DEV(ctx), from, to) < 0 ? map_err() : MCS_VFS_OK;
}
static int y_list(void* ctx, const char* path, mcs_vfs_list_cb cb, void* ud) {
    yaffs_DIR* d = yaffs_opendir_reldev(DEV(ctx), path);
    if (!d) return map_err();
    char full[MCS_VFS_PATH_MAX];
    size_t pl = strlen(path);
    if (pl == 1) pl = 0;                         /* "/" */
    struct yaffs_dirent* de;
    while ((de = yaffs_readdir(d)) != NULL) {
        size_t nl = strlen(de->d_name);
        if (pl + 1 + nl + 1 > sizeof full) continue;
        memcpy(full, path, pl); full[pl] = '/'; memcpy(full + pl + 1, de->d_name, nl + 1);
        struct yaffs_stat st;
        mcs_vfs_stat_t s = { 0, false };
        if (yaffs_stat_reldev(DEV(ctx), full, &st) == 0) {
            s.is_dir = S_ISDIR(st.st_mode);
            s.size = s.is_dir ? 0 : (uint32_t)st.st_size;
        }
        if (cb(ud, de->d_name, &s)) break;
    }
    yaffs_closedir(d);
    return MCS_VFS_OK;
}

static int y_statfs(void* ctx, mcs_vfs_statfs_t* st) {
    struct yaffs_dev* dev = (struct yaffs_dev*)ctx;
    Y_LOFF_T t = yaffs_totalspace_reldev(dev), f = yaffs_freespace_reldev(dev);
    if (t < 0 || f < 0) return MCS_VFS_EIO;
    st->total = (uint64_t)t; st->free = (uint64_t)f;
    st->format = "yaffs2";
    return MCS_VFS_OK;
}
const mcs_vfs_ops_t mcs_yaffs_ops = { y_open, y_read, y_write, y_close, y_stat, y_remove, y_mkdir, y_rename, y_list, y_statfs };

/* ---- YAFFS2 driver over mcs_flash_t ---- */
#if MCS_ENABLE_FLASH
#define PART(dev) ((mcs_flash_part_t*)(dev)->driver_context)

static int drv_write(struct yaffs_dev* dev, int chunk, const u8* data, int dlen, const u8* oob, int olen) {
    mcs_flash_t* f = PART(dev)->flash;
    uint32_t cs = dev->param.total_bytes_per_chunk;
    int e;
    if (f->type == MCS_FLASH_NAND)
        e = f->prog_page(f, (uint32_t)chunk, data, (uint32_t)dlen, oob, MCS_YAFFS_OOB_OFFSET, oob ? (uint32_t)olen : 0);
    else
        e = (data && dlen) ? f->prog(f, (uint32_t)chunk * cs, data, (uint32_t)dlen) : 0;
    return e ? YAFFS_FAIL : YAFFS_OK;
}
static int drv_read(struct yaffs_dev* dev, int chunk, u8* data, int dlen, u8* oob, int olen,
                    enum yaffs_ecc_result* ecc) {
    mcs_flash_t* f = PART(dev)->flash;
    uint32_t cs = dev->param.total_bytes_per_chunk;
    int e;
    if (f->type == MCS_FLASH_NAND)
        e = f->read_page(f, (uint32_t)chunk, data, data ? (uint32_t)dlen : 0, oob, MCS_YAFFS_OOB_OFFSET, oob ? (uint32_t)olen : 0);
    else
        e = (data && dlen) ? f->read(f, (uint32_t)chunk * cs, data, (uint32_t)dlen) : 0;
    if (ecc) *ecc = e == MCS_FLASH_EECC ? YAFFS_ECC_RESULT_UNFIXED : e == MCS_FLASH_FIXED ? YAFFS_ECC_RESULT_FIXED
                                                                                      : YAFFS_ECC_RESULT_NO_ERROR;
    return (e < 0 && e != MCS_FLASH_EECC) ? YAFFS_FAIL : YAFFS_OK;
}
static int drv_erase(struct yaffs_dev* dev, int block) {
    mcs_flash_t* f = PART(dev)->flash;
    return f->erase(f, (uint32_t)block) ? YAFFS_FAIL : YAFFS_OK;
}
static int drv_mark_bad(struct yaffs_dev* dev, int block) {
    mcs_flash_t* f = PART(dev)->flash;
    return (!f->mark_bad || f->mark_bad(f, (uint32_t)block) == 0) ? YAFFS_OK : YAFFS_FAIL;
}
static int drv_check_bad(struct yaffs_dev* dev, int block) {
    mcs_flash_t* f = PART(dev)->flash;
    return (f->is_bad && f->is_bad(f, (uint32_t)block) != 0) ? YAFFS_FAIL : YAFFS_OK;
}

int mcs_yaffs_flash_dev(struct yaffs_dev* dev, mcs_flash_part_t* part, const char* name) {
    mcs_flash_t* f = part->flash;
    uint32_t count = part->block_count ? part->block_count : f->block_count - part->first_block;
    if (part->first_block >= f->block_count || part->first_block + count > f->block_count) return MCS_VFS_EINVAL;
    memset(dev, 0, sizeof *dev);
    struct yaffs_param* p = &dev->param;
    p->name = name;
    p->is_yaffs2 = 1;
    if (f->type == MCS_FLASH_NAND) {
        p->total_bytes_per_chunk = f->page_size;
        p->spare_bytes_per_chunk = f->spare_size;
        p->inband_tags = MCS_YAFFS_NAND_INBAND || f->spare_size < MCS_YAFFS_OOB_OFFSET + sizeof(struct yaffs_packed_tags2);
    } else {                                     /* NOR: no spare area, never marks blocks bad */
        p->total_bytes_per_chunk = f->block_size >= 32768 ? 2048 : 512;
        p->inband_tags = 1;
        p->disable_bad_block_marking = 1;
    }
    p->chunks_per_block = f->block_size / p->total_bytes_per_chunk;
    if (p->chunks_per_block < 2 || count < 6) return MCS_VFS_EINVAL;   /* YAFFS2: reserved blocks + 4 minimum */
    p->start_block = part->first_block;
    p->end_block = part->first_block + count - 1;
    p->n_reserved_blocks = count > 4 * MCS_YAFFS_RESERVED_BLOCKS ? MCS_YAFFS_RESERVED_BLOCKS : 2;
    p->n_caches = MCS_YAFFS_CACHES;
    p->hide_lost_n_found = 1;                    /* keep Directory.GetDirectories("/") clean */
    p->use_nand_ecc = 1;                         /* data ECC is the chip's (NAND) or not needed (NOR) */
    dev->drv.drv_write_chunk_fn = drv_write;
    dev->drv.drv_read_chunk_fn = drv_read;
    dev->drv.drv_erase_fn = drv_erase;
    dev->drv.drv_mark_bad_fn = drv_mark_bad;
    dev->drv.drv_check_bad_fn = drv_check_bad;
    dev->driver_context = part;
    yaffs_add_device(dev);
    return MCS_VFS_OK;
}
#endif

/* ---- minimal single-threaded OS glue (bare metal) ---- */
#if MCS_YAFFS_OSGLUE
#include <stdlib.h>
unsigned int yaffs_trace_mask = 0;
static int g_yaffs_err;
static unsigned g_yaffs_mem, g_yaffs_mem_hw;
void yaffsfs_Lock(void) {}
void yaffsfs_Unlock(void) {}
u32 yaffsfs_CurrentTime(void) { return 0; }
void yaffsfs_SetError(int err) { g_yaffs_err = err; }
int yaffsfs_GetLastError(void) { return g_yaffs_err; }
typedef union { size_t n; long long ll; double d; void* p; } y_hdr;   /* keeps 8-byte alignment */
void* yaffsfs_malloc(size_t size) {
    y_hdr* p = (y_hdr*)malloc(size + sizeof(y_hdr));
    if (!p) return NULL;
    p->n = size; g_yaffs_mem += (unsigned)size;
    if (g_yaffs_mem > g_yaffs_mem_hw) g_yaffs_mem_hw = g_yaffs_mem;
    return p + 1;
}
void yaffsfs_free(void* ptr) {
    if (!ptr) return;
    y_hdr* p = (y_hdr*)ptr - 1;
    g_yaffs_mem -= (unsigned)p->n;
    free(p);
}
void yaffsfs_get_malloc_values(unsigned* current, unsigned* high_water) {
    if (current) *current = g_yaffs_mem;
    if (high_water) *high_water = g_yaffs_mem_hw;
}
int yaffsfs_CheckMemRegion(const void* addr, size_t size, int write_request) {
    (void)size; (void)write_request;
    return addr ? 0 : -1;
}
void yaffsfs_OSInitialisation(void) {}
void yaffs_bug_fn(const char* file_name, int line_no) { (void)file_name; (void)line_no; }
#endif
#endif
