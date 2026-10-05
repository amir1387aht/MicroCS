/*
 * MicroCS - virtual filesystem (optional module, MCS_ENABLE_FS).
 *
 * A VFS is a small mount table. Each mount maps a path prefix to a backend
 * (RAM, POSIX directory, LittleFS, or your own) through mcs_vfs_ops_t.
 * Paths are normalised before they reach a backend: "." and ".." are resolved,
 * and ".." can never leave the VFS root, so a backend only ever sees absolute
 * paths inside its own mount ("/", "/a/b.cs").
 */
#ifndef MCS_VFS_H
#define MCS_VFS_H
#include "mcs.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifndef MCS_VFS_MAX_MOUNTS
#define MCS_VFS_MAX_MOUNTS 4
#endif
#ifndef MCS_VFS_PATH_MAX
#define MCS_VFS_PATH_MAX 128
#endif

/* error codes (negative) */
enum {
    MCS_VFS_OK = 0, MCS_VFS_ENOENT = -1, MCS_VFS_EEXIST = -2, MCS_VFS_ENOTDIR = -3,
    MCS_VFS_EISDIR = -4, MCS_VFS_ENOSPC = -5, MCS_VFS_EACCES = -6, MCS_VFS_EIO = -7,
    MCS_VFS_ENOTEMPTY = -8, MCS_VFS_EINVAL = -9, MCS_VFS_ENOMEM = -10, MCS_VFS_ENAMETOOLONG = -11
};
/* open flags */
#define MCS_VFS_READ   0x01
#define MCS_VFS_WRITE  0x02    /* create + truncate */
#define MCS_VFS_APPEND 0x04    /* create + append   */
/* mount flags */
#define MCS_VFS_RDONLY 0x01
#define MCS_VFS_NOEXEC 0x02    /* files may be read but not run as scripts */

typedef struct { uint32_t size; bool is_dir; } mcs_vfs_stat_t;
typedef int (*mcs_vfs_list_cb)(void* ud, const char* name, const mcs_vfs_stat_t* st); /* non-zero stops */

typedef struct mcs_vfs_ops {
    int (*open)(void* ctx, const char* path, int flags, void** fh);
    int (*read)(void* ctx, void* fh, void* buf, size_t n);         /* bytes read or <0 */
    int (*write)(void* ctx, void* fh, const void* buf, size_t n);  /* bytes written or <0 */
    int (*close)(void* ctx, void* fh);
    int (*stat)(void* ctx, const char* path, mcs_vfs_stat_t* st);
    int (*remove)(void* ctx, const char* path);                    /* file or empty dir */
    int (*mkdir)(void* ctx, const char* path);
    int (*rename)(void* ctx, const char* from, const char* to);    /* may be NULL */
    int (*list)(void* ctx, const char* path, mcs_vfs_list_cb cb, void* ud);
} mcs_vfs_ops_t;

typedef struct {
    char prefix[32];
    const mcs_vfs_ops_t* ops;
    void* ctx;
    uint8_t flags;
} mcs_vfs_mount_t;

typedef struct mcs_vfs {
    mcs_vfs_mount_t mounts[MCS_VFS_MAX_MOUNTS];
    int count;
    /* scratch allocator for whole-file reads; NULL = realloc/free */
    mcs_realloc_fn realloc_fn;
    void* alloc_ud;
} mcs_vfs_t;

typedef struct { mcs_vfs_mount_t* m; void* fh; } mcs_vfs_file_t;

void mcs_vfs_init(mcs_vfs_t* vfs);
int mcs_vfs_mount(mcs_vfs_t* vfs, const char* prefix, const mcs_vfs_ops_t* ops, void* ctx, uint8_t flags);
int mcs_vfs_umount(mcs_vfs_t* vfs, const char* prefix);

/* Normalise `path` into `out` (absolute, no "."/".." / duplicate slashes). */
int mcs_vfs_normalize(const char* path, char* out, size_t cap);

int mcs_vfs_open(mcs_vfs_t* vfs, const char* path, int flags, mcs_vfs_file_t* f);
int mcs_vfs_read(mcs_vfs_file_t* f, void* buf, size_t n);
int mcs_vfs_write(mcs_vfs_file_t* f, const void* buf, size_t n);
int mcs_vfs_close(mcs_vfs_file_t* f);
int mcs_vfs_stat(mcs_vfs_t* vfs, const char* path, mcs_vfs_stat_t* st);
int mcs_vfs_remove(mcs_vfs_t* vfs, const char* path);
int mcs_vfs_mkdir(mcs_vfs_t* vfs, const char* path);
int mcs_vfs_rename(mcs_vfs_t* vfs, const char* from, const char* to);
int mcs_vfs_list(mcs_vfs_t* vfs, const char* path, mcs_vfs_list_cb cb, void* ud);
/* mount flags that apply to `path` (0 if unmounted) */
int mcs_vfs_flags(mcs_vfs_t* vfs, const char* path);

/* Whole-file helpers. The buffer from read_file is NUL-terminated and must be
 * released with mcs_vfs_free. */
int mcs_vfs_read_file(mcs_vfs_t* vfs, const char* path, char** data, size_t* len);
int mcs_vfs_write_file(mcs_vfs_t* vfs, const char* path, const void* data, size_t len, bool append);
void mcs_vfs_free(mcs_vfs_t* vfs, void* data, size_t len);
const char* mcs_vfs_strerror(int err);

/* Run a script (source or .mcsb image) from the VFS. Honours MCS_VFS_NOEXEC. */
mcs_result_t mcs_exec_file(mcs_vm_t* vm, mcs_vfs_t* vfs, const char* path);

/* C# System.IO subset: File, Directory, Path. Stores `vfs` in MCS_EXT_VFS. */
void mcs_fs_open_lib(mcs_vm_t* vm, mcs_vfs_t* vfs);

/* ---- LittleFS backend (optional, needs littlefs; see modules/fs/mcs_vfs_lfs.c) ----
 * ctx = a mounted `lfs_t*`. Enable with -DMCS_ENABLE_LFS=1. */
#ifndef MCS_ENABLE_LFS
#define MCS_ENABLE_LFS 0
#endif
#if MCS_ENABLE_LFS
extern const mcs_vfs_ops_t mcs_lfs_ops;
#endif

/* ---- built-in backends ---- */
/* RAM filesystem. `limit` caps total file bytes (0 = unlimited). The allocator
 * may be mcs_pool_realloc over a static buffer. */
typedef struct mcs_ramfs {
    struct mcs_ramfs_node* nodes;
    size_t used, limit;
    mcs_realloc_fn realloc_fn;
    void* alloc_ud;
} mcs_ramfs_t;
extern const mcs_vfs_ops_t mcs_ramfs_ops;
void mcs_ramfs_init(mcs_ramfs_t* fs, size_t limit, mcs_realloc_fn realloc_fn, void* alloc_ud);
void mcs_ramfs_free(mcs_ramfs_t* fs);

/* POSIX directory (Linux, macOS, MinGW). ctx = mcs_posixfs_t with the host root. */
typedef struct { char root[256]; } mcs_posixfs_t;
extern const mcs_vfs_ops_t mcs_posixfs_ops;
int mcs_posixfs_init(mcs_posixfs_t* fs, const char* host_dir);

#ifdef __cplusplus
}
#endif
#endif
