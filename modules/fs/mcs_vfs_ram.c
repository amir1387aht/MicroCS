/* MicroCS - RAM filesystem backend. Volatile; useful for tests, scratch data and
 * as the default store on boards without flash filesystem support. */
#include "mcs_vfs.h"
#if MCS_ENABLE_FS
#include <string.h>
#include <stdlib.h>

typedef struct mcs_ramfs_node {
    struct mcs_ramfs_node* next;
    char* path;
    size_t path_len;
    bool dir;
    uint8_t* data;
    size_t len, cap;
} node_t;

typedef struct { node_t* n; size_t pos; int flags; } handle_t;

static void* ram_alloc(mcs_ramfs_t* fs, void* p, size_t old, size_t n) {
    if (fs->realloc_fn) return fs->realloc_fn(fs->alloc_ud, p, old, n);
    if (!n) { free(p); return NULL; }
    return realloc(p, n);
}

void mcs_ramfs_init(mcs_ramfs_t* fs, size_t limit, mcs_realloc_fn fn, void* ud) {
    memset(fs, 0, sizeof *fs);
    fs->limit = limit; fs->realloc_fn = fn; fs->alloc_ud = ud;
}

static void node_free(mcs_ramfs_t* fs, node_t* n) {
    if (n->data) ram_alloc(fs, n->data, n->cap, 0);
    fs->used -= n->cap;
    ram_alloc(fs, n->path, n->path_len + 1, 0);
    ram_alloc(fs, n, sizeof *n, 0);
}

void mcs_ramfs_free(mcs_ramfs_t* fs) {
    node_t* n = fs->nodes;
    while (n) { node_t* nx = n->next; node_free(fs, n); n = nx; }
    fs->nodes = NULL;
}

static node_t* find(mcs_ramfs_t* fs, const char* path) {
    for (node_t* n = fs->nodes; n; n = n->next) if (!strcmp(n->path, path)) return n;
    return NULL;
}

/* parent must be the root or an existing directory */
static int check_parent(mcs_ramfs_t* fs, const char* path) {
    const char* slash = strrchr(path, '/');
    if (!slash || slash == path) return MCS_VFS_OK;
    char parent[MCS_VFS_PATH_MAX];
    size_t n = (size_t)(slash - path);
    memcpy(parent, path, n); parent[n] = 0;
    node_t* p = find(fs, parent);
    if (!p) return MCS_VFS_ENOENT;
    return p->dir ? MCS_VFS_OK : MCS_VFS_ENOTDIR;
}

static node_t* create(mcs_ramfs_t* fs, const char* path, bool dir, int* err) {
    if ((*err = check_parent(fs, path))) return NULL;
    node_t* n = (node_t*)ram_alloc(fs, NULL, 0, sizeof *n);
    if (!n) { *err = MCS_VFS_ENOMEM; return NULL; }
    memset(n, 0, sizeof *n);
    n->path_len = strlen(path);
    n->path = (char*)ram_alloc(fs, NULL, 0, n->path_len + 1);
    if (!n->path) { ram_alloc(fs, n, sizeof *n, 0); *err = MCS_VFS_ENOMEM; return NULL; }
    memcpy(n->path, path, n->path_len + 1);
    n->dir = dir;
    n->next = fs->nodes;
    fs->nodes = n;
    return n;
}

static int r_open(void* ctx, const char* path, int flags, void** fh) {
    mcs_ramfs_t* fs = (mcs_ramfs_t*)ctx;
    if (!strcmp(path, "/")) return MCS_VFS_EISDIR;
    node_t* n = find(fs, path);
    int e = MCS_VFS_OK;
    if (n && n->dir) return MCS_VFS_EISDIR;
    if (!n) {
        if (!(flags & (MCS_VFS_WRITE | MCS_VFS_APPEND))) return MCS_VFS_ENOENT;
        if (!(n = create(fs, path, false, &e))) return e;
    }
    handle_t* h = (handle_t*)ram_alloc(fs, NULL, 0, sizeof *h);
    if (!h) return MCS_VFS_ENOMEM;
    h->n = n; h->flags = flags; h->pos = 0;
    if (flags & MCS_VFS_WRITE) n->len = 0;
    if (flags & MCS_VFS_APPEND) h->pos = n->len;
    *fh = h;
    return MCS_VFS_OK;
}

static int r_read(void* ctx, void* fh, void* buf, size_t len) {
    (void)ctx;
    handle_t* h = (handle_t*)fh;
    size_t avail = h->pos < h->n->len ? h->n->len - h->pos : 0;
    if (len > avail) len = avail;
    if (len > 0x7fffffff) len = 0x7fffffff;
    memcpy(buf, h->n->data + h->pos, len);
    h->pos += len;
    return (int)len;
}

