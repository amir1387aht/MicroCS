/* MicroCS - VFS mount table, path normalisation and whole-file helpers. */
#include "mcs_vfs.h"
#if MCS_ENABLE_FS
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void mcs_vfs_init(mcs_vfs_t* vfs) { memset(vfs, 0, sizeof *vfs); }

int mcs_vfs_normalize(const char* path, char* out, size_t cap) {
    if (!path || cap < 2) return MCS_VFS_EINVAL;
    size_t n = 0;
    out[n++] = '/';
    const char* p = path;
    while (*p) {
        while (*p == '/' || *p == '\\') p++;
        if (!*p) break;
        const char* s = p;
        while (*p && *p != '/' && *p != '\\') p++;
        size_t len = (size_t)(p - s);
        if (len == 1 && s[0] == '.') continue;
        if (len == 2 && s[0] == '.' && s[1] == '.') {
            /* clamp at the root instead of failing, like a chroot */
            while (n > 1 && out[n - 1] != '/') n--;
            if (n > 1) n--;
            continue;
        }
        if (n > 1) { if (n + 1 >= cap) return MCS_VFS_ENAMETOOLONG; out[n++] = '/'; }
        if (n + len >= cap) return MCS_VFS_ENAMETOOLONG;
        memcpy(out + n, s, len);
        n += len;
    }
    out[n] = 0;
    return MCS_VFS_OK;
}

int mcs_vfs_mount(mcs_vfs_t* vfs, const char* prefix, const mcs_vfs_ops_t* ops, void* ctx, uint8_t flags) {
    char norm[sizeof vfs->mounts[0].prefix];
    int e = mcs_vfs_normalize(prefix, norm, sizeof norm);
    if (e) return e;
    for (int i = 0; i < vfs->count; i++) if (!strcmp(vfs->mounts[i].prefix, norm)) return MCS_VFS_EEXIST;
    if (vfs->count >= MCS_VFS_MAX_MOUNTS) return MCS_VFS_ENOSPC;
    mcs_vfs_mount_t* m = &vfs->mounts[vfs->count++];
    strcpy(m->prefix, norm);
    m->ops = ops; m->ctx = ctx; m->flags = flags;
    return MCS_VFS_OK;
}

int mcs_vfs_umount(mcs_vfs_t* vfs, const char* prefix) {
    char norm[sizeof vfs->mounts[0].prefix];
    int e = mcs_vfs_normalize(prefix, norm, sizeof norm);
    if (e) return e;
    for (int i = 0; i < vfs->count; i++)
        if (!strcmp(vfs->mounts[i].prefix, norm)) {
            memmove(&vfs->mounts[i], &vfs->mounts[i + 1], (size_t)(vfs->count - i - 1) * sizeof vfs->mounts[0]);
            vfs->count--;
            return MCS_VFS_OK;
        }
    return MCS_VFS_ENOENT;
}

/* longest-prefix match; `rel` receives the backend path */
static mcs_vfs_mount_t* resolve(mcs_vfs_t* vfs, const char* path, char* rel, int* err) {
    char norm[MCS_VFS_PATH_MAX];
    *err = mcs_vfs_normalize(path, norm, sizeof norm);
    if (*err) return NULL;
    mcs_vfs_mount_t* best = NULL;
    size_t best_len = 0;
    for (int i = 0; i < vfs->count; i++) {
        const char* pre = vfs->mounts[i].prefix;
        size_t pl = strlen(pre);
        bool hit = pl == 1 || (!strncmp(norm, pre, pl) && (norm[pl] == 0 || norm[pl] == '/'));
        if (hit && (!best || pl > best_len)) { best = &vfs->mounts[i]; best_len = pl; }
    }
    if (!best) { *err = MCS_VFS_ENOENT; return NULL; }
    const char* r = best_len == 1 ? norm : norm + best_len;
    if (!*r) r = "/";
    strcpy(rel, r);
    return best;
}

#define RESOLVE(path) \
    char rel[MCS_VFS_PATH_MAX]; int err_; \
    mcs_vfs_mount_t* m = resolve(vfs, path, rel, &err_); \
    if (!m) return err_

int mcs_vfs_flags(mcs_vfs_t* vfs, const char* path) {
    char rel[MCS_VFS_PATH_MAX]; int e;
    mcs_vfs_mount_t* m = resolve(vfs, path, rel, &e);
    return m ? m->flags : 0;
}

