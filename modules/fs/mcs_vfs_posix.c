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
#include <fcntl.h>
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
    bool write = (flags & MCS_VFS_WRITABLE) != 0;
    int of = write ? ((flags & MCS_VFS_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (flags & (MCS_VFS_WRITE | MCS_VFS_APPEND | MCS_VFS_CREATE)) of |= O_CREAT;
    if (flags & (MCS_VFS_WRITE | MCS_VFS_TRUNC)) of |= O_TRUNC;
    if (flags & MCS_VFS_APPEND) of |= O_APPEND;
    if (flags & MCS_VFS_EXCL) of |= O_EXCL;
    int fd = open(hp, of, 0666);
    if (fd < 0) return map_errno(errno);
    FILE* f = fdopen(fd, write ? ((flags & MCS_VFS_READ) ? "r+b" : "wb") : "rb");
    if (!f) { int e = map_errno(errno); close(fd); return e; }
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
#if MCS_ENABLE_FILE_STREAMS
static int p_seek(void* ctx, void* fh, int32_t off, int origin, int32_t* out) {
    (void)ctx; FILE* f = (FILE*)fh;
    long old = ftell(f);
    if (old < 0 || fseek(f, off, origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END)) return MCS_VFS_EINVAL;
    long p = ftell(f);
    if (p < 0 || (uint64_t)p > 0x7fffffff) { (void)fseek(f, old, SEEK_SET); return MCS_VFS_EINVAL; }
    *out = (int32_t)p; return 0;
}
static int p_flush(void* ctx, void* fh) { (void)ctx; return fflush((FILE*)fh) ? MCS_VFS_EIO : 0; }
#endif
static int p_stat(void* ctx, const char* path, mcs_vfs_stat_t* out) {
    HOST(path);
    struct stat st;
    if (stat(hp, &st)) return map_errno(errno);
    out->is_dir = S_ISDIR(st.st_mode);
    /* Preserve an oversized sentinel instead of wrapping >4 GiB files to a
     * small size; FileStream must reject these under its signed 32-bit limit. */
    out->size = out->is_dir ? 0 : ((uint64_t)st.st_size > UINT32_MAX ? UINT32_MAX : (uint32_t)st.st_size);
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
    char** names = NULL;
    size_t count = 0, cap = 0;
    int error = MCS_VFS_OK;
    struct dirent* de;
    for (;;) {
        errno = 0;
        de = readdir(d);
        if (!de) { if (errno) error = map_errno(errno); break; }
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (count == cap) {
            size_t next = cap ? cap * 2 : 32;
            if (next < cap || next > SIZE_MAX / sizeof(*names)) { error = MCS_VFS_ENOMEM; break; }
            char** grown = (char**)realloc(names, next * sizeof(*names));
            if (!grown) { error = MCS_VFS_ENOMEM; break; }
            names = grown; cap = next;
        }
        names[count] = strdup(de->d_name);
        if (!names[count]) { error = MCS_VFS_ENOMEM; break; }
        count++;
    }
    if (closedir(d) && !error) error = map_errno(errno);
    if (!error) {
        if (count > 1) qsort(names, count, sizeof names[0], name_cmp);
        for (size_t i = 0; i < count; i++) {
            char full[600];
            snprintf(full, sizeof full, "%s/%s", hp, names[i]);
            struct stat st;
            mcs_vfs_stat_t vs = { 0, false };
            if (!stat(full, &st)) { vs.is_dir = S_ISDIR(st.st_mode); vs.size = vs.is_dir ? 0 : ((uint64_t)st.st_size > UINT32_MAX ? UINT32_MAX : (uint32_t)st.st_size); }
            if (cb(ud, names[i], &vs)) break;
        }
    }
    for (size_t i = 0; i < count; i++) free(names[i]);
    free(names);
    return error;
}

/* ESP-IDF has no statvfs: the ESP32 port wraps these ops with esp_littlefs_info */
#if defined(_WIN32)
#include <windows.h>
static int p_statfs(void* ctx, mcs_vfs_statfs_t* st) {
    ULARGE_INTEGER avail, total, all_free;
    if (!GetDiskFreeSpaceExA(((mcs_posixfs_t*)ctx)->root, &avail, &total, &all_free)) return MCS_VFS_EIO;
    st->total = total.QuadPart; st->free = avail.QuadPart; st->format = "posix";
    return MCS_VFS_OK;
}
#define P_STATFS p_statfs
#elif !defined(ESP_PLATFORM)
#include <sys/statvfs.h>
static int p_statfs(void* ctx, mcs_vfs_statfs_t* st) {
    struct statvfs v;
    if (statvfs(((mcs_posixfs_t*)ctx)->root, &v)) return map_errno(errno);
    st->total = (uint64_t)v.f_blocks * v.f_frsize; st->free = (uint64_t)v.f_bavail * v.f_frsize; st->format = "posix";
    return MCS_VFS_OK;
}
#define P_STATFS p_statfs
#else
#define P_STATFS NULL
#endif

const mcs_vfs_ops_t mcs_posixfs_ops = { p_open, p_read, p_write, p_close, p_stat, p_remove, p_mkdir, p_rename, p_list, P_STATFS,
#if MCS_ENABLE_FILE_STREAMS
    p_seek, p_flush
#else
    NULL, NULL
#endif
};
#endif
