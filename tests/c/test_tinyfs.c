/* TinyFS (modules/fs/mcs_vfs_tinyfs.c) on a simulated internal flash:
 * built-in filesystem tests, flash-physics checks (program only clears bits,
 * every program unit written once per erase), a randomised model check,
 * wear spread, and a power cut injected at every single program/erase step of
 * a long scenario (with garbage collection) followed by a remount and
 * consistency check. The flash is reached through mcs_intflash_init(), the
 * same port table a user supplies for an unsupported MCU. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcs_vfs.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---------------- simulated internal flash ---------------- */
typedef struct {
    uint8_t* mem; uint8_t* written;
    uint32_t size, erase, ws;
    long ops, limit;              /* limit < 0: no power cut; ops > limit: cut */
    int dead;
    long viol, erases, progs;
    long block_erases[64];
    unsigned rnd;
    uint8_t erased_to;
} sim_t;

static unsigned rnd_next(unsigned* s) { *s = *s * 1103515245u + 12345u; return (*s >> 16) & 0x7fff; }

static void sim_init(sim_t* s, uint32_t size, uint32_t erase, uint32_t ws) {
    memset(s, 0, sizeof *s);
    s->mem = (uint8_t*)malloc(size); s->written = (uint8_t*)calloc(size / (ws ? ws : 1), 1);
    memset(s->mem, 0xFF, size);
    s->size = size; s->erase = erase; s->ws = ws; s->limit = -1; s->rnd = 7;
}
static void sim_free(sim_t* s) { free(s->mem); free(s->written); }

static int sim_read(void* c, uint32_t off, void* b, uint32_t n) {
    sim_t* s = (sim_t*)c;
    if ((uint64_t)off + n > s->size) { s->viol++; return -1; }
    memcpy(b, s->mem + off, n);
    return 0;
}
static int sim_write(void* c, uint32_t off, const void* b, uint32_t n) {
    sim_t* s = (sim_t*)c;
    uint32_t ws = s->ws ? s->ws : 1;
    if ((uint64_t)off + n > s->size || off % ws || n % ws || ((uintptr_t)b & 3)) { s->viol++; return -1; }
    if (s->dead) return -1;
    s->ops++; s->progs++;
    const uint8_t* p = (const uint8_t*)b;
    uint32_t units = n / ws, cut = units;
    int power_cut = s->limit >= 0 && s->ops > s->limit;
    if (power_cut) cut = (uint32_t)rnd_next(&s->rnd) % (units + 1);        /* units fully written */
    for (uint32_t u = 0; u < units; u++) {
        uint32_t a = off + u * ws;
        int blank = 1;
        for (uint32_t i = 0; i < ws; i++) if (p[u * ws + i] != 0xFF) blank = 0;
        if (blank && u >= cut) continue;
        if (u > cut) break;
        if (!blank) {
            if (s->written[a / ws]) s->viol++;                              /* programmed twice since erase */
            s->written[a / ws] = 1;
        }
        for (uint32_t i = 0; i < ws; i++) {
            uint8_t v = p[u * ws + i];
            if (u == cut) v |= (uint8_t)rnd_next(&s->rnd);                 /* the torn unit: some bits only */
            s->mem[a + i] &= v;
        }
    }
    if (power_cut) { s->dead = 1; return -1; }
    return 0;
}
static int sim_erase(void* c, uint32_t off) {
    sim_t* s = (sim_t*)c;
    if (off % s->erase || off >= s->size) { s->viol++; return -1; }
    if (s->dead) return -1;
    s->ops++; s->erases++; s->block_erases[off / s->erase]++;
    uint32_t ws = s->ws ? s->ws : 1;
    int power_cut = s->limit >= 0 && s->ops > s->limit;
    for (uint32_t i = 0; i < s->erase; i++) {
        if (power_cut && (rnd_next(&s->rnd) & 1)) s->mem[off + i] = (uint8_t)(s->mem[off + i] | rnd_next(&s->rnd));
        else s->mem[off + i] = 0xFF;
    }
    if (!power_cut) memset(s->written + off / ws, 0, s->erase / ws);
    else { memset(s->written + off / ws, 0, s->erase / ws); s->dead = 1; return -1; }
    return 0;
}
static void sim_reboot(sim_t* s) { s->dead = 0; s->limit = -1; }

typedef struct { sim_t sim; mcs_flash_port_t port; mcs_intflash_t dev; mcs_tinyfs_t fs; mcs_flash_part_t part; } rig_t;
static int rig_init(rig_t* r, uint32_t size, uint32_t erase, uint32_t ws) {
    sim_init(&r->sim, size, erase, ws);
    r->port = (mcs_flash_port_t){ size, erase, ws, sim_read, sim_write, sim_erase, &r->sim };
    int e = mcs_intflash_init(&r->dev, &r->port);
    r->part = (mcs_flash_part_t){ &r->dev.flash, 0, 0 };
    return e;
}
static int rig_mount(rig_t* r, int flags) { return mcs_tinyfs_mount(&r->fs, &r->part, flags); }

