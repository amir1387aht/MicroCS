/* MicroCS - VFS mount table, path normalisation and whole-file helpers. */
#include "mcs_vfs.h"
#if MCS_ENABLE_FS
#include <string.h>
#include <stdlib.h>

void mcs_vfs_init(mcs_vfs_t* vfs) { memset(vfs, 0, sizeof *vfs); }

/* backend call under the optional thread lock: LOCKED(lock, int_expr) */
static int locked_(const mcs_vfs_lock_t* l, int r) { if (l) l->leave(l->ud); return r; }
#define LOCKED(l, expr) ((l) ? ((l)->enter((l)->ud), locked_((l), (expr))) : (expr))

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

int mcs_vfs_statfs(mcs_vfs_t* vfs, const char* path, mcs_vfs_statfs_t* st) {
    RESOLVE(path);
    memset(st, 0, sizeof *st);
    st->mount = m->prefix;
    if (!m->ops->statfs) return MCS_VFS_EINVAL;
    int e = LOCKED(vfs->lock, m->ops->statfs(m->ctx, st));
    st->mount = m->prefix;
    if (st->free > st->total) st->free = st->total;
    return e;
}

int mcs_vfs_open(mcs_vfs_t* vfs, const char* path, int flags, mcs_vfs_file_t* f) {
    RESOLVE(path);
    if ((flags & MCS_VFS_WRITABLE) && (m->flags & MCS_VFS_RDONLY)) return MCS_VFS_EACCES;
    /* Legacy backends may ignore unfamiliar flags: fail rather than corrupt data. */
    if ((flags & (MCS_VFS_UPDATE | MCS_VFS_CREATE | MCS_VFS_EXCL | MCS_VFS_TRUNC)) && !m->ops->seek) return MCS_VFS_EINVAL;
    f->m = m; f->ops = m->ops; f->ctx = m->ctx;
    f->lock = vfs->lock;
    return LOCKED(vfs->lock, m->ops->open(m->ctx, rel, flags, &f->fh));
}
int mcs_vfs_read(mcs_vfs_file_t* f, void* buf, size_t n) { return LOCKED(f->lock, f->ops->read(f->ctx, f->fh, buf, n)); }
int mcs_vfs_write(mcs_vfs_file_t* f, const void* buf, size_t n) { return LOCKED(f->lock, f->ops->write(f->ctx, f->fh, buf, n)); }
int mcs_vfs_close(mcs_vfs_file_t* f) { return LOCKED(f->lock, f->ops->close(f->ctx, f->fh)); }
int mcs_vfs_seek(mcs_vfs_file_t* f, int32_t offset, int origin, int32_t* position) {
    if (!position || origin < 0 || origin > 2 || !f->ops->seek) return MCS_VFS_EINVAL;
    return LOCKED(f->lock, f->ops->seek(f->ctx, f->fh, offset, origin, position));
}
int mcs_vfs_flush(mcs_vfs_file_t* f) {
    return f->ops->flush ? LOCKED(f->lock, f->ops->flush(f->ctx, f->fh)) : MCS_VFS_OK;
}

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
    if (m && (e = LOCKED(vfs->lock, m->ops->stat(m->ctx, rel, st))) == MCS_VFS_OK) return e;
    if (!strcmp(norm, "/") || is_mount_point(vfs, norm)) { st->size = 0; st->is_dir = true; return MCS_VFS_OK; }
    return m ? e : MCS_VFS_ENOENT;
}

