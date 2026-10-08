/*
 * MicroCS - LittleFS or YAFFS2 on any mcs_flash_t in one call (see mcs_vfs.h).
 *
 * Mount, and on a blank or foreign partition format it; the result is a
 * backend (ops + ctx) for mcs_vfs_mount() or mcs_runtime_cfg_t.fs_ops/fs_ctx.
 * The filesystem state (lfs_t + config, or struct yaffs_dev) lives in a small
 * static table so callers need neither lfs.h nor the yaffs headers.
 */
#include "mcs_vfs.h"
#if MCS_ENABLE_FS && MCS_ENABLE_FLASH && (MCS_ENABLE_LFS || MCS_ENABLE_YAFFS)
#include <string.h>
#if MCS_ENABLE_LFS
#include "lfs.h"
#endif
#if MCS_ENABLE_YAFFS
#include <sys/types.h>     /* mode_t, off_t (newlib) before the yaffs headers */
#include "yaffsfs.h"
#include "yaffs_guts.h"
void yaffs_remove_device(struct yaffs_dev* dev);   /* yaffsfs.c, not in yaffsfs.h */
#endif

#ifndef MCS_FLASHFS_MAX
#define MCS_FLASHFS_MAX 2          /* filesystems mounted at the same time */
#endif

static struct {
    uint8_t used;
#if MCS_ENABLE_LFS
    lfs_t lfs;
    struct lfs_config cfg;
#endif
#if MCS_ENABLE_YAFFS
    struct yaffs_dev dev;
#endif
} g_fs[MCS_FLASHFS_MAX];

const char* mcs_flashfs_kind_name(int kind) {
    return kind == MCS_FLASHFS_YAFFS2 ? "yaffs2" : kind == MCS_FLASHFS_LITTLEFS ? "littlefs" : "?";
}

/* erase every block of the partition (skipping bad NAND blocks) */
static int erase_all(mcs_flash_part_t* p) {
    mcs_flash_t* f = p->flash;
    uint32_t n = p->block_count ? p->block_count : f->block_count - p->first_block;
    for (uint32_t b = p->first_block; b < p->first_block + n; b++) {
        if (f->is_bad && f->is_bad(f, b) > 0) continue;
        int e = f->erase(f, b);
        if (e < 0 && !(e == MCS_FLASH_EPROG && f->mark_bad && f->mark_bad(f, b) == 0)) return MCS_VFS_EIO;
    }
    return MCS_VFS_OK;
}

int mcs_flashfs_mount(mcs_flashfs_t* fs, mcs_flash_t* flash, uint32_t first_block, uint32_t block_count,
                      int kind, int flags) {
    if (!fs || !flash || !flash->read || !flash->prog || !flash->erase) return MCS_VFS_EINVAL;
    memset(fs, 0, sizeof *fs);
    fs->part.flash = flash;
    fs->part.first_block = first_block;
    fs->part.block_count = block_count;
    if (first_block >= flash->block_count || (block_count && first_block + block_count > flash->block_count))
        return MCS_VFS_EINVAL;
    int slot = 0;
    while (slot < MCS_FLASHFS_MAX && g_fs[slot].used) slot++;
    if (slot == MCS_FLASHFS_MAX) return MCS_VFS_ENOMEM;
    if ((flags & MCS_FLASHFS_FORMAT) && erase_all(&fs->part) != MCS_VFS_OK) return MCS_VFS_EIO;
#if MCS_ENABLE_LFS
    if (kind == MCS_FLASHFS_LITTLEFS) {
        lfs_t* lfs = &g_fs[slot].lfs;
        struct lfs_config* cfg = &g_fs[slot].cfg;
        int e = mcs_lfs_flash_config(cfg, &fs->part);
        if (e) return e;
        memset(lfs, 0, sizeof *lfs);
        if (lfs_mount(lfs, cfg) != 0) {
            if (!(flags & (MCS_FLASHFS_FORMAT_IF_NEEDED | MCS_FLASHFS_FORMAT))) return MCS_VFS_EIO;
            if (lfs_format(lfs, cfg) != 0 || lfs_mount(lfs, cfg) != 0) return MCS_VFS_EIO;
        }
        g_fs[slot].used = 1;
        fs->ops = &mcs_lfs_ops; fs->ctx = lfs; fs->kind = kind; fs->slot = slot;
        return MCS_VFS_OK;
    }
#endif
#if MCS_ENABLE_YAFFS
    if (kind == MCS_FLASHFS_YAFFS2) {
        struct yaffs_dev* dev = &g_fs[slot].dev;
        fs->name[0] = '/'; fs->name[1] = 'f'; fs->name[2] = (char)('0' + slot); fs->name[3] = 0;
        int e = mcs_yaffs_flash_dev(dev, &fs->part, fs->name);
        if (e) return e;
        if (yaffs_mount(fs->name) != 0) {          /* a blank partition mounts (YAFFS formats lazily) */
            if (!(flags & MCS_FLASHFS_FORMAT_IF_NEEDED) || erase_all(&fs->part) != MCS_VFS_OK ||
                yaffs_mount(fs->name) != 0) {
                yaffs_remove_device(dev);
                return MCS_VFS_EIO;
            }
        }
        g_fs[slot].used = 1;
        fs->ops = &mcs_yaffs_ops; fs->ctx = dev; fs->kind = kind; fs->slot = slot;
        return MCS_VFS_OK;
    }
#endif
    return MCS_VFS_EINVAL;
}

int mcs_flashfs_unmount(mcs_flashfs_t* fs) {
    if (!fs || !fs->ops || fs->slot < 0 || fs->slot >= MCS_FLASHFS_MAX || !g_fs[fs->slot].used) return MCS_VFS_EINVAL;
    int e = 0;
#if MCS_ENABLE_LFS
    if (fs->kind == MCS_FLASHFS_LITTLEFS) e = lfs_unmount(&g_fs[fs->slot].lfs) ? MCS_VFS_EIO : 0;
#endif
#if MCS_ENABLE_YAFFS
    if (fs->kind == MCS_FLASHFS_YAFFS2) {
        e = yaffs_unmount(fs->name) ? MCS_VFS_EIO : 0;
        yaffs_remove_device(&g_fs[fs->slot].dev);
    }
#endif
    g_fs[fs->slot].used = 0;
    fs->ops = NULL; fs->ctx = NULL;
    return e;
}
#endif