/* ---------------- helpers over the vfs ops ---------------- */
static const mcs_vfs_ops_t* O = &mcs_tinyfs_ops;

static int put(mcs_tinyfs_t* fs, const char* path, const void* d, size_t n, int flags, size_t piece) {
    void* fh;
    int r = O->open(fs, path, flags, &fh);
    if (r) return r;
    const uint8_t* p = (const uint8_t*)d;
    size_t o = 0;
    int err = 0;
    while (o < n && !err) {
        size_t k = n - o < piece ? n - o : piece;
        int w = O->write(fs, fh, p + o, k);
        if (w < 0) err = w; else o += k;
    }
    int c = O->close(fs, fh);
    return err ? err : c;
}
static long slurp(mcs_tinyfs_t* fs, const char* path, uint8_t* buf, size_t cap) {
    void* fh;
    int r = O->open(fs, path, MCS_VFS_READ, &fh);
    if (r) return r;
    long total = 0;
    for (;;) {
        int k = O->read(fs, fh, buf + total, cap - (size_t)total < 37 ? cap - (size_t)total : 37);
        if (k <= 0) { if (k < 0) total = k; break; }
        total += k;
    }
    O->close(fs, fh);
    return total;
}
static void fill(uint8_t* b, size_t n, unsigned seed) {
    unsigned s = seed * 2654435761u + 1;
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(rnd_next(&s) >> 3);
}
static int same(mcs_tinyfs_t* fs, const char* path, const uint8_t* d, size_t n) {
    uint8_t* b = (uint8_t*)malloc(n + 8);
    long k = slurp(fs, path, b, n + 8);
    int ok = k == (long)n && !memcmp(b, d, n);
    free(b);
    return ok;
}
typedef struct { char names[40][40]; int dir[40]; uint32_t size[40]; int n; } listing_t;
static int list_cb(void* ud, const char* name, const mcs_vfs_stat_t* st) {
    listing_t* l = (listing_t*)ud;
    if (l->n < 40) { snprintf(l->names[l->n], 40, "%s", name); l->dir[l->n] = st->is_dir; l->size[l->n] = st->size; l->n++; }
    return 0;
}