int mcs_vfs_remove(mcs_vfs_t* vfs, const char* path) {
    RESOLVE(path);
    if (m->flags & MCS_VFS_RDONLY) return MCS_VFS_EACCES;
    if (!strcmp(rel, "/")) return MCS_VFS_EACCES;
    return LOCKED(vfs->lock, m->ops->remove(m->ctx, rel));
}
int mcs_vfs_mkdir(mcs_vfs_t* vfs, const char* path) {
    RESOLVE(path);
    if (m->flags & MCS_VFS_RDONLY) return MCS_VFS_EACCES;
    if (!strcmp(rel, "/")) return MCS_VFS_EEXIST;
    return LOCKED(vfs->lock, m->ops->mkdir(m->ctx, rel));
}
int mcs_vfs_rename(mcs_vfs_t* vfs, const char* from, const char* to) {
    char rel2[MCS_VFS_PATH_MAX]; int e2;
    RESOLVE(from);
    mcs_vfs_mount_t* m2 = resolve(vfs, to, rel2, &e2);
    if (!m2) return e2;
    if (m != m2) return MCS_VFS_EINVAL;
    if (m->flags & MCS_VFS_RDONLY) return MCS_VFS_EACCES;
    if (!m->ops->rename) return MCS_VFS_EINVAL;
    return LOCKED(vfs->lock, m->ops->rename(m->ctx, rel, rel2));
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
        e = LOCKED(vfs->lock, m->ops->list(m->ctx, rel, list_fwd, &w));
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
    if (cap == SIZE_MAX) { mcs_vfs_close(&f); return MCS_VFS_ENOMEM; }
    char* buf = (char*)vfs_alloc(vfs, NULL, 0, cap + 1);
    if (!buf) { mcs_vfs_close(&f); return MCS_VFS_ENOMEM; }
    for (;;) {
        if (n == cap) {
            /* Use the terminator byte to probe for EOF. A file of exactly the
             * stat size must not need a second, twice-as-large allocation. */
            int r = mcs_vfs_read(&f, buf + n, 1);
            if (r <= 0) {
                if (r < 0) { vfs_alloc(vfs, buf, cap + 1, 0); mcs_vfs_close(&f); return r; }
                break;
            }
            if (cap > (SIZE_MAX - 65) / 2) {
                vfs_alloc(vfs, buf, cap + 1, 0); mcs_vfs_close(&f); return MCS_VFS_ENOMEM;
            }
            size_t next = cap * 2 + 64;
            char* nb = (char*)vfs_alloc(vfs, buf, cap + 1, next + 1);
            if (!nb) { vfs_alloc(vfs, buf, cap + 1, 0); mcs_vfs_close(&f); return MCS_VFS_ENOMEM; }
            buf = nb; cap = next; n++;
        }
        int r = mcs_vfs_read(&f, buf + n, cap - n);
        if (r < 0) { vfs_alloc(vfs, buf, cap + 1, 0); mcs_vfs_close(&f); return r; }
        if (r == 0) break;
        n += (size_t)r;
    }
    mcs_vfs_close(&f);
    if (n != cap) {
        char* nb = (char*)vfs_alloc(vfs, buf, cap + 1, n + 1);
        if (!nb) { vfs_alloc(vfs, buf, cap + 1, 0); return MCS_VFS_ENOMEM; }
        buf = nb;   /* len stays the bytes read, never uninitialized capacity */
    }
    buf[n] = 0;
    *data = buf; *len = n;
    return MCS_VFS_OK;
}

int mcs_vfs_check_space(mcs_vfs_t* vfs, const char* path, uint64_t len, bool append) {
    mcs_vfs_statfs_t st;
    if (mcs_vfs_statfs(vfs, path, &st) || !st.total) return MCS_VFS_OK;   /* cannot tell */
    uint64_t avail = st.free;
    mcs_vfs_stat_t fs;
    if (!append && !mcs_vfs_stat(vfs, path, &fs) && !fs.is_dir) avail += fs.size;   /* replaced */
    /* flash filesystems count free space in whole blocks and keep small files inline in
     * metadata (LittleFS can report 0 free blocks and still store a 30-byte file): only
     * refuse what clearly cannot fit - one block (4 KB, at most 1/8 of the volume) of slack */
    uint64_t slack = st.total / 8 < 4096 ? st.total / 8 : 4096;
    avail += slack;
    return len > avail ? MCS_VFS_ENOSPC : MCS_VFS_OK;
}

