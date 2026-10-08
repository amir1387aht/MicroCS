/* MicroCS port for the Arduino API - files on an Arduino filesystem
 * (fs::FS on ESP32 / RP2040, the SD library elsewhere). See mcs_port_arduino.h. */
#include "mcs_port_arduino.h"
#include <string.h>

#if MCS_ENABLE_FS && (defined(MCS_ARDUINO_FSAPI) || defined(MCS_ARDUINO_SDLIB))

#if defined(MCS_ARDUINO_FSAPI)
#define FS_OF(ctx) (((mcs_arduino_fs_t*)(ctx))->fs)
#define FILE_T fs::File
#else
#define FS_OF(ctx) (((mcs_arduino_fs_t*)(ctx))->sd)
#define FILE_T File
#endif

struct fh_t { FILE_T f; };                 /* open file handle (wrapper: File has no virtual destructor) */

static const char* base_name(const char* p) {
    const char* s = strrchr(p, '/');
    return s ? s + 1 : p;
}

/* stat without opening twice where the core allows it */
static bool a_stat(void* ctx, const char* path, bool* is_dir, uint32_t* size) {
    if (!strcmp(path, "/")) { *is_dir = true; *size = 0; return true; }
#if defined(ARDUINO_ARCH_RP2040) || defined(ESP8266)
    fs::FSStat st;
    if (!FS_OF(ctx)->stat(path, &st)) return false;
    *is_dir = st.isDir;
    *size = st.isDir ? 0 : (uint32_t)st.size;
    return true;
#else
    if (!FS_OF(ctx)->exists(path)) return false;      /* ESP32 logs an error when opening a missing file */
    FILE_T f = FS_OF(ctx)->open(path);
    if (!f) return false;
    *is_dir = f.isDirectory();
    *size = *is_dir ? 0 : (uint32_t)f.size();
    f.close();
    return true;
#endif
}