int mcs_vfs_open(mcs_vfs_t* vfs, const char* path, int flags, mcs_vfs_file_t* f) {
    RESOLVE(path);
    if ((flags & (MCS_VFS_WRITE | MCS_VFS_APPEND)) && (m->flags & MCS_VFS_RDONLY)) return MCS_VFS_EACCES;
    f->m = m;
    return m->ops->open(m->ctx, rel, flags, &f->fh);
}
int mcs_vfs_read(mcs_vfs_file_t* f, void* buf, size_t n) { return f->m->ops->read(f->m->ctx, f->fh, buf, n); }
int mcs_vfs_write(mcs_vfs_file_t* f, const void* buf, size_t n) { return f->m->ops->write(f->m->ctx, f->fh, buf, n); }
int mcs_vfs_close(mcs_vfs_file_t* f) { return f->m->ops->close(f->m->ctx, f->fh); }

static bool is_mount_point(mcs_vfs_t* vfs, const char* norm) {
    for (int i = 0; i < vfs->count; i++) if (!strcmp(vfs->mounts[i].prefix, norm)) return true;
    return false;
}

int mcs_vfs_stat(mcs_vfs_t* vfs, const char* path, mcs_vfs_stat_t* st) {
    char norm[MCS_VFS_PATH_MAX];
    int e = mcs_vfs_normalize(path, norm, sizeof norm);
    if (e) return e;
    char rel[MCS_VFS_PATH_MAX];
    mcs_vfs_mount_t* m = resolve(vfs, norm, rel, &e);
    if (m && (e = m->ops->stat(m->ctx, rel, st)) == MCS_VFS_OK) return e;
    if (!strcmp(norm, "/") || is_mount_point(vfs, norm)) { st->size = 0; st->is_dir = true; return MCS_VFS_OK; }
    return m ? e : MCS_VFS_ENOENT;
}

int mcs_vfs_remove(mcs_vfs_t* vfs, const char* path) {
    RESOLVE(path);
    if (m->flags & MCS_VFS_RDONLY) return MCS_VFS_EACCES;
    if (!strcmp(rel, "/")) return MCS_VFS_EACCES;
    return m->ops->remove(m->ctx, rel);
}
int mcs_vfs_mkdir(mcs_vfs_t* vfs, const char* path) {
    RESOLVE(path);
    if (m->flags & MCS_VFS_RDONLY) return MCS_VFS_EACCES;
    if (!strcmp(rel, "/")) return MCS_VFS_EEXIST;
    return m->ops->mkdir(m->ctx, rel);
}
int mcs_vfs_rename(mcs_vfs_t* vfs, const char* from, const char* to) {
    char rel2[MCS_VFS_PATH_MAX]; int e2;
    RESOLVE(from);
    mcs_vfs_mount_t* m2 = resolve(vfs, to, rel2, &e2);
    if (!m2) return e2;
    if (m != m2) return MCS_VFS_EINVAL;
    if (m->flags & MCS_VFS_RDONLY) return MCS_VFS_EACCES;
    if (!m->ops->rename) return MCS_VFS_EINVAL;
    return m->ops->rename(m->ctx, rel, rel2);
}

typedef struct { mcs_vfs_list_cb cb; void* ud; int stopped; } list_wrap_t;
static int list_fwd(void* ud, const char* name, const mcs_vfs_stat_t* st) {
    list_wrap_t* w = (list_wrap_t*)ud;
    int r = w->cb(w->ud, name, st);
    if (r) w->stopped = 1;
    return r;
}

int mcs_vfs_list(mcs_vfs_t* vfs, const char* path, mcs_vfs_list_cb cb, void* ud) {
    char norm[MCS_VFS_PATH_MAX];
    int e = mcs_vfs_normalize(path, norm, sizeof norm);
    if (e) return e;
    char rel[MCS_VFS_PATH_MAX];
    mcs_vfs_mount_t* m = resolve(vfs, norm, rel, &e);
    list_wrap_t w = { cb, ud, 0 };
    bool any = false;
    if (m) {
        e = m->ops->list(m->ctx, rel, list_fwd, &w);
        if (e == MCS_VFS_OK) any = true;
        else if (!is_mount_point(vfs, norm) && strcmp(norm, "/")) return e;
        if (w.stopped) return MCS_VFS_OK;
    }
    /* mount points directly below `norm` appear as directories */
    size_t nl = strlen(norm);
    for (int i = 0; i < vfs->count; i++) {
        const char* pre = vfs->mounts[i].prefix;
        if (!strcmp(pre, norm) || strlen(pre) <= nl) continue;
        if (nl > 1 && (strncmp(pre, norm, nl) || pre[nl] != '/')) continue;
        const char* child = pre + (nl > 1 ? nl + 1 : 1);
        if (strchr(child, '/')) continue;
        mcs_vfs_stat_t st = { 0, true };
        any = true;
        if (cb(ud, child, &st)) return MCS_VFS_OK;
    }
    return any || !strcmp(norm, "/") ? MCS_VFS_OK : MCS_VFS_ENOENT;
}