/* ---------------- 1: basics ---------------- */
static void test_basics(uint32_t size, uint32_t erase, uint32_t ws) {
    rig_t* r = (rig_t*)calloc(1, sizeof *r);
    CHECK(rig_init(r, size, erase, ws) == 0);
    CHECK(rig_mount(r, 0) == MCS_VFS_EIO);                       /* blank flash is not a filesystem */
    CHECK(rig_mount(r, MCS_FLASHFS_FORMAT_IF_NEEDED) == 0);
    mcs_tinyfs_t* fs = &r->fs;
    mcs_vfs_statfs_t sf;
    CHECK(O->statfs(fs, &sf) == 0 && !strcmp(sf.format, "tinyfs") && sf.total > size / 3 && sf.free == sf.total);

    uint8_t a[300], b[1000], buf[1200];
    fill(a, sizeof a, 1); fill(b, sizeof b, 2);
    CHECK(put(fs, "/a.txt", a, sizeof a, MCS_VFS_WRITE, 50) == 0);
    CHECK(put(fs, "/big.bin", b, sizeof b, MCS_VFS_WRITE, 333) == 0);
    CHECK(same(fs, "/a.txt", a, sizeof a) && same(fs, "/big.bin", b, sizeof b));
    mcs_vfs_stat_t st;
    CHECK(O->stat(fs, "/a.txt", &st) == 0 && st.size == sizeof a && !st.is_dir);
    CHECK(O->stat(fs, "/", &st) == 0 && st.is_dir);
    CHECK(O->stat(fs, "/nope", &st) == MCS_VFS_ENOENT);
    CHECK(put(fs, "/a.txt", "XY", 2, MCS_VFS_APPEND, 1) == 0);
    CHECK(O->stat(fs, "/a.txt", &st) == 0 && st.size == sizeof a + 2);
    CHECK(slurp(fs, "/a.txt", buf, sizeof buf) == (long)sizeof a + 2 && !memcmp(buf, a, sizeof a) && buf[300] == 'X' && buf[301] == 'Y');
    CHECK(put(fs, "/a.txt", "new", 3, MCS_VFS_WRITE, 3) == 0);                /* truncates */
    CHECK(same(fs, "/a.txt", (const uint8_t*)"new", 3));
    CHECK(put(fs, "/empty", "", 0, MCS_VFS_WRITE, 1) == 0 && slurp(fs, "/empty", buf, sizeof buf) == 0);
    CHECK(put(fs, "/new/x", "1", 1, MCS_VFS_WRITE, 1) == MCS_VFS_ENOENT);       /* no parent */
    CHECK(O->mkdir(fs, "/d") == 0 && O->mkdir(fs, "/d") == MCS_VFS_EEXIST);
    CHECK(O->mkdir(fs, "/x/y") == MCS_VFS_ENOENT);
    CHECK(put(fs, "/d/in.txt", "inside", 6, MCS_VFS_WRITE, 4) == 0);
    CHECK(put(fs, "/a.txt/x", "1", 1, MCS_VFS_WRITE, 1) == MCS_VFS_ENOTDIR);
    void* fh;
    CHECK(O->open(fs, "/d", MCS_VFS_READ, &fh) == MCS_VFS_EISDIR);
    CHECK(O->open(fs, "/missing", MCS_VFS_READ, &fh) == MCS_VFS_ENOENT);
    listing_t l = { 0 };
    CHECK(O->list(fs, "/", list_cb, &l) == 0 && l.n == 4);
    {   /* creation order: a.txt was re-created by the truncating open */
        static const char* want[] = { "big.bin", "a.txt", "empty", "d" };
        for (int i = 0; i < 4; i++) CHECK(!strcmp(l.names[i], want[i]));
        CHECK(l.dir[3] && !l.dir[0] && l.size[0] == sizeof b && l.size[1] == 3);
    }
    memset(&l, 0, sizeof l);
    CHECK(O->list(fs, "/d", list_cb, &l) == 0 && l.n == 1 && !strcmp(l.names[0], "in.txt") && l.size[0] == 6);
    CHECK(O->list(fs, "/a.txt", list_cb, &l) == MCS_VFS_ENOTDIR && O->list(fs, "/zz", list_cb, &l) == MCS_VFS_ENOENT);
    CHECK(O->remove(fs, "/d") == MCS_VFS_ENOTEMPTY);
    CHECK(O->rename(fs, "/d/in.txt", "/moved.txt") == 0 && same(fs, "/moved.txt", (const uint8_t*)"inside", 6));
    CHECK(O->rename(fs, "/moved.txt", "/a.txt") == MCS_VFS_EEXIST && O->rename(fs, "/nope", "/q") == MCS_VFS_ENOENT);
    CHECK(O->rename(fs, "/d", "/dd") == 0 && O->stat(fs, "/dd", &st) == 0 && st.is_dir && O->stat(fs, "/d", &st) < 0);
    CHECK(O->remove(fs, "/dd") == 0 && O->remove(fs, "/dd") == MCS_VFS_ENOENT);
    CHECK(O->remove(fs, "/big.bin") == 0 && O->stat(fs, "/big.bin", &st) == MCS_VFS_ENOENT);

    /* long names and the table limit */
    char longp[200];
    memset(longp, 'n', sizeof longp - 1); longp[0] = '/'; longp[sizeof longp - 1] = 0;
    CHECK(put(fs, longp, "1", 1, MCS_VFS_WRITE, 1) == MCS_VFS_ENAMETOOLONG);
    mcs_vfs_statfs_t sf2;
    O->statfs(fs, &sf2);
    CHECK(sf2.free < sf2.total && sf2.free > sf2.total / 2);

    /* a removed file's open handle fails cleanly */
    CHECK(put(fs, "/gone", "abc", 3, MCS_VFS_WRITE, 3) == 0);
    CHECK(O->open(fs, "/gone", MCS_VFS_READ, &fh) == 0);
    CHECK(O->remove(fs, "/gone") == 0 && O->read(fs, fh, buf, 3) == MCS_VFS_ENOENT);
    O->close(fs, fh);
    /* writing through two handles, reading while writing */
    void *h1, *h2;
    CHECK(O->open(fs, "/w1", MCS_VFS_WRITE | MCS_VFS_READ, &h1) == 0 && O->open(fs, "/w2", MCS_VFS_WRITE, &h2) == 0);
    CHECK(O->open(fs, "/w3", MCS_VFS_WRITE, &fh) == MCS_VFS_ENOMEM);
    CHECK(O->write(fs, h1, "hello ", 6) == 6 && O->write(fs, h2, "other", 5) == 5 && O->write(fs, h1, "world", 5) == 5);
    CHECK(O->stat(fs, "/w1", &st) == 0 && st.size == 11);
    O->close(fs, h1); O->close(fs, h2);
    CHECK(same(fs, "/w1", (const uint8_t*)"hello world", 11) && same(fs, "/w2", (const uint8_t*)"other", 5));
    CHECK(O->open(fs, "/w1", MCS_VFS_READ, &fh) == 0 && O->write(fs, fh, "x", 1) == MCS_VFS_EACCES);
    O->close(fs, fh);

    /* persistence across a remount (both paths) */
    CHECK(mcs_tinyfs_unmount(fs) == 0);
    CHECK(rig_mount(r, 0) == 0);
    CHECK(same(fs, "/a.txt", (const uint8_t*)"new", 3) && same(fs, "/w1", (const uint8_t*)"hello world", 11));
    CHECK(O->stat(fs, "/big.bin", &st) == MCS_VFS_ENOENT && O->stat(fs, "/gone", &st) == MCS_VFS_ENOENT);
    memset(&l, 0, sizeof l);
    CHECK(O->list(fs, "/", list_cb, &l) == 0 && l.n == 5);                      /* a.txt empty w1 w2 + nothing else */
    CHECK(r->sim.viol == 0);
    /* FORMAT wipes */
    CHECK(rig_mount(r, MCS_FLASHFS_FORMAT) == 0 && O->stat(fs, "/a.txt", &st) == MCS_VFS_ENOENT);
    CHECK(r->sim.viol == 0);
    sim_free(&r->sim); free(r);
}