static int af_open(void* ctx, const char* path, int flags, void** fh) {
    bool dir = false;
    uint32_t size = 0;
    bool exists = a_stat(ctx, path, &dir, &size);
    if (exists && dir) return MCS_VFS_EISDIR;
    FILE_T f;
    if (flags & (MCS_VFS_WRITE | MCS_VFS_APPEND)) {
#if defined(MCS_ARDUINO_FSAPI)
        f = FS_OF(ctx)->open(path, (flags & MCS_VFS_APPEND) ? "a" : "w");
#else
        if (exists && !(flags & MCS_VFS_APPEND)) FS_OF(ctx)->remove(path);   /* SD's FILE_WRITE appends */
        f = FS_OF(ctx)->open(path, FILE_WRITE);
#endif
        if (!f) return MCS_VFS_EIO;
    } else {
        if (!exists) return MCS_VFS_ENOENT;
#if defined(MCS_ARDUINO_FSAPI)
        f = FS_OF(ctx)->open(path, "r");
#else
        f = FS_OF(ctx)->open(path, FILE_READ);
#endif
        if (!f) return MCS_VFS_EIO;
    }
    fh_t* h = new fh_t{ f };
    if (!h) { f.close(); return MCS_VFS_ENOMEM; }
    *fh = h;
    return MCS_VFS_OK;
}
static int af_read(void*, void* fh, void* buf, size_t n) {
    FILE_T* f = &((fh_t*)fh)->f;
    size_t got = 0;
    while (got < n) {                                   /* SD's read takes a 16-bit count */
        size_t k = n - got > 16384 ? 16384 : n - got;
        int r = (int)f->read((uint8_t*)buf + got, k);
        if (r <= 0) break;
        got += (size_t)r;
        if ((size_t)r < k) break;
    }
    return (int)got;
}
static int af_write(void*, void* fh, const void* buf, size_t n) {
    size_t w = ((fh_t*)fh)->f.write((const uint8_t*)buf, n);
    return w == 0 && n ? MCS_VFS_ENOSPC : (int)w;
}
static int af_close(void*, void* fh) {
    fh_t* h = (fh_t*)fh;
    h->f.close();
    delete h;
    return MCS_VFS_OK;
}
static int af_stat(void* ctx, const char* path, mcs_vfs_stat_t* st) {
    bool dir; uint32_t size;
    if (!a_stat(ctx, path, &dir, &size)) return MCS_VFS_ENOENT;
    st->is_dir = dir; st->size = size;
    return MCS_VFS_OK;
}
static int af_remove(void* ctx, const char* path) {
    bool dir; uint32_t size;
    if (!a_stat(ctx, path, &dir, &size)) return MCS_VFS_ENOENT;
    if (dir) return FS_OF(ctx)->rmdir(path) ? MCS_VFS_OK : MCS_VFS_ENOTEMPTY;
    return FS_OF(ctx)->remove(path) ? MCS_VFS_OK : MCS_VFS_EIO;
}
static int af_mkdir(void* ctx, const char* path) {
    bool dir; uint32_t size;
    if (a_stat(ctx, path, &dir, &size)) return MCS_VFS_EEXIST;
    return FS_OF(ctx)->mkdir(path) ? MCS_VFS_OK : MCS_VFS_EIO;
}
#if defined(MCS_ARDUINO_FSAPI)
static int af_rename(void* ctx, const char* from, const char* to) {
    bool dir; uint32_t size;
    if (!a_stat(ctx, from, &dir, &size)) return MCS_VFS_ENOENT;
    if (a_stat(ctx, to, &dir, &size)) return MCS_VFS_EEXIST;
    return FS_OF(ctx)->rename(from, to) ? MCS_VFS_OK : MCS_VFS_EIO;
}
#else
static int af_rename(void* ctx, const char* from, const char* to) {   /* the SD library cannot rename: copy + delete */
    bool dir; uint32_t size;
    if (!a_stat(ctx, from, &dir, &size)) return MCS_VFS_ENOENT;
    if (dir) return MCS_VFS_EINVAL;                       /* directories cannot be moved */
    if (a_stat(ctx, to, &dir, &size)) return MCS_VFS_EEXIST;
    FILE_T in = FS_OF(ctx)->open(from, FILE_READ);
    if (!in) return MCS_VFS_EIO;
    FILE_T out = FS_OF(ctx)->open(to, FILE_WRITE);
    if (!out) { in.close(); return MCS_VFS_EIO; }
    uint8_t buf[256];
    int r, e = MCS_VFS_OK;
    while ((r = (int)in.read(buf, sizeof buf)) > 0)
        if ((int)out.write(buf, (size_t)r) != r) { e = MCS_VFS_ENOSPC; break; }
    in.close(); out.close();
    if (e) { FS_OF(ctx)->remove(to); return e; }
    return FS_OF(ctx)->remove(from) ? MCS_VFS_OK : MCS_VFS_EIO;
}
#endif
#define AF_RENAME af_rename
static int af_list(void* ctx, const char* path, mcs_vfs_list_cb cb, void* ud) {
    bool dir; uint32_t size;
    if (!a_stat(ctx, path, &dir, &size)) return MCS_VFS_ENOENT;
    if (!dir) return MCS_VFS_ENOTDIR;
#if defined(ARDUINO_ARCH_RP2040) || defined(ESP8266)
    fs::Dir d = FS_OF(ctx)->openDir(path);
    while (d.next()) {
        mcs_vfs_stat_t vs = { d.isDirectory() ? 0 : (uint32_t)d.fileSize(), d.isDirectory() };
        if (cb(ud, base_name(d.fileName().c_str()), &vs)) break;
    }
#else
    FILE_T d = FS_OF(ctx)->open(path);
    if (!d) return MCS_VFS_EIO;
    for (;;) {
        FILE_T e = d.openNextFile();
        if (!e) break;
        mcs_vfs_stat_t vs = { e.isDirectory() ? 0 : (uint32_t)e.size(), e.isDirectory() };
        char name[MCS_VFS_PATH_MAX];
        strncpy(name, base_name(e.name()), sizeof name - 1);
        name[sizeof name - 1] = 0;
        e.close();
        if (cb(ud, name, &vs)) break;
    }
    d.close();
#endif
    return MCS_VFS_OK;
}
#if defined(MCS_ARDUINO_FSAPI)
static int af_statfs(void* ctx, mcs_vfs_statfs_t* st) {
    const mcs_arduino_fs_t* a = (const mcs_arduino_fs_t*)ctx;
    if (!a->total) return MCS_VFS_EINVAL;
    st->total = a->total();
    uint64_t used = a->used ? a->used() : 0;
    st->free = st->total > used ? st->total - used : 0;
    st->format = a->format ? a->format : "arduino";
    return MCS_VFS_OK;
}
#define AF_STATFS af_statfs
#else
#define AF_STATFS NULL
#endif

const mcs_vfs_ops_t mcs_arduino_fs_ops = { af_open, af_read, af_write, af_close, af_stat, af_remove, af_mkdir, AF_RENAME, af_list, AF_STATFS };
#endif