static void* vfs_alloc(mcs_vfs_t* vfs, void* p, size_t old, size_t n) {
    if (vfs->realloc_fn) return vfs->realloc_fn(vfs->alloc_ud, p, old, n);
    if (!n) { free(p); return NULL; }
    return realloc(p, n);
}
void mcs_vfs_free(mcs_vfs_t* vfs, void* data, size_t len) { if (data) vfs_alloc(vfs, data, len + 1, 0); }

int mcs_vfs_read_file(mcs_vfs_t* vfs, const char* path, char** data, size_t* len) {
    *data = NULL; *len = 0;
    mcs_vfs_stat_t st;
    int e = mcs_vfs_stat(vfs, path, &st);
    if (e) return e;
    if (st.is_dir) return MCS_VFS_EISDIR;
    mcs_vfs_file_t f;
    if ((e = mcs_vfs_open(vfs, path, MCS_VFS_READ, &f))) return e;
    size_t cap = st.size, n = 0;
    char* buf = (char*)vfs_alloc(vfs, NULL, 0, cap + 1);
    if (!buf) { mcs_vfs_close(&f); return MCS_VFS_ENOMEM; }
    for (;;) {
        if (n == cap) {  /* file grew since stat (POSIX backends) */
            char* nb = (char*)vfs_alloc(vfs, buf, cap + 1, cap * 2 + 65);
            if (!nb) { vfs_alloc(vfs, buf, cap + 1, 0); mcs_vfs_close(&f); return MCS_VFS_ENOMEM; }
            buf = nb; cap = cap * 2 + 64;
        }
        int r = mcs_vfs_read(&f, buf + n, cap - n);
        if (r < 0) { vfs_alloc(vfs, buf, cap + 1, 0); mcs_vfs_close(&f); return r; }
        if (r == 0) break;
        n += (size_t)r;
    }
    mcs_vfs_close(&f);
    if (n != cap) {
        char* nb = (char*)vfs_alloc(vfs, buf, cap + 1, n + 1);
        if (nb) buf = nb;
        else { /* keep the larger block; record its real size for mcs_vfs_free */ n = cap; }
    }
    buf[n] = 0;
    *data = buf; *len = n;
    return MCS_VFS_OK;
}

int mcs_vfs_write_file(mcs_vfs_t* vfs, const char* path, const void* data, size_t len, bool append) {
    mcs_vfs_file_t f;
    int e = mcs_vfs_open(vfs, path, append ? MCS_VFS_APPEND : MCS_VFS_WRITE, &f);
    if (e) return e;
    const uint8_t* p = (const uint8_t*)data;
    while (len) {
        int w = mcs_vfs_write(&f, p, len);
        if (w <= 0) { mcs_vfs_close(&f); return w < 0 ? w : MCS_VFS_ENOSPC; }
        p += w; len -= (size_t)w;
    }
    return mcs_vfs_close(&f);
}

const char* mcs_vfs_strerror(int e) {
    switch (e) {
    case MCS_VFS_OK: return "ok";
    case MCS_VFS_ENOENT: return "no such file or directory";
    case MCS_VFS_EEXIST: return "already exists";
    case MCS_VFS_ENOTDIR: return "not a directory";
    case MCS_VFS_EISDIR: return "is a directory";
    case MCS_VFS_ENOSPC: return "no space left";
    case MCS_VFS_EACCES: return "permission denied";
    case MCS_VFS_ENOTEMPTY: return "directory not empty";
    case MCS_VFS_EINVAL: return "invalid argument";
    case MCS_VFS_ENOMEM: return "out of memory";
    case MCS_VFS_ENAMETOOLONG: return "path too long";
    default: return "I/O error";
    }
}

mcs_result_t mcs_exec_file(mcs_vm_t* vm, mcs_vfs_t* vfs, const char* path) {
    char* data; size_t len;
    int e;
    if (mcs_vfs_flags(vfs, path) & MCS_VFS_NOEXEC) e = MCS_VFS_EACCES;
    else e = mcs_vfs_read_file(vfs, path, &data, &len);
    if (e) return mcs_fail(vm, MCS_ERR_RUNTIME, "cannot run '%s': %s", path, mcs_vfs_strerror(e));
    /* the buffer is NUL-terminated, so source runs without a copy */
    mcs_result_t r = mcs_exec_auto(vm, path, data, mcs_is_image(data, len) ? len : len + 1);
    mcs_vfs_free(vfs, data, len);
    return r;
}
#endif