int mcs_vfs_write_file(mcs_vfs_t* vfs, const char* path, const void* data, size_t len, bool append) {
    /* refuse up front: a write that runs out of space half-way would already have
     * truncated the old contents */
    int se = mcs_vfs_check_space(vfs, path, len, append);
    if (se) return se;
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

#if MCS_ENABLE_COMPILER
typedef struct {
    uint32_t path;            /* offset in source_set_t.paths */
    uint8_t state;             /* 0 unseen, 1 visiting, 2 loaded/checked */
} source_file_t;
typedef struct { char* data; size_t len, cap; } name_list_t;
typedef struct {
    mcs_vm_t* vm;
    mcs_vfs_t* vfs;
    source_file_t* files;
    size_t count, cap, root;
    bool check;
    name_list_t paths;
} source_set_t;

static bool cs_id0(char c) { return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static bool cs_id(char c) { return cs_id0(c) || (c >= '0' && c <= '9'); }
static bool cs_extension(const char* path) {
    size_t n = strlen(path);
    return n >= 3 && path[n - 3] == '.' && (path[n - 2] == 'c' || path[n - 2] == 'C')
        && (path[n - 1] == 's' || path[n - 1] == 'S');
}
static const char* cs_space(const char* p) {
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (p[0] == '/' && p[1] == '/') { p += 2; while (*p && *p != '\n') p++; continue; }
        if (p[0] == '/' && p[1] == '*') {
            p += 2; while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        return p;
    }
}
static const char* cs_string(const char* p, bool verbatim) {
    char q = *p++;
    if (q == '"' && p[0] == '"' && p[1] == '"') {
        p += 2;
        while (*p && !(p[0] == '"' && p[1] == '"' && p[2] == '"')) p++;
        return *p ? p + 3 : p;
    }
    while (*p) {
        if (*p == q) {
            if (verbatim && q == '"' && p[1] == '"') { p += 2; continue; }
            return p + 1;
        }
        if (!verbatim && *p == '\\' && p[1]) p += 2; else p++;
    }
    return p;
}
static bool cs_word(const char* p, const char* word) {
    size_t n = strlen(word);
    for (size_t i = 0; i < n; i++) if (!p[i] || p[i] != word[i]) return false;
    return !cs_id(p[n]);
}
/* Finds the next namespace name after `using` or `namespace`. Strings and
 * comments are ignored; aliases and `using static` are not source imports. */
static bool next_namespace(const char* src, size_t* off, bool declarations, char* out, size_t cap) {
    const char* p = src + *off;
    while (*p) {
        if (p[0] == '/' && (p[1] == '/' || p[1] == '*')) { p = cs_space(p); continue; }
        if (*p == '"' || *p == '\'') {
            bool verbatim = *p == '"' && ((p > src && p[-1] == '@') ||
                (p - src > 1 && p[-1] == '$' && p[-2] == '@'));
            p = cs_string(p, verbatim); continue;
        }
        if (!cs_id0(*p)) { p++; continue; }
        const char* w = p++; while (cs_id(*p)) p++;
        size_t wn = (size_t)(p - w);
        const char* keyword = declarations ? "namespace" : "using";
        size_t kn = strlen(keyword);
        if (wn != kn || memcmp(w, keyword, kn)) continue;
        p = cs_space(p);
        if (!declarations && (cs_word(p, "static") || cs_word(p, "var"))) continue;
        if (!cs_id0(*p)) continue;       /* using (...), using var, malformed */
        size_t n = 0;
        for (;;) {
            const char* id = p++; while (cs_id(*p)) p++;
            size_t ilen = (size_t)(p - id);
            if (n && n + 1 < cap) out[n++] = '.';
            if (n + ilen >= cap) { n = 0; break; }
            memcpy(out + n, id, ilen); n += ilen;
            p = cs_space(p);
            if (*p != '.') break;
            p = cs_space(p + 1);
            if (!cs_id0(*p)) { n = 0; break; }
        }
        if (!n || (declarations ? (*p != '{' && *p != ';') : *p != ';')) continue;
        out[n] = 0; *off = (size_t)(p - src) + 1;
        return true;
    }
    *off = (size_t)(p - src);
    return false;
}
static bool declares_namespace(const char* src, const char* wanted) {
    char name[MCS_VFS_PATH_MAX]; size_t off = 0;
    while (next_namespace(src, &off, true, name, sizeof name))
        if (!strcmp(name, wanted)) return true;
    return false;
}
static int add_name(mcs_vfs_t* vfs, name_list_t* list, const char* name) {
    size_t n = strlen(name) + 1;
    if (list->len > SIZE_MAX - n) return MCS_VFS_ENOMEM;
    if (list->len + n > list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 64;
        if (cap < list->cap) return MCS_VFS_ENOMEM;
        while (cap < list->len + n) {
            if (cap > SIZE_MAX / 2) return MCS_VFS_ENOMEM;
            cap *= 2;
        }
        char* p = (char*)vfs_alloc(vfs, list->data, list->cap, cap);
        if (!p) return MCS_VFS_ENOMEM;
        list->data = p; list->cap = cap;
    }
    memcpy(list->data + list->len, name, n); list->len += n;
    return MCS_VFS_OK;
}
static int collect_usings(mcs_vfs_t* vfs, const char* src, name_list_t* list) {
    char name[MCS_VFS_PATH_MAX]; size_t off = 0;
    while (next_namespace(src, &off, false, name, sizeof name)) {
        bool duplicate = false;
        for (size_t i = 0; i < list->len; i += strlen(list->data + i) + 1)
            if (!strcmp(list->data + i, name)) { duplicate = true; break; }
        if (!duplicate) { int e = add_name(vfs, list, name); if (e) return e; }
    }
    return MCS_VFS_OK;
}
static int add_unique_name(mcs_vfs_t* vfs, name_list_t* list, const char* name) {
    for (size_t i = 0; i < list->len; i += strlen(list->data + i) + 1)
        if (!strcmp(list->data + i, name)) return MCS_VFS_OK;
    return add_name(vfs, list, name);
}
static int add_source(source_set_t* set, const char* path) {
    for (size_t i = 0; i < set->count; i++)
        if (!strcmp(set->paths.data + set->files[i].path, path)) return (int)i;
    if (set->paths.len > UINT32_MAX) return MCS_VFS_ENOMEM;
    if (set->count == set->cap) {
        if (set->cap > SIZE_MAX / 2 / sizeof(*set->files)) return MCS_VFS_ENOMEM;
        size_t cap = set->cap ? set->cap * 2 : 4;
        source_file_t* p = (source_file_t*)vfs_alloc(set->vfs, set->files,
            set->cap * sizeof(*p), cap * sizeof(*p));
        if (!p) return MCS_VFS_ENOMEM;
        set->files = p; set->cap = cap;
    }
    uint32_t offset = (uint32_t)set->paths.len;
    int e = add_name(set->vfs, &set->paths, path);
    if (e) return e;
    source_file_t* f = &set->files[set->count];
    memset(f, 0, sizeof *f);
    f->path = offset;
    return (int)set->count++;
}
static const char* source_path(const source_set_t* set, size_t at) {
    return set->paths.data + set->files[at].path;
}
static int join_source_path(const char* dir, const char* name, char* path) {
    size_t dl = strlen(dir), nl = strlen(name);
    size_t slash = dl == 1 ? 0 : 1;
    if (dl + slash + nl >= MCS_VFS_PATH_MAX) return MCS_VFS_ENAMETOOLONG;
    memcpy(path, dir, dl);
    if (slash) path[dl++] = '/';
    memcpy(path + dl, name, nl + 1);
    return MCS_VFS_OK;
}
typedef struct {
    source_set_t* set;
    const char* dir;
    name_list_t dirs;
    int error;
} source_scan_t;
static int collect_source(void* ud, const char* name, const mcs_vfs_stat_t* st) {
    source_scan_t* scan = (source_scan_t*)ud;
    if (!strncmp(name, ".studio", 7)) return 0;
    char path[MCS_VFS_PATH_MAX];
    int e = join_source_path(scan->dir, name, path);
    if (e) { scan->error = e; return 1; }
    if (st->is_dir) e = add_unique_name(scan->set->vfs, &scan->dirs, path);
    else if (cs_extension(name)) {
        int added = add_source(scan->set, path);
        if (added < 0) e = added;
    }
    if (e) { scan->error = e; return 1; }
    return 0;
}
#define MCS_SOURCE_SCAN_DEPTH 16
static int scan_sources(source_set_t* set, const char* dir, unsigned depth) {
    source_scan_t scan;
    memset(&scan, 0, sizeof scan);
    scan.set = set; scan.dir = dir;
    int e = mcs_vfs_list(set->vfs, dir, collect_source, &scan);
    if (!e) e = scan.error;
    if (!e && depth < MCS_SOURCE_SCAN_DEPTH) {
        for (size_t i = 0; i < scan.dirs.len; i += strlen(scan.dirs.data + i) + 1) {
            e = scan_sources(set, scan.dirs.data + i, depth + 1);
            if (e) break;
        }
    }
    if (scan.dirs.data) vfs_alloc(set->vfs, scan.dirs.data, scan.dirs.cap, 0);
    return e;
}
static void free_source_set(source_set_t* set) {
    if (set->files) vfs_alloc(set->vfs, set->files, set->cap * sizeof(*set->files), 0);
    if (set->paths.data) vfs_alloc(set->vfs, set->paths.data, set->paths.cap, 0);
}
static mcs_result_t source_error(source_set_t* set, const char* action, const char* path, int e) {
    return mcs_fail(set->vm, e == MCS_VFS_ENOMEM ? MCS_ERR_MEMORY : MCS_ERR_RUNTIME,
                    "cannot %s '%s': %s", action, path, mcs_vfs_strerror(e));
}
/* Keep dependency depth off the native MCU stack. Each suspended visit owns
 * only its using list and scan cursors, charged to the VFS scratch allocator. */
typedef struct {
    size_t at, use, candidate;
    name_list_t uses;
} source_visit_t;
static mcs_result_t visit_source(source_set_t* set, size_t at) {
    source_visit_t* visits = NULL;
    size_t count = 0, cap = 0;
    mcs_result_t result = MCS_OK;
    bool push = true;
    for (;;) {
        if (push) {
            if (count == cap) {
                size_t next = cap ? cap * 2 : 4;
                if (next < cap || next > SIZE_MAX / sizeof(*visits)) {
                    result = source_error(set, "resolve", source_path(set, at), MCS_VFS_ENOMEM);
                    break;
                }
                source_visit_t* grown = (source_visit_t*)vfs_alloc(set->vfs, visits,
                    cap * sizeof(*visits), next * sizeof(*visits));
                if (!grown) {
                    result = source_error(set, "resolve", source_path(set, at), MCS_VFS_ENOMEM);
                    break;
                }
                visits = grown; cap = next;
            }
            source_visit_t* visit = &visits[count++];
            memset(visit, 0, sizeof *visit); visit->at = at;
            set->files[at].state = 1;
            const char* path = source_path(set, at);
            char* src = NULL; size_t len = 0;
            int e = (mcs_vfs_flags(set->vfs, path) & MCS_VFS_NOEXEC) ? MCS_VFS_EACCES
                : mcs_vfs_read_file(set->vfs, path, &src, &len);
            if (!e) e = collect_usings(set->vfs, src, &visit->uses);
            mcs_vfs_free(set->vfs, src, len);
            if (e) { result = source_error(set, set->check ? "check" : "run", path, e); break; }
            push = false;
        }
        source_visit_t* visit = &visits[count - 1];
        while (visit->use < visit->uses.len && !push) {
            while (visit->candidate < set->count) {
                size_t i = visit->candidate++;
                if (i == visit->at || set->files[i].state) continue;
                char* candidate; size_t len;
                const char* path = source_path(set, i);
                int e = mcs_vfs_read_file(set->vfs, path, &candidate, &len);
                if (e) { result = source_error(set, set->check ? "check" : "run", path, e); goto done; }
                bool provider = declares_namespace(candidate, visit->uses.data + visit->use);
                mcs_vfs_free(set->vfs, candidate, len);
                if (provider) { at = i; push = true; break; }
            }
            if (!push) {
                visit->use += strlen(visit->uses.data + visit->use) + 1;
                visit->candidate = 0;
            }
        }
        if (push) continue;
        at = visit->at;
        const char* path = source_path(set, at);
        char* src; size_t len;
        int e = mcs_vfs_read_file(set->vfs, path, &src, &len);
        if (e) { result = source_error(set, set->check ? "check" : "run", path, e); break; }
        result = set->check ? mcs_check_source(set->vm, path, src)
            : at == set->root ? mcs_exec_source(set->vm, path, src)
                             : mcs_exec_declarations(set->vm, path, src);
        mcs_vfs_free(set->vfs, src, len);
        if (result != MCS_OK) break;
        set->files[at].state = 2;
        if (visit->uses.data) vfs_alloc(set->vfs, visit->uses.data, visit->uses.cap, 0);
        count--;
        if (!count) break;
    }
done:
    for (size_t i = 0; i < count; i++)
        if (visits[i].uses.data) vfs_alloc(set->vfs, visits[i].uses.data, visits[i].uses.cap, 0);
    if (visits) vfs_alloc(set->vfs, visits, cap * sizeof(*visits), 0);
    return result;
}
static mcs_result_t resolve_source_file(mcs_vm_t* vm, mcs_vfs_t* vfs, const char* path, bool check) {
    /* Source buffers use the VFS allocator, before the compiler's own entry
     * safepoint. Release unreachable prior-run objects before this first read,
     * but never collect under a nested native call with unrooted C locals. */
    if (!mcs_running(vm)) mcs_gc(vm);
    source_set_t set;
    memset(&set, 0, sizeof set);
    set.vm = vm; set.vfs = vfs; set.check = check;
    char norm[MCS_VFS_PATH_MAX];
    int e = mcs_vfs_normalize(path, norm, sizeof norm);
    if (e) return mcs_fail(vm, MCS_ERR_RUNTIME, "cannot %s '%s': %s",
                           check ? "check" : "run", path, mcs_vfs_strerror(e));
    char* root_src; size_t root_len;
    e = (mcs_vfs_flags(vfs, norm) & MCS_VFS_NOEXEC) ? MCS_VFS_EACCES
        : mcs_vfs_read_file(vfs, norm, &root_src, &root_len);
    if (e) return mcs_fail(vm, e == MCS_VFS_ENOMEM ? MCS_ERR_MEMORY : MCS_ERR_RUNTIME,
                           "cannot %s '%s': %s", check ? "check" : "run", norm, mcs_vfs_strerror(e));
    if (!check && mcs_is_image(root_src, root_len)) {
        mcs_result_t r = mcs_exec_auto(vm, norm, root_src, root_len);
        mcs_vfs_free(vfs, root_src, root_len);
        return r;
    }
    name_list_t root_uses = {0};
    e = collect_usings(vfs, root_src, &root_uses);
    if (e) {
        if (root_uses.data) vfs_alloc(vfs, root_uses.data, root_uses.cap, 0);
        mcs_vfs_free(vfs, root_src, root_len);
        return mcs_fail(vm, MCS_ERR_MEMORY, "cannot %s '%s': %s",
                        check ? "check" : "run", norm, mcs_vfs_strerror(e));
    }
    if (!root_uses.len) {
        mcs_result_t r = check ? mcs_check_source(vm, norm, root_src)
                               : mcs_exec_source(vm, norm, root_src);
        if (root_uses.data) vfs_alloc(vfs, root_uses.data, root_uses.cap, 0);
        mcs_vfs_free(vfs, root_src, root_len);
        return r;
    }
    vfs_alloc(vfs, root_uses.data, root_uses.cap, 0);
    mcs_vfs_free(vfs, root_src, root_len);
    int root = add_source(&set, norm);
    if (root < 0) {
        free_source_set(&set);
        return mcs_fail(vm, root == MCS_VFS_ENOMEM ? MCS_ERR_MEMORY : MCS_ERR_RUNTIME,
                        "cannot scan '/': %s", mcs_vfs_strerror(root));
    }
    set.root = (size_t)root;
    e = scan_sources(&set, "/", 0);
    /* VFS listing synthesizes only direct mount children. A nested mount can
     * have no backing parent directory, so also enter nested mount roots.
     * add_source deduplicates files already reached through the root walk. */
    for (int i = 0; !e && i < vfs->count; i++) {
        const char* prefix = vfs->mounts[i].prefix;
        if (strchr(prefix + 1, '/')) e = scan_sources(&set, prefix, 0);
    }
    if (e) {
        free_source_set(&set);
        return mcs_fail(vm, e == MCS_VFS_ENOMEM ? MCS_ERR_MEMORY : MCS_ERR_RUNTIME,
                        "cannot scan '/': %s", mcs_vfs_strerror(e));
    }
    mcs_result_t r = visit_source(&set, set.root);
    free_source_set(&set);
    return r;
}

mcs_result_t mcs_check_file(mcs_vm_t* vm, mcs_vfs_t* vfs, const char* path) {
    return resolve_source_file(vm, vfs, path, true);
}
#endif

mcs_result_t mcs_exec_file(mcs_vm_t* vm, mcs_vfs_t* vfs, const char* path) {
#if MCS_ENABLE_COMPILER
    if (cs_extension(path)) return resolve_source_file(vm, vfs, path, false);
#endif
    char* data; size_t len;
    int e = (mcs_vfs_flags(vfs, path) & MCS_VFS_NOEXEC) ? MCS_VFS_EACCES
            : mcs_vfs_read_file(vfs, path, &data, &len);
    if (e) return mcs_fail(vm, MCS_ERR_RUNTIME, "cannot run '%s': %s", path, mcs_vfs_strerror(e));
    mcs_result_t r = mcs_exec_auto(vm, path, data, mcs_is_image(data, len) ? len : len + 1);
    mcs_vfs_free(vfs, data, len);
    return r;
}
#endif