/* ---------------- 2: ENOSPC and space reuse ---------------- */
static void test_full(uint32_t size, uint32_t erase, uint32_t ws) {
    rig_t* r = (rig_t*)calloc(1, sizeof *r);
    CHECK(rig_init(r, size, erase, ws) == 0 && rig_mount(r, MCS_FLASHFS_FORMAT_IF_NEEDED) == 0);
    mcs_tinyfs_t* fs = &r->fs;
    uint8_t blk[200], buf[256];
    int files = 0, e = 0;
    for (; files < MCS_TINYFS_MAX_FILES - 2 && !e; files++) {
        char p[16]; snprintf(p, sizeof p, "/f%d", files);
        fill(blk, sizeof blk, (unsigned)files);
        e = put(fs, p, blk, sizeof blk, MCS_VFS_WRITE, 64);
        if (!e) { mcs_vfs_statfs_t sf; O->statfs(fs, &sf); CHECK(sf.free <= sf.total); }
    }
    /* keep filling one file until the filesystem says no */
    void* fh;
    CHECK(O->open(fs, "/fill", MCS_VFS_WRITE, &fh) == 0 || files >= MCS_TINYFS_MAX_FILES - 2);
    long written = 0;
    int w = 0;
    for (int i = 0; i < 400 && w >= 0; i++) { w = O->write(fs, fh, blk, sizeof blk); if (w > 0) written += w; }
    int c = O->close(fs, fh);
    CHECK(w == MCS_VFS_ENOSPC || c == MCS_VFS_ENOSPC || files < MCS_TINYFS_MAX_FILES - 2);
    mcs_vfs_statfs_t sf;
    O->statfs(fs, &sf);
    CHECK(written > 0 || files >= MCS_TINYFS_MAX_FILES - 2);
    /* the earlier files are intact; deleting gives the space back */
    for (int i = 0; i < files && i < 3; i++) {
        char p[16]; snprintf(p, sizeof p, "/f%d", i);
        fill(blk, sizeof blk, (unsigned)i);
        if (i == 0 || i == 1) CHECK(same(fs, p, blk, sizeof blk));
    }
    O->remove(fs, "/fill");
    for (int i = 0; i < files; i++) { char p[16]; snprintf(p, sizeof p, "/f%d", i); CHECK(O->remove(fs, p) == 0); }
    O->statfs(fs, &sf);
    CHECK(sf.free == sf.total);
    CHECK(put(fs, "/after", buf, 100, MCS_VFS_WRITE, 100) == 0);
    /* a file that does not fit */
    uint8_t* huge = (uint8_t*)calloc(1, size);
    CHECK(put(fs, "/huge", huge, size, MCS_VFS_WRITE, 128) == MCS_VFS_ENOSPC);
    O->remove(fs, "/huge");
    free(huge);
    CHECK(put(fs, "/after2", buf, 100, MCS_VFS_WRITE, 100) == 0);
    CHECK(r->sim.viol == 0);
    sim_free(&r->sim); free(r);
}

/* ---------------- 3: random operations against a model ---------------- */
typedef struct { char path[24]; uint8_t* d; size_t n; int dir; } m_ent_t;
typedef struct { m_ent_t e[16]; int n; } model_t;
static m_ent_t* m_find(model_t* m, const char* p) { for (int i = 0; i < m->n; i++) if (!strcmp(m->e[i].path, p)) return &m->e[i]; return NULL; }
static void m_free(model_t* m) { for (int i = 0; i < m->n; i++) free(m->e[i].d); m->n = 0; }
static void m_copy(model_t* to, const model_t* from) {
    m_free(to);
    *to = *from;
    for (int i = 0; i < to->n; i++) { if (from->e[i].d) { to->e[i].d = (uint8_t*)malloc(from->e[i].n + 1); memcpy(to->e[i].d, from->e[i].d, from->e[i].n); } }
}
static void m_del(model_t* m, const char* p) {
    for (int i = 0; i < m->n; i++) if (!strcmp(m->e[i].path, p)) { free(m->e[i].d); m->e[i] = m->e[--m->n]; return; }
}
static m_ent_t* m_add(model_t* m, const char* p, int dir) {
    m_ent_t* e = m_find(m, p);
    if (e) { free(e->d); e->d = NULL; e->n = 0; e->dir = dir; return e; }
    e = &m->e[m->n++];
    memset(e, 0, sizeof *e); snprintf(e->path, sizeof e->path, "%s", p); e->dir = dir;
    return e;
}
static size_t m_bytes(model_t* m) { size_t t = 0; for (int i = 0; i < m->n; i++) t += m->e[i].n + 40; return t; }

