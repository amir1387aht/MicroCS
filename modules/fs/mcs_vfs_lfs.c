/*
 * MicroCS VFS backend for LittleFS (https://github.com/littlefs-project/littlefs).
 *
 * LittleFS is not bundled. Build with -DMCS_ENABLE_LFS=1 and add lfs.c/lfs_util.c
 * plus their include path (`make lfs-test` downloads v2.9.3 into build/third_party).
 *
 * The caller owns the lfs_t: configure the block device, lfs_mount() (or format),
 * then  mcs_vfs_mount(&vfs, "/flash", &mcs_lfs_ops, &lfs, 0).
 * Rename maps to lfs_rename(), which is atomic - the shell's upload path
 * (write "<name>.part", then rename) is therefore power-loss safe on LittleFS.
 *
 * File handles come from a fixed table (MCS_LFS_MAX_FILES); each open file uses
 * LittleFS's own cache buffer (lfs_malloc, i.e. malloc unless the port defines
 * LFS_NO_MALLOC and supplies per-file buffers - see docs/FILESYSTEM.md).
 */
#include "mcs_vfs.h"
#if MCS_ENABLE_FS && MCS_ENABLE_LFS
#include <string.h>
#include "lfs.h"

#ifndef MCS_LFS_MAX_FILES
#define MCS_LFS_MAX_FILES 4
#endif

static lfs_file_t g_files[MCS_LFS_MAX_FILES];
static bool g_used[MCS_LFS_MAX_FILES];

static int map_err(int e) {
    if (e >= 0) return e;
    switch (e) {
    case LFS_ERR_NOENT: return MCS_VFS_ENOENT;
    case LFS_ERR_EXIST: return MCS_VFS_EEXIST;
    case LFS_ERR_NOTDIR: return MCS_VFS_ENOTDIR;
    case LFS_ERR_ISDIR: return MCS_VFS_EISDIR;
    case LFS_ERR_NOTEMPTY: return MCS_VFS_ENOTEMPTY;
    case LFS_ERR_NOSPC: return MCS_VFS_ENOSPC;
    case LFS_ERR_NOMEM: return MCS_VFS_ENOMEM;
    case LFS_ERR_NAMETOOLONG: return MCS_VFS_ENAMETOOLONG;
    case LFS_ERR_INVAL: return MCS_VFS_EINVAL;
    default: return MCS_VFS_EIO; /* LFS_ERR_IO, LFS_ERR_CORRUPT, ... */
    }
}

static int l_open(void* ctx, const char* path, int flags, void** fh) {
    lfs_t* lfs = (lfs_t*)ctx;
    int slot = -1;
    for (int i = 0; i < MCS_LFS_MAX_FILES; i++) if (!g_used[i]) { slot = i; break; }
    if (slot < 0) return MCS_VFS_ENOMEM;
    int lf = LFS_O_RDONLY;
    if (flags & MCS_VFS_WRITE) lf = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
    else if (flags & MCS_VFS_APPEND) lf = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND;
    struct lfs_info info;
    if (lfs_stat(lfs, path, &info) == 0 && info.type == LFS_TYPE_DIR) return MCS_VFS_EISDIR;
    int e = lfs_file_open(lfs, &g_files[slot], path, lf);
    if (e < 0) return map_err(e);
    g_used[slot] = true;
    *fh = &g_files[slot];
    return MCS_VFS_OK;
}
static int l_read(void* ctx, void* fh, void* buf, size_t n) {
    return map_err((int)lfs_file_read((lfs_t*)ctx, (lfs_file_t*)fh, buf, (lfs_size_t)n));
}
static int l_write(void* ctx, void* fh, const void* buf, size_t n) {
    return map_err((int)lfs_file_write((lfs_t*)ctx, (lfs_file_t*)fh, buf, (lfs_size_t)n));
}
static int l_close(void* ctx, void* fh) {
    int e = lfs_file_close((lfs_t*)ctx, (lfs_file_t*)fh);
    g_used[(lfs_file_t*)fh - g_files] = false;
    return map_err(e);
}
static int l_stat(void* ctx, const char* path, mcs_vfs_stat_t* st) {
    struct lfs_info info;
    int e = lfs_stat((lfs_t*)ctx, path, &info);
    if (e < 0) return map_err(e);
    st->is_dir = info.type == LFS_TYPE_DIR;
    st->size = st->is_dir ? 0 : (uint32_t)info.size;
    return MCS_VFS_OK;
}
static int l_remove(void* ctx, const char* path) { return map_err(lfs_remove((lfs_t*)ctx, path)); }
static int l_mkdir(void* ctx, const char* path) { return map_err(lfs_mkdir((lfs_t*)ctx, path)); }
static int l_rename(void* ctx, const char* from, const char* to) {
    /* LittleFS replaces an existing destination file atomically (POSIX semantics) */
    return map_err(lfs_rename((lfs_t*)ctx, from, to));
}
static int l_list(void* ctx, const char* path, mcs_vfs_list_cb cb, void* ud) {
    lfs_t* lfs = (lfs_t*)ctx;
    lfs_dir_t dir;
    int e = lfs_dir_open(lfs, &dir, path);
    if (e < 0) return map_err(e);
    struct lfs_info info;
    while ((e = lfs_dir_read(lfs, &dir, &info)) > 0) {
        if (!strcmp(info.name, ".") || !strcmp(info.name, "..")) continue;
        mcs_vfs_stat_t st = { info.type == LFS_TYPE_DIR ? 0 : (uint32_t)info.size, info.type == LFS_TYPE_DIR };
        if (cb(ud, info.name, &st)) break;
    }
    lfs_dir_close(lfs, &dir);
    return e < 0 ? map_err(e) : MCS_VFS_OK;
}