static int r_write(void* ctx, void* fh, const void* buf, size_t len) {
    mcs_ramfs_t* fs = (mcs_ramfs_t*)ctx;
    handle_t* h = (handle_t*)fh;
    if (!(h->flags & (MCS_VFS_WRITE | MCS_VFS_APPEND))) return MCS_VFS_EACCES;
    node_t* n = h->n;
    if (len > 0x7fffffff) len = 0x7fffffff;
    size_t need = h->pos + len;
    if (need > n->cap) {
        size_t cap = n->cap ? n->cap : 32;
        while (cap < need) cap *= 2;
        if (fs->limit && fs->used - n->cap + cap > fs->limit) {
            cap = need;   /* retry with an exact fit before reporting ENOSPC */
            if (fs->used - n->cap + cap > fs->limit) return MCS_VFS_ENOSPC;
        }
        uint8_t* d = (uint8_t*)ram_alloc(fs, n->data, n->cap, cap);
        if (!d) return MCS_VFS_ENOMEM;
        fs->used += cap - n->cap;
        n->data = d; n->cap = cap;
    }
    memcpy(n->data + h->pos, buf, len);
    h->pos += len;
    if (h->pos > n->len) n->len = h->pos;
    return (int)len;
}

static int r_close(void* ctx, void* fh) { ram_alloc((mcs_ramfs_t*)ctx, fh, sizeof(handle_t), 0); return MCS_VFS_OK; }

static int r_stat(void* ctx, const char* path, mcs_vfs_stat_t* st) {
    if (!strcmp(path, "/")) { st->size = 0; st->is_dir = true; return MCS_VFS_OK; }
    node_t* n = find((mcs_ramfs_t*)ctx, path);
    if (!n) return MCS_VFS_ENOENT;
    st->size = (uint32_t)n->len; st->is_dir = n->dir;
    return MCS_VFS_OK;
}

static bool is_child_of(const node_t* n, const char* dir, size_t dl) {
    if (dl == 1) return n->path_len > 1;
    return n->path_len > dl && !strncmp(n->path, dir, dl) && n->path[dl] == '/';
}

static int r_remove(void* ctx, const char* path) {
    mcs_ramfs_t* fs = (mcs_ramfs_t*)ctx;
    node_t** pp = &fs->nodes;
    while (*pp && strcmp((*pp)->path, path)) pp = &(*pp)->next;
    if (!*pp) return MCS_VFS_ENOENT;
    node_t* n = *pp;
    if (n->dir) {
        size_t dl = strlen(path);
        for (node_t* c = fs->nodes; c; c = c->next) if (is_child_of(c, path, dl)) return MCS_VFS_ENOTEMPTY;
    }
    *pp = n->next;
    node_free(fs, n);
    return MCS_VFS_OK;
}

static int r_mkdir(void* ctx, const char* path) {
    mcs_ramfs_t* fs = (mcs_ramfs_t*)ctx;
    if (find(fs, path)) return MCS_VFS_EEXIST;
    int e;
    return create(fs, path, true, &e) ? MCS_VFS_OK : e;
}

static int r_rename(void* ctx, const char* from, const char* to) {
    mcs_ramfs_t* fs = (mcs_ramfs_t*)ctx;
    node_t* src = find(fs, from);
    if (!src) return MCS_VFS_ENOENT;
    if (find(fs, to)) return MCS_VFS_EEXIST;
    int e = check_parent(fs, to);
    if (e) return e;
    size_t fl = strlen(from), tl = strlen(to);
    if (src->dir && tl > fl && !strncmp(to, from, fl) && to[fl] == '/') return MCS_VFS_EINVAL;
    for (node_t* n = fs->nodes; n; n = n->next) {
        if (n != src && !(src->dir && is_child_of(n, from, fl))) continue;
        size_t nl = tl + (n->path_len - fl);
        char* p = (char*)ram_alloc(fs, NULL, 0, nl + 1);
        if (!p) return MCS_VFS_ENOMEM;
        memcpy(p, to, tl);
        memcpy(p + tl, n->path + fl, n->path_len - fl + 1);
        ram_alloc(fs, n->path, n->path_len + 1, 0);
        n->path = p; n->path_len = nl;
    }
    return MCS_VFS_OK;
}

static int r_list(void* ctx, const char* path, mcs_vfs_list_cb cb, void* ud) {
    mcs_ramfs_t* fs = (mcs_ramfs_t*)ctx;
    if (strcmp(path, "/")) {
        node_t* d = find(fs, path);
        if (!d) return MCS_VFS_ENOENT;
        if (!d->dir) return MCS_VFS_ENOTDIR;
    }
    size_t dl = strlen(path);
    /* nodes are kept newest-first; walk to report in creation order */
    node_t* items[64];
    int count = 0;
    for (node_t* n = fs->nodes; n; n = n->next) {
        if (!is_child_of(n, path, dl)) continue;
        const char* name = n->path + (dl == 1 ? 1 : dl + 1);
        if (strchr(name, '/')) continue;
        if (count < 64) items[count++] = n;
    }
    for (int i = count - 1; i >= 0; i--) {
        mcs_vfs_stat_t st = { (uint32_t)items[i]->len, items[i]->dir };
        if (cb(ud, items[i]->path + (dl == 1 ? 1 : dl + 1), &st)) break;
    }
    return MCS_VFS_OK;
}

static int r_statfs(void* ctx, mcs_vfs_statfs_t* st) {
    mcs_ramfs_t* fs = (mcs_ramfs_t*)ctx;
    uint64_t lim = fs->limit ? fs->limit : 0x80000000u;
    st->total = lim;
    st->free = fs->used < lim ? lim - fs->used : 0;
    st->format = "ramfs";
    return MCS_VFS_OK;
}
const mcs_vfs_ops_t mcs_ramfs_ops = { r_open, r_read, r_write, r_close, r_stat, r_remove, r_mkdir, r_rename, r_list, r_statfs };
#endif