/* does the filesystem hold exactly this model? (0 = yes) */
static int m_verify(mcs_tinyfs_t* fs, model_t* m, const char* what) {
    for (int i = 0; i < m->n; i++) {
        mcs_vfs_stat_t st;
        if (O->stat(fs, m->e[i].path, &st) || st.is_dir != (bool)m->e[i].dir) { printf("  %s: %s state\n", what, m->e[i].path); return 1; }
        if (!m->e[i].dir && (st.size != m->e[i].n || !same(fs, m->e[i].path, m->e[i].d, m->e[i].n))) { printf("  %s: %s content\n", what, m->e[i].path); return 1; }
    }
    listing_t l = { 0 };
    int root = 0, sub = 0;
    for (int i = 0; i < m->n; i++) { if (strchr(m->e[i].path + 1, '/')) sub++; else root++; }
    if (O->list(fs, "/", list_cb, &l) || l.n != root) { printf("  %s: root count %d vs %d\n", what, l.n, root); return 1; }
    memset(&l, 0, sizeof l);
    if (m_find(m, "/d") && (O->list(fs, "/d", list_cb, &l) || l.n != sub)) { printf("  %s: /d count\n", what); return 1; }
    return 0;
}

enum { OP_WRITE, OP_APPEND, OP_REMOVE, OP_RENAME, OP_MKDIR };
typedef struct { int kind; char p1[24], p2[24]; size_t len; unsigned seed; size_t piece; } op_t;
static const char* NAMES[] = { "/a", "/b", "/c.txt", "/d/e", "/d/f", "/g", "/h.bin", "/d/i" };

static void gen_op(op_t* o, unsigned* rs, model_t* m, size_t limit) {
    memset(o, 0, sizeof *o);
    unsigned k = rnd_next(rs) % 100;
    o->kind = k < 45 ? OP_WRITE : k < 62 ? OP_APPEND : k < 78 ? OP_REMOVE : k < 92 ? OP_RENAME : OP_MKDIR;
    snprintf(o->p1, sizeof o->p1, "%s", NAMES[rnd_next(rs) % 8]);
    snprintf(o->p2, sizeof o->p2, "%s", NAMES[rnd_next(rs) % 8]);
    o->len = rnd_next(rs) % 6 == 0 ? rnd_next(rs) % 900 : rnd_next(rs) % 200;
    o->seed = rnd_next(rs); o->piece = 1 + rnd_next(rs) % 150;
    if (o->kind == OP_MKDIR) snprintf(o->p1, sizeof o->p1, "/d");
    m_ent_t* e = m_find(m, o->p1);
    if ((o->kind == OP_WRITE || o->kind == OP_APPEND) && m_bytes(m) - (e ? e->n : 0) + o->len > limit) o->len = 0;
}
static int parent_ok(model_t* m, const char* p) {
    if (!strncmp(p, "/d/", 3)) { m_ent_t* d = m_find(m, "/d"); return d && d->dir; }
    return 1;
}
/* apply to the model; returns whether the operation is legal (it is then run on the fs too) */
static int op_model(op_t* o, model_t* m) {
    m_ent_t* e = m_find(m, o->p1);
    switch (o->kind) {
    case OP_WRITE: case OP_APPEND:
        if (!parent_ok(m, o->p1) || (e && e->dir) || (!e && m->n >= 14)) return 0;
        if (!e) { e = m_add(m, o->p1, 0); }
        if (o->kind == OP_WRITE) { free(e->d); e->d = NULL; e->n = 0; }
        e->d = (uint8_t*)realloc(e->d, e->n + o->len + 1);
        fill(e->d + e->n, o->len, o->seed); e->n += o->len;
        return 1;
    case OP_REMOVE:
        if (!e) return 0;
        if (e->dir) { for (int i = 0; i < m->n; i++) if (!strncmp(m->e[i].path, "/d/", 3)) return 0; }
        m_del(m, o->p1);
        return 1;
    case OP_RENAME:
        if (!e || m_find(m, o->p2) || !parent_ok(m, o->p2) || e->dir || !strcmp(o->p1, o->p2)) return 0;
        snprintf(e->path, sizeof e->path, "%s", o->p2);
        return 1;
    default:
        if (e) return 0;
        m_add(m, "/d", 1);
        return 1;
    }
}
static int op_fs(mcs_tinyfs_t* fs, op_t* o) {
    uint8_t* d = (uint8_t*)malloc(o->len + 1);
    fill(d, o->len, o->seed);
    int r = 0;
    switch (o->kind) {
    case OP_WRITE: r = put(fs, o->p1, d, o->len, MCS_VFS_WRITE, o->piece); break;
    case OP_APPEND: r = put(fs, o->p1, d, o->len, MCS_VFS_APPEND, o->piece); break;
    case OP_REMOVE: r = O->remove(fs, o->p1); break;
    case OP_RENAME: r = O->rename(fs, o->p1, o->p2); break;
    default: r = O->mkdir(fs, o->p1); break;
    }
    free(d);
    return r;
}

