/* MicroCS - POSIX directory backend for host builds, embedded Linux and
 * ESP-IDF (any VFS-mounted filesystem, e.g. LittleFS on a flash partition).
 * The VFS has already normalised paths, so "../" cannot escape `root`.
 * Symlinks inside the root are followed; do not mount untrusted trees that
 * contain links pointing outside if confinement matters. */
#include "mcs_vfs.h"
#if MCS_ENABLE_FS && (defined(__unix__) || defined(__APPLE__) || defined(_WIN32) || defined(ESP_PLATFORM))
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#if defined(_WIN32)
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0777)
#endif

int mcs_posixfs_init(mcs_posixfs_t* fs, const char* dir) {
    size_t n = strlen(dir);
    while (n > 1 && dir[n - 1] == '/') n--;
    if (n + 1 >= sizeof fs->root) return MCS_VFS_ENAMETOOLONG;
    memcpy(fs->root, dir, n); fs->root[n] = 0;
    struct stat st;
    if (stat(fs->root, &st) || !S_ISDIR(st.st_mode)) return MCS_VFS_ENOENT;
    return MCS_VFS_OK;
}

static int host_path(mcs_posixfs_t* fs, const char* path, char* out, size_t cap) {
    int n = snprintf(out, cap, "%s%s", fs->root, strcmp(path, "/") ? path : "");
    return n < 0 || (size_t)n >= cap ? MCS_VFS_ENAMETOOLONG : MCS_VFS_OK;
}

static int map_errno(int e) {
    switch (e) {
    case ENOENT: return MCS_VFS_ENOENT;
    case EEXIST: return MCS_VFS_EEXIST;
    case ENOTDIR: return MCS_VFS_ENOTDIR;
    case EISDIR: return MCS_VFS_EISDIR;
    case ENOSPC: return MCS_VFS_ENOSPC;
    case EACCES: case EPERM: case EROFS: return MCS_VFS_EACCES;
    case ENOTEMPTY: return MCS_VFS_ENOTEMPTY;
    case ENOMEM: return MCS_VFS_ENOMEM;
    case ENAMETOOLONG: return MCS_VFS_ENAMETOOLONG;
    default: return MCS_VFS_EIO;
    }
}

#define HOST(p) char hp[512]; { int e_ = host_path((mcs_posixfs_t*)ctx, p, hp, sizeof hp); if (e_) return e_; }

static int p_open(void* ctx, const char* path, int flags, void** fh) {
    HOST(path);
    struct stat st;
    if (!stat(hp, &st) && S_ISDIR(st.st_mode)) return MCS_VFS_EISDIR;
    FILE* f = fopen(hp, (flags & MCS_VFS_APPEND) ? "ab" : (flags & MCS_VFS_WRITE) ? "wb" : "rb");
    if (!f) return map_errno(errno);
    *fh = f;
    return MCS_VFS_OK;
}
static int p_read(void* ctx, void* fh, void* buf, size_t n) {
    (void)ctx;
    if (n > 0x7fffffff) n = 0x7fffffff;
    size_t r = fread(buf, 1, n, (FILE*)fh);
    return ferror((FILE*)fh) ? MCS_VFS_EIO : (int)r;
}
static int p_write(void* ctx, void* fh, const void* buf, size_t n) {
    (void)ctx;
    if (n > 0x7fffffff) n = 0x7fffffff;
    size_t w = fwrite(buf, 1, n, (FILE*)fh);
    return w == 0 && n ? map_errno(errno) : (int)w;
}
static int p_close(void* ctx, void* fh) { (void)ctx; return fclose((FILE*)fh) ? MCS_VFS_EIO : MCS_VFS_OK; }
static int p_stat(void* ctx, const char* path, mcs_vfs_stat_t* out) {
    HOST(path);
    struct stat st;
    if (stat(hp, &st)) return map_errno(errno);
    out->is_dir = S_ISDIR(st.st_mode);
    out->size = out->is_dir ? 0 : (uint32_t)st.st_size;
    return MCS_VFS_OK;
}
static int p_remove(void* ctx, const char* path) {
    HOST(path);
    struct stat st;
    if (stat(hp, &st)) return map_errno(errno);
    if (S_ISDIR(st.st_mode) ? rmdir(hp) : remove(hp)) return map_errno(errno);
    return MCS_VFS_OK;
}
static int p_mkdir(void* ctx, const char* path) { HOST(path); return MKDIR(hp) ? map_errno(errno) : MCS_VFS_OK; }
static int p_rename(void* ctx, const char* from, const char* to) {
    char hp2[512];
    HOST(from);
    if (host_path((mcs_posixfs_t*)ctx, to, hp2, sizeof hp2)) return MCS_VFS_ENAMETOOLONG;
    struct stat st;
    if (!stat(hp2, &st)) return MCS_VFS_EEXIST;
    return rename(hp, hp2) ? map_errno(errno) : MCS_VFS_OK;
}

static int name_cmp(const void* a, const void* b) { return strcmp(*(char* const*)a, *(char* const*)b); }

static int p_list(void* ctx, const char* path, mcs_vfs_list_cb cb, void* ud) {
    HOST(path);
    DIR* d = opendir(hp);
    if (!d) return map_errno(errno);
    /* sorted so output is deterministic across hosts */
    char* names[256];
    int count = 0;
    struct dirent* de;
    while ((de = readdir(d)) && count < 256) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        names[count] = strdup(de->d_name);
        if (names[count]) count++;
    }
    closedir(d);
    qsort(names, (size_t)count, sizeof names[0], name_cmp);
    int i = 0;
    for (; i < count; i++) {
        char full[600];
        snprintf(full, sizeof full, "%s/%s", hp, names[i]);
        struct stat st;
        mcs_vfs_stat_t vs = { 0, false };
        if (!stat(full, &st)) { vs.is_dir = S_ISDIR(st.st_mode); vs.size = vs.is_dir ? 0 : (uint32_t)st.st_size; }
        if (cb(ud, names[i], &vs)) break;
    }
    for (int j = 0; j < count; j++) free(names[j]);
    return MCS_VFS_OK;
}

const mcs_vfs_ops_t mcs_posixfs_ops = { p_open, p_read, p_write, p_close, p_stat, p_remove, p_mkdir, p_rename, p_list };
#endif
