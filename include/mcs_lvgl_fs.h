/* Host-lifetime, bounded read-only LVGL filesystem adapter. docs/LVGL.md. */
#ifndef MCS_LVGL_FS_H
#define MCS_LVGL_FS_H
#include "mcs_lvgl.h"
#if MCS_ENABLE_LVGL && MCS_ENABLE_FS
#include "mcs_vfs.h"
#if MCS_LVGL_FS_MAX_FILES < 1
#error "MCS_LVGL_FS_MAX_FILES must be positive"
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    mcs_vfs_file_t file;
    uint32_t position;
    bool used;
} mcs_lvgl_fs_file_t;
typedef struct {
    lv_fs_drv_t driver; /* must be first: callbacks recover context without LV_USE_USER_DATA */
    mcs_vfs_t* vfs;
    char root[MCS_VFS_PATH_MAX];
    mcs_lvgl_fs_file_t files[MCS_LVGL_FS_MAX_FILES];
    bool registered, active;
} mcs_lvgl_fs_t;
/* Zero-initialize bridge. Call under host GUI lock AFTER lv_init, before loading
 * assets. root is a VFS directory, e.g. /assets; letter e.g. 'S'. No VM allocator.
 * Driver/context must live UNTIL lv_deinit: LVGL 8/9 offer no public unregister.
 * Never register per VM. vfs/mounts/backend/lock must outlive open assets. */
int mcs_lvgl_fs_register(mcs_lvgl_fs_t* bridge, mcs_vfs_t* vfs, char letter, const char* root);
/* Under GUI lock, after releasing widgets/fonts/decoder caches using this drive,
 * before destroying VFS: close files and permanently mark drive unavailable.
 * Does NOT unregister/free bridge; keep it alive until host lv_deinit. */
void mcs_lvgl_fs_disable(mcs_lvgl_fs_t* bridge);
#ifdef __cplusplus
}
#endif
#endif
#endif