static void test_random(uint32_t size, uint32_t erase, uint32_t ws, unsigned seed, int nops) {
    rig_t* r = (rig_t*)calloc(1, sizeof *r);
    CHECK(rig_init(r, size, erase, ws) == 0 && rig_mount(r, MCS_FLASHFS_FORMAT_IF_NEEDED) == 0);
    model_t m = { 0 };
    unsigned rs = seed;
    mcs_vfs_statfs_t sf;
    O->statfs(&r->fs, &sf);
    size_t limit = (size_t)(sf.total / 2);
    int bad = 0, done = 0;
    for (int i = 0; i < nops && !bad; i++) {
        op_t o;
        gen_op(&o, &rs, &m, limit);
        if (!op_model(&o, &m)) continue;
        int e = op_fs(&r->fs, &o);
        if (e) { printf("  op %d kind %d failed: %d\n", i, o.kind, e); bad = 1; break; }
        done++;
        if (m_verify(&r->fs, &m, "after op")) bad = 1;
        if (!bad && rnd_next(&rs) % 9 == 0) {                 /* clean remount */
            mcs_tinyfs_unmount(&r->fs);
            if (rig_mount(r, 0) || m_verify(&r->fs, &m, "after remount")) bad = 1;
        }
    }
    CHECK(!bad);
    CHECK(r->sim.viol == 0);
    long mn = 1 << 30, mx = 0;
    for (uint32_t b = 0; b < size / erase; b++) { if (r->sim.block_erases[b] < mn) mn = r->sim.block_erases[b]; if (r->sim.block_erases[b] > mx) mx = r->sim.block_erases[b]; }
    CHECK(done > nops / 4);
    if (r->sim.erases > 4 * (long)(size / erase)) CHECK(mx - mn <= 2 + mx / 4);          /* wear is spread */
    if (seed == 1 && ws == 8) printf("  random %u B/%u B ws %u: %d ops, %ld erases (min %ld max %ld per block), %ld programs\n",
                                     size, erase, ws, done, r->sim.erases, mn, mx, r->sim.progs);
    m_free(&m);
    sim_free(&r->sim); free(r);
}