static int l_statfs(void* ctx, mcs_vfs_statfs_t* st) {
    lfs_t* lfs = (lfs_t*)ctx;
    lfs_ssize_t used = lfs_fs_size(lfs);              /* blocks in use (incl. metadata) */
    if (used < 0) return map_err((int)used);
    uint64_t bs = lfs->cfg->block_size, count = lfs->cfg->block_count;
#if defined(LFS_VERSION) && LFS_VERSION >= 0x00020007
    struct lfs_fsinfo fi;
    if (lfs_fs_stat(lfs, &fi) == 0) { bs = fi.block_size; count = fi.block_count; }
#endif
    st->total = bs * count;
    st->free = (uint64_t)used < count ? bs * (count - (uint64_t)used) : 0;
    st->format = "littlefs";
    return MCS_VFS_OK;
}
const mcs_vfs_ops_t mcs_lfs_ops = { l_open, l_read, l_write, l_close, l_stat, l_remove, l_mkdir, l_rename, l_list, l_statfs };
#endif

/* ---- LittleFS block device over mcs_flash_t ---- */
#if MCS_ENABLE_FS && MCS_ENABLE_LFS && MCS_ENABLE_FLASH
static int fl_err(int e) {
    if (e >= 0) return 0;
    return (e == MCS_FLASH_EPROG || e == MCS_FLASH_EBAD || e == MCS_FLASH_EECC) ? LFS_ERR_CORRUPT : LFS_ERR_IO;
}
static int fl_read(const struct lfs_config* c, lfs_block_t b, lfs_off_t off, void* buf, lfs_size_t n) {
    mcs_flash_part_t* p = (mcs_flash_part_t*)c->context;
    return fl_err(p->flash->read(p->flash, (p->first_block + b) * c->block_size + off, buf, n));
}
static int fl_prog(const struct lfs_config* c, lfs_block_t b, lfs_off_t off, const void* buf, lfs_size_t n) {
    mcs_flash_part_t* p = (mcs_flash_part_t*)c->context;
    int e = p->flash->prog(p->flash, (p->first_block + b) * c->block_size + off, buf, n);
    if (e == MCS_FLASH_EPROG && p->flash->mark_bad) p->flash->mark_bad(p->flash, p->first_block + b);
    return fl_err(e);
}
static int fl_erase(const struct lfs_config* c, lfs_block_t b) {
    mcs_flash_part_t* p = (mcs_flash_part_t*)c->context;
    mcs_flash_t* f = p->flash;
    if (f->is_bad && f->is_bad(f, p->first_block + b) > 0) return LFS_ERR_CORRUPT;
    int e = f->erase(f, p->first_block + b);
    if (e == MCS_FLASH_EPROG && f->mark_bad) f->mark_bad(f, p->first_block + b);
    return fl_err(e);
}
static int fl_sync(const struct lfs_config* c) {
    mcs_flash_part_t* p = (mcs_flash_part_t*)c->context;
    return p->flash->sync ? fl_err(p->flash->sync(p->flash)) : 0;
}
int mcs_lfs_flash_config(struct lfs_config* cfg, mcs_flash_part_t* part) {
    mcs_flash_t* f = part->flash;
    if (part->first_block >= f->block_count) return MCS_VFS_EINVAL;
    memset(cfg, 0, sizeof *cfg);
    cfg->context = part;
    cfg->read = fl_read; cfg->prog = fl_prog; cfg->erase = fl_erase; cfg->sync = fl_sync;
    cfg->block_size = f->block_size;
    cfg->block_count = part->block_count ? part->block_count : f->block_count - part->first_block;
    if (part->first_block + cfg->block_count > f->block_count) return MCS_VFS_EINVAL;
    /* LittleFS keeps its superblock pair in blocks 0 and 1 of the partition and
     * cannot move it: on NAND both must be good (block 0 of a chip always is). */
    for (uint32_t b = 0; b < 2 && f->is_bad; b++)
        if (f->is_bad(f, part->first_block + b) != 0) return MCS_VFS_EIO;
    cfg->read_size = 16;
    if (f->type == MCS_FLASH_NAND) {
        cfg->prog_size = f->page_size;   /* one program per NAND page (on-die ECC) */
        cfg->cache_size = f->page_size;
    } else {
        cfg->prog_size = f->write_size > 16 ? f->write_size : 16;
        cfg->cache_size = f->page_size < f->block_size ? f->page_size : f->block_size;
        if (cfg->cache_size < cfg->prog_size) cfg->cache_size = cfg->prog_size;
    }
    cfg->lookahead_size = 16;
    cfg->block_cycles = 500;
    return MCS_VFS_OK;
}
#endif