/* ---------------- 4: power cut at every flash step ---------------- */
#define SCEN 150
static int scen_cut(uint32_t size, uint32_t erase, uint32_t ws, int cut_op, long cut_at, long* nops_out, long* erases_out, long* opcount) {
    rig_t* r = (rig_t*)calloc(1, sizeof *r);
    int bad = 0;
    rig_init(r, size, erase, ws);
    rig_mount(r, MCS_FLASHFS_FORMAT_IF_NEEDED);
    model_t m = { 0 }, m1 = { 0 };
    unsigned rs = 99;
    mcs_vfs_statfs_t sf;
    O->statfs(&r->fs, &sf);
    size_t limit = (size_t)(sf.total / 2);
    for (int i = 0; i < SCEN && !bad; i++) {
        op_t o;
        gen_op(&o, &rs, &m, limit);
        m_copy(&m1, &m);
        if (!op_model(&o, &m1)) continue;
        if (i == cut_op && cut_at > 0) r->sim.limit = r->sim.ops + cut_at - 1;
        long before = r->sim.ops;
        int e = op_fs(&r->fs, &o);
        if (opcount) opcount[i] = r->sim.ops - before;
        if (i == cut_op && cut_at > 0) {
            /* power returns: remount and compare with the states the interrupted operation may leave */
            sim_reboot(&r->sim);
            int me = rig_mount(r, 0);
            if (me) { printf("  mount after cut: %d (op %d, step %ld)\n", me, i, cut_at); bad = 1; break; }
            for (int k = 0; k < m.n && !bad; k++) {                         /* every path of the old state */
                m_ent_t* a = &m.e[k];
                m_ent_t* b = m_find(&m1, a->path);
                mcs_vfs_stat_t st;
                int present = O->stat(&r->fs, a->path, &st) == 0;
                if (!present) { if (b) { if (!(o.kind == OP_WRITE || o.kind == OP_APPEND || o.kind == OP_RENAME)) bad = 1; } continue; }
                if (st.is_dir != (bool)a->dir) { bad = 1; continue; }
                if (a->dir) continue;
                uint8_t* got = (uint8_t*)malloc(st.size + 8);
                long n = slurp(&r->fs, a->path, got, st.size + 8);
                int ok = n == (long)st.size;
                if (ok && b && o.kind == OP_APPEND && !strcmp(o.p1, a->path)) ok = n >= (long)a->n && (size_t)n <= b->n && !memcmp(got, b->d, (size_t)n);
                else if (ok && b && o.kind == OP_WRITE && !strcmp(o.p1, a->path)) ok = ((size_t)n <= b->n && !memcmp(got, b->d, (size_t)n)) || (n == (long)a->n && !memcmp(got, a->d, a->n));
                else if (ok) ok = (n == (long)a->n && !memcmp(got, a->d, a->n)) || (b && n == (long)b->n && !memcmp(got, b->d, b->n));
                free(got);
                if (!ok) { printf("  %s differs after cut (op %d kind %d step %ld)\n", a->path, i, o.kind, cut_at); bad = 1; }
            }
            for (int k = 0; k < m1.n && !bad; k++) {                        /* paths the operation created */
                m_ent_t* b = &m1.e[k];
                if (m_find(&m, b->path)) continue;
                mcs_vfs_stat_t st;
                if (O->stat(&r->fs, b->path, &st) == 0 && !b->dir) {
                    uint8_t* got = (uint8_t*)malloc(st.size + 8);
                    long n = slurp(&r->fs, b->path, got, st.size + 8);
                    if (n != (long)st.size || (size_t)n > b->n || memcmp(got, b->d, (size_t)n)) { printf("  new file %s torn (op %d step %ld)\n", b->path, i, cut_at); bad = 1; }
                    free(got);
                }
            }
            if (o.kind == OP_RENAME) {
                mcs_vfs_stat_t st;
                if (O->stat(&r->fs, o.p1, &st) == 0 && O->stat(&r->fs, o.p2, &st) == 0) { printf("  rename left both names (step %ld)\n", cut_at); bad = 1; }
                if (O->stat(&r->fs, o.p1, &st) && O->stat(&r->fs, o.p2, &st)) { printf("  rename lost the file (step %ld)\n", cut_at); bad = 1; }
            }
            if (!bad) {
                /* adopt what survived as the new model, then keep going: it must be fully usable */
                listing_t l = { 0 };
                O->list(&r->fs, "/", list_cb, &l);
                uint8_t blk[64];
                fill(blk, sizeof blk, 5);
                if (put(&r->fs, "/after_cut", blk, sizeof blk, MCS_VFS_WRITE, 64) || !same(&r->fs, "/after_cut", blk, sizeof blk)) { printf("  unusable after cut (op %d step %ld)\n", i, cut_at); bad = 1; }
                if (!bad) {
                    mcs_tinyfs_unmount(&r->fs);
                    if (rig_mount(r, 0) || !same(&r->fs, "/after_cut", blk, sizeof blk)) { printf("  second mount differs (op %d step %ld)\n", i, cut_at); bad = 1; }
                }
                if (!bad && r->sim.viol) { printf("  flash rule violated (op %d step %ld)\n", i, cut_at); bad = 1; }
            }
            break;
        }
        if (e) { printf("  scenario op %d kind %d failed: %d\n", i, o.kind, e); bad = 1; break; }
        m_copy(&m, &m1);
    }
    if (nops_out) *nops_out = r->sim.ops;
    if (erases_out) *erases_out = r->sim.erases;
    if (!bad && cut_at == 0 && m_verify(&r->fs, &m, "scenario")) bad = 1;
    if (cut_at == 0 && r->sim.viol) { printf("  rule violations: %ld\n", r->sim.viol); bad = 1; }
    m_free(&m); m_free(&m1);
    sim_free(&r->sim); free(r);
    return bad;
}

static void test_powercut(uint32_t size, uint32_t erase, uint32_t ws) {
    long opcount[SCEN] = { 0 }, total = 0, erases = 0;
    CHECK(scen_cut(size, erase, ws, -1, 0, &total, &erases, opcount) == 0);
    CHECK(erases >= 2 * (long)(size / erase));                          /* the scenario wraps the log: GC and block rotation run */
    long cuts = 0;
    int bad = 0;
    for (int op = 0; op < SCEN && !bad; op++) {
        for (long at = 1; at <= opcount[op] && !bad; at++) {
            if (scen_cut(size, erase, ws, op, at, NULL, NULL, NULL)) { bad = 1; printf("  cut at op %d, flash step %ld/%ld\n", op, at, opcount[op]); }
            cuts++;
        }
    }
    CHECK(!bad);
    printf("  power cut: %ld cut points over %ld flash operations (%ld erases), %u B region, %u B blocks, unit %u\n",
           cuts, total, erases, size, erase, ws);
}

/* ---------------- 5: through mcs_flashfs_mount + the vfs + C#-facing helpers ---------------- */
static void test_flashfs(void) {
    rig_t* r = (rig_t*)calloc(1, sizeof *r);
    CHECK(rig_init(r, 8192, 1024, 4) == 0);
    mcs_flashfs_t ffs;
    CHECK(mcs_flashfs_mount(&ffs, &r->dev.flash, 0, 0, MCS_FLASHFS_TINYFS, 0) == MCS_VFS_EIO);
    CHECK(mcs_flashfs_mount(&ffs, &r->dev.flash, 0, 0, MCS_FLASHFS_TINYFS, MCS_FLASHFS_FORMAT_IF_NEEDED) == 0);
    CHECK(ffs.kind == MCS_FLASHFS_TINYFS && !strcmp(mcs_flashfs_kind_name(ffs.kind), "tinyfs"));
    mcs_vfs_t vfs;
    mcs_vfs_init(&vfs);
    CHECK(mcs_vfs_mount(&vfs, "/flash", ffs.ops, ffs.ctx, 0) == 0);
    CHECK(mcs_vfs_write_file(&vfs, "/flash/hello.cs", "Console.WriteLine(1);", 21, false) == 0);
    CHECK(mcs_vfs_write_file(&vfs, "/flash/hello.cs", "// more\n", 8, true) == 0);
    char* data; size_t n;
    CHECK(mcs_vfs_read_file(&vfs, "/flash/hello.cs", &data, &n) == 0 && n == 29 && !strncmp(data, "Console.WriteLine(1);// more\n", 29));
    mcs_vfs_free(&vfs, data, n);
    mcs_vfs_statfs_t sf;
    CHECK(mcs_vfs_statfs(&vfs, "/flash/hello.cs", &sf) == 0 && !strcmp(sf.format, "tinyfs") && !strcmp(sf.mount, "/flash"));
    CHECK(mcs_vfs_check_space(&vfs, "/flash/x", 100000, false) == MCS_VFS_ENOSPC && mcs_vfs_check_space(&vfs, "/flash/x", 100, false) == 0);
    mcs_vfs_umount(&vfs, "/flash");
    CHECK(mcs_flashfs_unmount(&ffs) == 0);
    CHECK(mcs_flashfs_mount(&ffs, &r->dev.flash, 0, 0, MCS_FLASHFS_TINYFS, 0) == 0);      /* mounts without a format flag now */
    /* a partition of an existing chip: blocks 2..5 (4 blocks) */
    mcs_flashfs_t f2;
    CHECK(mcs_flashfs_mount(&f2, &r->dev.flash, 2, 4, MCS_FLASHFS_TINYFS, MCS_FLASHFS_FORMAT) == 0);
    mcs_flashfs_unmount(&f2);
    /* geometry that cannot work */
    CHECK(mcs_flashfs_mount(&f2, &r->dev.flash, 0, 1, MCS_FLASHFS_TINYFS, MCS_FLASHFS_FORMAT) == MCS_VFS_EINVAL);   /* one block */
    mcs_flashfs_unmount(&ffs);
    sim_free(&r->sim);
    /* port validation */
    mcs_intflash_t d;
    mcs_flash_port_t bad = { 8192, 1000, 4, sim_read, sim_write, sim_erase, NULL };
    CHECK(mcs_intflash_init(&d, &bad) != 0);                          /* size not a multiple of the erase unit */
    bad = (mcs_flash_port_t){ 8192, 1024, 3, sim_read, sim_write, sim_erase, NULL };
    CHECK(mcs_intflash_init(&d, &bad) != 0);                          /* write unit not a power of two */
    bad = (mcs_flash_port_t){ 8192, 1024, 4, NULL, sim_write, sim_erase, NULL };
    CHECK(mcs_intflash_init(&d, &bad) != 0);
    free(r);
}

int main(void) {
    static const struct { uint32_t size, erase, ws; } geo[] = {
        { 8192, 1024, 2 }, { 8192, 1024, 4 }, { 8192, 2048, 8 }, { 8192, 4096, 1 }, { 16384, 2048, 16 },
        { 32768, 4096, 32 }, { 65536, 16384, 64 }, { 8192, 1024, 0 }
    };
    for (unsigned i = 0; i < sizeof geo / sizeof geo[0]; i++) {
        printf("geometry %u B / %u B blocks / unit %u\n", geo[i].size, geo[i].erase, geo[i].ws);
        test_basics(geo[i].size, geo[i].erase, geo[i].ws);
        test_full(geo[i].size, geo[i].erase, geo[i].ws);
        for (unsigned s = 1; s <= 3; s++) test_random(geo[i].size, geo[i].erase, geo[i].ws, s, 400);
    }
    test_flashfs();
    printf("power-cut sweeps\n");
    test_powercut(8192, 1024, 4);
    test_powercut(8192, 2048, 8);
    test_powercut(4096 * 2, 4096, 1);
    test_powercut(8192, 1024, 32);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
