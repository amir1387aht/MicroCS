/*
 * MicroCS - TinyFS: a small power-fail-safe filesystem for NOR-type flash.
 * No third-party sources (MIT, like the rest of MicroCS); meant for the MCU's
 * own flash (a few KB of a 128 KB part) but works on any mcs_flash_t.
 *
 * Layout - a log, nothing is modified in place:
 *   erase block = block header (magic "TFS1", seq, gc_from, crc) + records.
 *   record      = 16-byte header {type, flags, plen, id, off, crc32} + payload,
 *                 padded with 0xFF to the flash program unit, so every flash
 *                 write is unit-aligned and no unit is programmed twice.
 *   N NAME  path of file/dir `id` (flags bit0 = directory); a newer NAME for the
 *           same id renames it, a NAME for an existing path replaces that entry
 *           (this is how open-for-write truncates)
 *   D DATA  `plen` bytes of file `id` at offset `off`
 *   X DEL   file/dir `id` is gone
 *   C COMMIT ends a garbage-collection copy
 * ids are never reused. The RAM index (id, name address, size) is rebuilt at
 * mount by replaying the valid blocks in sequence order; names stay in flash.
 *
 * Blocks are used round-robin (wear levelling). When only one erased block is
 * left, the oldest block's live records are copied into it (header gc_from =
 * victim seq), a COMMIT record seals the copy and the victim is erased. A copy
 * without COMMIT is ignored at mount; a victim whose copy has a COMMIT is erased
 * at mount - so a power cut at any point loses at most the record being written.
 * Space: live record bytes are kept <= (blocks - 1) * (usable - one record),
 * which guarantees that collecting always makes room.
 */
#include "mcs_vfs.h"
#if MCS_ENABLE_FS && MCS_ENABLE_FLASH && MCS_ENABLE_TINYFS
#include <string.h>

typedef mcs_tinyfs_t tfs_t;
typedef mcs_tinyfs_entry_t ent_t;
typedef mcs_tinyfs_handle_t hnd_t;

#define REC_HDR 16u
#define T_NAME 'N'
#define T_DATA 'D'
#define T_DEL 'X'
#define T_COMMIT 'C'
#define MAXPATH (MCS_VFS_PATH_MAX - 1)
#define MAXPAY (MCS_TINYFS_CHUNK > MAXPATH ? MCS_TINYFS_CHUNK : MAXPATH)
#define PIECE 64u

static uint32_t rup(uint32_t v, uint32_t u) { return (v + u - 1) / u * u; }
static uint32_t rd32(const uint8_t* p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static uint32_t crc_upd(uint32_t c, const uint8_t* p, size_t n) {
    static const uint32_t t[16] = { 0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                    0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                    0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C };
    while (n--) {
        c ^= *p++;
        c = (c >> 4) ^ t[c & 15];
        c = (c >> 4) ^ t[c & 15];
    }
    return c;
}

/* ---- raw flash access (offsets are relative to the start of the region) ---- */
static int fl_read(tfs_t* fs, uint32_t a, void* b, uint32_t n) {
    return fs->f->read(fs->f, fs->base + a, b, n) < 0 ? MCS_VFS_EIO : 0;
}
static int fl_prog(tfs_t* fs, uint32_t a, const void* b, uint32_t n) {
    return fs->f->prog(fs->f, fs->base + a, b, n) < 0 ? MCS_VFS_EIO : 0;
}
static int fl_erase(tfs_t* fs, uint32_t blk) {
    return fs->f->erase(fs->f, fs->part.first_block + blk) < 0 ? MCS_VFS_EIO : 0;
}
static int is_blank(const uint8_t* p, uint32_t n) {
    while (n--) if (*p++ != 0xFF) return 0;
    return 1;
}
/* every byte of [a, a+n) erased? */
static int blank_range(tfs_t* fs, uint32_t a, uint32_t n, int* blank) {
    uint8_t b[PIECE];
    *blank = 1;
    while (n) {
        uint32_t k = n < PIECE ? n : PIECE;
        if (fl_read(fs, a, b, k)) return MCS_VFS_EIO;
        if (!is_blank(b, k)) { *blank = 0; return 0; }
        a += k; n -= k;
    }
    return 0;
}

/* ---- records ---- */
typedef struct { uint8_t type, flags; uint16_t plen; uint32_t id, off, crc; } rh_t;

static void rh_unpack(const uint8_t* h, rh_t* r) {
    r->type = h[0]; r->flags = h[1]; r->plen = (uint16_t)(h[2] | h[3] << 8);
    r->id = rd32(h + 4); r->off = rd32(h + 8); r->crc = rd32(h + 12);
}
static int rh_sane(tfs_t* fs, const rh_t* r, uint32_t pos) {
    if (r->type != T_NAME && r->type != T_DATA && r->type != T_DEL && r->type != T_COMMIT) return 0;
    if (r->plen > MAXPAY) return 0;
    if (r->type == T_NAME && (r->plen == 0 || r->plen > MAXPATH)) return 0;
    return pos + rup(REC_HDR + r->plen, fs->unit) <= fs->bsize;
}
static uint32_t rsize(tfs_t* fs, uint32_t plen) { return rup(REC_HDR + plen, fs->unit); }

/* header + payload CRC check of the record at region offset a */
static int rec_crc_ok(tfs_t* fs, uint32_t a, const uint8_t* hdr, const rh_t* r) {
    uint32_t c = crc_upd(~0u, hdr, 12);
    uint8_t b[32];
    for (uint32_t o = 0; o < r->plen;) {
        uint32_t k = r->plen - o < sizeof b ? r->plen - o : (uint32_t)sizeof b;
        if (fl_read(fs, a + REC_HDR + o, b, k)) return 0;
        c = crc_upd(c, b, k);
        o += k;
    }
    return ~c == r->crc;
}

/* append one record at the head block (no room check) */
static int put_rec(tfs_t* fs, uint8_t type, uint8_t flags, uint32_t id, uint32_t off, const void* payload,
                   uint32_t plen, uint32_t* addr_out) {
    uint32_t u = fs->unit, a = (uint32_t)fs->head * fs->bsize + fs->off, total = rsize(fs, plen);
    uint8_t hdr[REC_HDR], buf[MCS_TINYFS_MAX_UNIT > REC_HDR ? MCS_TINYFS_MAX_UNIT : REC_HDR];
    const uint8_t* p = (const uint8_t*)payload;
    hdr[0] = type; hdr[1] = flags; hdr[2] = (uint8_t)plen; hdr[3] = (uint8_t)(plen >> 8);
    wr32(hdr + 4, id); wr32(hdr + 8, off);
    uint32_t c = crc_upd(~0u, hdr, 12);
    if (plen) c = crc_upd(c, p, plen);
    wr32(hdr + 12, ~c);
    uint32_t lead = rup(REC_HDR, u), n1 = plen < lead - REC_HDR ? plen : lead - REC_HDR;
    memset(buf, 0xFF, lead);
    memcpy(buf, hdr, REC_HDR);
    if (n1) memcpy(buf + REC_HDR, p, n1);
    int e = fl_prog(fs, a, buf, lead);
    uint32_t rem = plen - n1, main_n = rem - rem % u;
    p += n1;
    if (!e && main_n) e = fl_prog(fs, a + lead, p, main_n);
    if (!e && rem % u) {
        memset(buf, 0xFF, u);
        memcpy(buf, p + main_n, rem % u);
        e = fl_prog(fs, a + lead + main_n, buf, u);
    }
    if (e) { fs->sealed[fs->head] = 1; return e; }   /* torn record stays behind: stop using this block */
    if (addr_out) *addr_out = a;
    fs->off += total;
    fs->end[fs->head] = fs->off;
    return 0;
}

/* ---- block management ---- */
static int open_block(tfs_t* fs, int b, uint32_t gc_from) {
    int blank, e = blank_range(fs, (uint32_t)b * fs->bsize, fs->bsize, &blank);
    if (e) return e;
    if (!blank && (e = fl_erase(fs, (uint32_t)b))) return e;
    uint8_t buf[MCS_TINYFS_MAX_UNIT > REC_HDR ? MCS_TINYFS_MAX_UNIT : REC_HDR];
    memset(buf, 0xFF, fs->hs);
    memcpy(buf, "TFS1", 4);
    wr32(buf + 4, fs->max_seq + 1);
    wr32(buf + 8, gc_from);
    wr32(buf + 12, ~crc_upd(~0u, buf, 12));
    fs->valid[b] = 0;
    if ((e = fl_prog(fs, (uint32_t)b * fs->bsize, buf, fs->hs))) return e;
    fs->max_seq++;
    fs->seq[b] = fs->max_seq;
    fs->valid[b] = 1; fs->sealed[b] = 0;
    fs->end[b] = fs->hs;
    fs->head = b; fs->off = fs->hs;
    return 0;
}

static ent_t* by_id(tfs_t* fs, uint32_t id) {
    if (!id) return NULL;
    for (int i = 0; i < MCS_TINYFS_MAX_FILES; i++) if (fs->ent[i].id == id) return &fs->ent[i];
    return NULL;
}

/* copy the live records of the oldest block into a fresh one */
static int collect(tfs_t* fs) {
    int v = -1, t = -1;
    for (uint32_t i = 0; i < fs->nblk; i++) {
        if (fs->valid[i]) { if (v < 0 || fs->seq[i] < fs->seq[v]) v = (int)i; }
        else if (t < 0) t = (int)i;
    }
    if (v < 0 || t < 0) return MCS_VFS_EIO;
    uint32_t vseq = fs->seq[v], vend = fs->end[v];
    int e = open_block(fs, t, vseq);
    if (e) return e;
    for (uint32_t pos = fs->hs; pos < vend;) {
        uint8_t hb[REC_HDR];
        rh_t r;
        if (fl_read(fs, (uint32_t)v * fs->bsize + pos, hb, REC_HDR)) return MCS_VFS_EIO;
        rh_unpack(hb, &r);
        uint32_t n = rsize(fs, r.plen), src = (uint32_t)v * fs->bsize + pos;
        ent_t* en = r.type == T_NAME || r.type == T_DATA ? by_id(fs, r.id) : NULL;
        int live = en && (r.type == T_DATA || en->name_addr == src);
        pos += n;
        if (!live) continue;
        if (fs->off + n > fs->bsize - fs->cs) return MCS_VFS_EIO;   /* cannot happen: see header comment */
        uint32_t dst = (uint32_t)t * fs->bsize + fs->off;
        uint8_t pc[PIECE];
        for (uint32_t o = 0; o < n; o += PIECE) {
            uint32_t k = n - o < PIECE ? n - o : PIECE;
            if (fl_read(fs, src + o, pc, k) || fl_prog(fs, dst + o, pc, k)) { fs->sealed[t] = 1; return MCS_VFS_EIO; }
        }
        if (r.type == T_NAME) en->name_addr = dst;
        fs->off += n;
        fs->end[t] = fs->off;
    }
    if ((e = put_rec(fs, T_COMMIT, 0, vseq, 0, NULL, 0, NULL))) return e;
    fs->valid[v] = 0;
    return fl_erase(fs, (uint32_t)v);
}

/* make sure the head block has room for `need` more bytes */
static int ensure_room(tfs_t* fs, uint32_t need) {
    for (uint32_t guard = 0; guard < 3 * fs->nblk + 3; guard++) {
        if (fs->head >= 0 && !fs->sealed[fs->head] && fs->off + need <= fs->bsize - fs->cs) return 0;
        uint32_t nv = 0;
        int pick = -1;
        uint32_t start = fs->head < 0 ? fs->nblk - 1 : (uint32_t)fs->head;
        for (uint32_t k = 1; k <= fs->nblk; k++) {            /* next erased block after the head, round robin */
            uint32_t b = (start + k) % fs->nblk;
            if (!fs->valid[b]) { nv++; if (pick < 0) pick = (int)b; }
        }
        int e = nv >= 2 ? open_block(fs, pick, 0) : collect(fs);
        if (e) return e;
    }
    return MCS_VFS_ENOSPC;
}

static int emit(tfs_t* fs, uint8_t type, uint8_t flags, uint32_t id, uint32_t off, const void* payload,
                uint32_t plen, uint32_t* addr_out) {
    int e = ensure_room(fs, rsize(fs, plen));
    return e ? e : put_rec(fs, type, flags, id, off, payload, plen, addr_out);
}

/* ---- names ---- */
static uint32_t hash_path(const char* s, size_t n) {
    uint32_t h = 2166136261u;
    while (n--) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}
static int name_is(tfs_t* fs, const ent_t* e, const char* path, size_t n) {
    if (e->nlen != n) return 0;
    char b[32];
    for (uint32_t o = 0; o < n;) {
        uint32_t k = n - o < sizeof b ? (uint32_t)(n - o) : (uint32_t)sizeof b;
        if (fl_read(fs, e->name_addr + REC_HDR + o, b, k) || memcmp(b, path + o, k)) return 0;
        o += k;
    }
    return 1;
}
static int read_name(tfs_t* fs, const ent_t* e, char* out) {
    if (fl_read(fs, e->name_addr + REC_HDR, out, e->nlen)) return MCS_VFS_EIO;
    out[e->nlen] = 0;
    return 0;
}
static ent_t* find_path(tfs_t* fs, const char* path) {
    size_t n = strlen(path);
    uint32_t h = hash_path(path, n);
    for (int i = 0; i < MCS_TINYFS_MAX_FILES; i++) {
        ent_t* e = &fs->ent[i];
        if (e->id && e->hash == h && name_is(fs, e, path, n)) return e;
    }
    return NULL;
}
static ent_t* free_slot(tfs_t* fs) {
    for (int i = 0; i < MCS_TINYFS_MAX_FILES; i++) if (!fs->ent[i].id) return &fs->ent[i];
    return NULL;
}
static void drop_entry(tfs_t* fs, ent_t* e) {
    for (int i = 0; i < MCS_TINYFS_HANDLES; i++)
        if (fs->h[i].used && fs->h[i].id == e->id) { fs->h[i].id = 0; fs->h[i].len = 0; }
    fs->live -= e->rec;
    memset(e, 0, sizeof *e);
}

/* parent of `path` must be the root or an existing directory */
static int check_parent(tfs_t* fs, const char* path) {
    const char* slash = strrchr(path, '/');
    if (!slash || slash == path) return MCS_VFS_OK;
    char parent[MCS_VFS_PATH_MAX];
    size_t n = (size_t)(slash - path);
    memcpy(parent, path, n); parent[n] = 0;
    ent_t* p = find_path(fs, parent);
    if (!p) return MCS_VFS_ENOENT;
    return p->dir ? MCS_VFS_OK : MCS_VFS_ENOTDIR;
}
static int has_children(tfs_t* fs, const char* path) {
    size_t n = strlen(path);
    char nm[MCS_VFS_PATH_MAX];
    for (int i = 0; i < MCS_TINYFS_MAX_FILES; i++) {
        ent_t* e = &fs->ent[i];
        if (!e->id || e->nlen <= n || read_name(fs, e, nm)) continue;
        if (!strncmp(nm, path, n) && nm[n] == '/') return 1;
    }
    return 0;
}

/* ---- writing ---- */
static int flush_handle(tfs_t* fs, hnd_t* h) {
    if (!h->used || !h->len) return h->err;
    if (h->err) { h->len = 0; return h->err; }
    ent_t* e = by_id(fs, h->id);
    uint32_t n = rsize(fs, h->len);
    int r = 0;
    if (!e) r = MCS_VFS_EIO;
    else if (fs->live + n > fs->allowed) r = MCS_VFS_ENOSPC;
    else if (!(r = emit(fs, T_DATA, 0, e->id, e->size, h->buf, h->len, NULL))) {
        e->size += h->len; e->rec += n; fs->live += n;
    }
    h->len = 0;
    if (r) h->err = (int16_t)r;
    return r;
}
static int flush_all(tfs_t* fs) {
    int r = 0;
    for (int i = 0; i < MCS_TINYFS_HANDLES; i++) {
        int e = flush_handle(fs, &fs->h[i]);
        if (e && !r) r = e;
    }
    return r;
}

/* write a NAME record for `id` and give back its address */
static int put_name(tfs_t* fs, uint32_t id, bool dir, const char* path, size_t n, uint32_t* addr) {
    return emit(fs, T_NAME, dir ? 1 : 0, id, 0, path, (uint32_t)n, addr);
}
static int check_len(const char* path, size_t* n) {
    *n = strlen(path);
    return *n > MAXPATH ? MCS_VFS_ENAMETOOLONG : MCS_VFS_OK;
}

/* new entry for `path` (replaces an existing entry of that path) */
static int create_entry(tfs_t* fs, const char* path, bool dir, ent_t** out) {
    size_t n;
    int r = check_len(path, &n);
    if (r) return r;
    if ((r = check_parent(fs, path))) return r;
    ent_t* old = find_path(fs, path);
    ent_t* slot = old ? old : free_slot(fs);
    if (!slot) return MCS_VFS_ENOSPC;
    uint32_t need = rsize(fs, (uint32_t)n);
    if (fs->live - (old ? old->rec : 0) + need > fs->allowed) return MCS_VFS_ENOSPC;
    uint32_t id = fs->next_id, addr;
    if ((r = put_name(fs, id, dir, path, n, &addr))) return r;
    fs->next_id++;
    if (old) drop_entry(fs, old);
    memset(slot, 0, sizeof *slot);
    slot->id = id; slot->name_addr = addr; slot->rec = need; slot->hash = hash_path(path, n);
    slot->nlen = (uint8_t)n; slot->dir = dir;
    fs->live += need;
    *out = slot;
    return 0;
}

/* ---- mount ---- */
static int apply_name(tfs_t* fs, const rh_t* r, uint32_t addr) {
    char path[MCS_VFS_PATH_MAX];
    if (fl_read(fs, addr + REC_HDR, path, r->plen)) return MCS_VFS_EIO;
    path[r->plen] = 0;
    uint32_t h = hash_path(path, r->plen);
    ent_t* e = by_id(fs, r->id);
    for (int i = 0; i < MCS_TINYFS_MAX_FILES; i++) {          /* a newer name for the same path replaces that entry */
        ent_t* o = &fs->ent[i];
        if (o->id && o != e && o->hash == h && name_is(fs, o, path, r->plen)) memset(o, 0, sizeof *o);
    }
    if (!e && !(e = free_slot(fs))) return MCS_VFS_ENOMEM;
    if (!e->id) memset(e, 0, sizeof *e);
    e->id = r->id; e->name_addr = addr; e->hash = h; e->nlen = (uint8_t)r->plen; e->dir = r->flags & 1;
    e->rec = rsize(fs, r->plen);
    if (r->id >= fs->next_id) fs->next_id = r->id + 1;
    return 0;
}

static int replay_block(tfs_t* fs, uint32_t b, int pass) {
    for (uint32_t pos = fs->hs; pos < fs->end[b];) {
        uint8_t hb[REC_HDR];
        rh_t r;
        uint32_t a = b * fs->bsize + pos;
        if (fl_read(fs, a, hb, REC_HDR)) return MCS_VFS_EIO;
        rh_unpack(hb, &r);
        uint32_t n = rsize(fs, r.plen);
        ent_t* e;
        if (r.id >= fs->next_id && r.type != T_COMMIT) fs->next_id = r.id + 1;
        if (pass == 1 && r.type == T_NAME) { int x = apply_name(fs, &r, a); if (x) return x; }
        else if (pass == 1 && r.type == T_DEL) { if ((e = by_id(fs, r.id))) memset(e, 0, sizeof *e); }
        else if (pass == 2 && r.type == T_DATA && (e = by_id(fs, r.id))) {
            if (r.off + r.plen > e->size) e->size = r.off + r.plen;
            e->rec += n;
        }
        pos += n;
    }
    return 0;
}

static int format_all(tfs_t* fs) {
    for (uint32_t b = 0; b < fs->nblk; b++) if (fl_erase(fs, b)) return MCS_VFS_EIO;
    return 0;
}

int mcs_tinyfs_mount(mcs_tinyfs_t* fs, const mcs_flash_part_t* part, int flags) {
    if (!fs || !part || !part->flash) return MCS_VFS_EINVAL;
    mcs_flash_t* f = part->flash;
    if (!f->read || !f->prog || !f->erase || f->type != MCS_FLASH_NOR || !f->block_size) return MCS_VFS_EINVAL;
    if (part->first_block >= f->block_count) return MCS_VFS_EINVAL;
    uint32_t nblk = part->block_count ? part->block_count : f->block_count - part->first_block;
    if (part->first_block + nblk > f->block_count) return MCS_VFS_EINVAL;
    if (nblk > MCS_TINYFS_MAX_BLOCKS) nblk = MCS_TINYFS_MAX_BLOCKS;
    uint32_t u = f->write_size ? f->write_size : 1;
    if (u > MCS_TINYFS_MAX_UNIT || (u & (u - 1)) || f->block_size % u) return MCS_VFS_EINVAL;
    memset(fs, 0, sizeof *fs);
    fs->part = *part; fs->part.block_count = nblk;
    fs->f = f; fs->bsize = f->block_size; fs->nblk = nblk; fs->unit = u;
    fs->base = part->first_block * f->block_size;
    fs->hs = rup(REC_HDR, u); fs->cs = rsize(fs, 0);
    fs->maxrec = rsize(fs, MAXPAY);
    fs->head = -1;
    uint32_t usable = fs->bsize > fs->hs + fs->cs ? fs->bsize - fs->hs - fs->cs : 0;
    if (nblk < 2 || usable < 4 * fs->maxrec) return MCS_VFS_EINVAL;
    fs->allowed = (nblk - 1) * (usable - fs->maxrec);
    fs->next_id = 1;
    if (flags & MCS_FLASHFS_FORMAT) { int e = format_all(fs); if (e) return e; }

    /* A: classify blocks by header, CRC-scan the records of the valid ones */
    uint8_t has_commit[MCS_TINYFS_MAX_BLOCKS] = { 0 };
    uint32_t gc_from[MCS_TINYFS_MAX_BLOCKS] = { 0 };
    uint32_t nvalid = 0;
    for (uint32_t b = 0; b < nblk; b++) {
        uint8_t hb[REC_HDR];
        if (fl_read(fs, b * fs->bsize, hb, REC_HDR)) return MCS_VFS_EIO;
        if (memcmp(hb, "TFS1", 4) || ~crc_upd(~0u, hb, 12) != rd32(hb + 12) || !rd32(hb + 4)) continue;
        fs->valid[b] = 1; fs->seq[b] = rd32(hb + 4); gc_from[b] = rd32(hb + 8);
        uint32_t pos = fs->hs;
        while (pos + REC_HDR <= fs->bsize) {
            rh_t r;
            if (fl_read(fs, b * fs->bsize + pos, hb, REC_HDR)) return MCS_VFS_EIO;
            if (is_blank(hb, REC_HDR)) break;
            rh_unpack(hb, &r);
            if (!rh_sane(fs, &r, pos) || !rec_crc_ok(fs, b * fs->bsize + pos, hb, &r)) break;
            if (r.type == T_COMMIT) has_commit[b] = 1;
            pos += rsize(fs, r.plen);
        }
        fs->end[b] = pos;
        if (fs->seq[b] > fs->max_seq) fs->max_seq = fs->seq[b];
        nvalid++;
    }
    /* B: a garbage-collection copy without COMMIT is void; with COMMIT its victim is obsolete */
    for (uint32_t b = 0; b < nblk; b++) if (fs->valid[b] && gc_from[b] && !has_commit[b]) fs->valid[b] = 0;
    for (uint32_t b = 0; b < nblk; b++) {
        if (!fs->valid[b] || !gc_from[b]) continue;
        for (uint32_t o = 0; o < nblk; o++) {
            if (fs->valid[o] && fs->seq[o] == gc_from[b]) {
                fs->valid[o] = 0;
                if (fl_erase(fs, o)) return MCS_VFS_EIO;
            }
        }
    }
    nvalid = 0;
    for (uint32_t b = 0; b < nblk; b++) nvalid += fs->valid[b];
    if (!nvalid) {
        if (!(flags & (MCS_FLASHFS_FORMAT_IF_NEEDED | MCS_FLASHFS_FORMAT))) return MCS_VFS_EIO;
        if (!(flags & MCS_FLASHFS_FORMAT) && format_all(fs)) return MCS_VFS_EIO;
        int e = open_block(fs, 0, 0);
        if (e) return e;
        fs->mounted = 1;
        return MCS_VFS_OK;
    }
    /* C: replay in sequence order (names first, then data) */
    for (int pass = 1; pass <= 2; pass++) {
        uint32_t last = 0;
        for (uint32_t n = 0; n < nvalid; n++) {
            int pick = -1;
            for (uint32_t b = 0; b < nblk; b++)
                if (fs->valid[b] && fs->seq[b] > last && (pick < 0 || fs->seq[b] < fs->seq[pick])) pick = (int)b;
            if (pick < 0) break;
            last = fs->seq[pick];
            int e = replay_block(fs, (uint32_t)pick, pass);
            if (e) return e;
            if (pass == 2) { fs->head = pick; }
        }
    }
    for (int i = 0; i < MCS_TINYFS_MAX_FILES; i++) fs->live += fs->ent[i].rec;
    fs->off = fs->end[fs->head];
    {   /* anything after the last good record (a torn write) retires the block for appending */
        int blank;
        if (blank_range(fs, (uint32_t)fs->head * fs->bsize + fs->off, fs->bsize - fs->off, &blank)) return MCS_VFS_EIO;
        fs->sealed[fs->head] = !blank;
    }
    fs->mounted = 1;
    return MCS_VFS_OK;
}

int mcs_tinyfs_unmount(mcs_tinyfs_t* fs) {
    if (!fs || !fs->mounted) return MCS_VFS_EINVAL;
    int e = flush_all(fs);
    fs->mounted = 0;
    return e;
}

/* ---- vfs ops ---- */
static int t_open(void* ctx, const char* path, int flags, void** fh) {
    tfs_t* fs = (tfs_t*)ctx;
    if (!strcmp(path, "/")) return MCS_VFS_EISDIR;
    hnd_t* h = NULL;
    for (int i = 0; i < MCS_TINYFS_HANDLES && !h; i++) if (!fs->h[i].used) h = &fs->h[i];
    if (!h) return MCS_VFS_ENOMEM;
    int r;
    flush_all(fs);
    ent_t* e = find_path(fs, path);
    if (e && e->dir) return MCS_VFS_EISDIR;
    if (flags & MCS_VFS_WRITE) {
        if ((r = create_entry(fs, path, false, &e))) return r;
    } else if (!e) {
        if (!(flags & MCS_VFS_APPEND)) return MCS_VFS_ENOENT;
        if ((r = create_entry(fs, path, false, &e))) return r;
    }
    memset(h, 0, sizeof *h);
    h->used = 1; h->id = e->id; h->flags = (uint8_t)flags;
    if (flags & MCS_VFS_APPEND) h->pos = e->size;
    *fh = h;
    return MCS_VFS_OK;
}

static int t_read(void* ctx, void* fh, void* buf, size_t n) {
    tfs_t* fs = (tfs_t*)ctx;
    hnd_t* h = (hnd_t*)fh;
    flush_all(fs);
    ent_t* e = by_id(fs, h->id);
    if (!e) return MCS_VFS_ENOENT;
    if (n > 0x7fffffff) n = 0x7fffffff;
    uint32_t avail = h->pos < e->size ? e->size - h->pos : 0, want = n < avail ? (uint32_t)n : avail, got = 0;
    for (uint32_t b = 0; b < fs->nblk && got < want; b++) {
        if (!fs->valid[b]) continue;
        for (uint32_t pos = fs->hs; pos < fs->end[b];) {
            uint8_t hb[REC_HDR];
            rh_t r;
            uint32_t a = b * fs->bsize + pos;
            if (fl_read(fs, a, hb, REC_HDR)) return MCS_VFS_EIO;
            rh_unpack(hb, &r);
            pos += rsize(fs, r.plen);
            if (r.type != T_DATA || r.id != e->id) continue;
            uint32_t lo = r.off > h->pos ? r.off : h->pos, hi = r.off + r.plen < h->pos + want ? r.off + r.plen : h->pos + want;
            if (lo >= hi) continue;
            if (fl_read(fs, a + REC_HDR + (lo - r.off), (uint8_t*)buf + (lo - h->pos), hi - lo)) return MCS_VFS_EIO;
            got += hi - lo;
        }
    }
    if (got < want) return MCS_VFS_EIO;
    h->pos += want;
    return (int)want;
}

static int t_write(void* ctx, void* fh, const void* buf, size_t n) {
    tfs_t* fs = (tfs_t*)ctx;
    hnd_t* h = (hnd_t*)fh;
    if (!(h->flags & (MCS_VFS_WRITE | MCS_VFS_APPEND))) return MCS_VFS_EACCES;
    ent_t* e = by_id(fs, h->id);
    if (!e) return MCS_VFS_EIO;
    if (h->err) return h->err;
    if (h->pos != e->size + h->len) return MCS_VFS_EINVAL;      /* files are written sequentially */
    if (n > 0x7fffffff) n = 0x7fffffff;
    const uint8_t* p = (const uint8_t*)buf;
    size_t left = n;
    while (left) {
        uint32_t k = MCS_TINYFS_CHUNK - h->len;
        if (k > left) k = (uint32_t)left;
        memcpy(h->buf + h->len, p, k);
        h->len = (uint16_t)(h->len + k); h->pos += k; p += k; left -= k;
        if (h->len == MCS_TINYFS_CHUNK) { int r = flush_handle(fs, h); if (r) return r; }
    }
    return (int)n;
}

static int t_close(void* ctx, void* fh) {
    tfs_t* fs = (tfs_t*)ctx;
    hnd_t* h = (hnd_t*)fh;
    int r = flush_handle(fs, h);
    h->used = 0;
    return r;
}

static int t_stat(void* ctx, const char* path, mcs_vfs_stat_t* st) {
    tfs_t* fs = (tfs_t*)ctx;
    if (!strcmp(path, "/")) { st->size = 0; st->is_dir = true; return MCS_VFS_OK; }
    flush_all(fs);
    ent_t* e = find_path(fs, path);
    if (!e) return MCS_VFS_ENOENT;
    st->size = e->size; st->is_dir = e->dir;
    return MCS_VFS_OK;
}

static int t_remove(void* ctx, const char* path) {
    tfs_t* fs = (tfs_t*)ctx;
    flush_all(fs);
    ent_t* e = !strcmp(path, "/") ? NULL : find_path(fs, path);
    if (!e) return MCS_VFS_ENOENT;
    if (e->dir && has_children(fs, path)) return MCS_VFS_ENOTEMPTY;
    int r = emit(fs, T_DEL, e->dir, e->id, 0, NULL, 0, NULL);
    if (r) return r;
    drop_entry(fs, e);
    return MCS_VFS_OK;
}

static int t_mkdir(void* ctx, const char* path) {
    tfs_t* fs = (tfs_t*)ctx;
    if (!strcmp(path, "/") || find_path(fs, path)) return MCS_VFS_EEXIST;
    ent_t* e;
    return create_entry(fs, path, true, &e);
}

static int t_rename(void* ctx, const char* from, const char* to) {
    tfs_t* fs = (tfs_t*)ctx;
    flush_all(fs);
    ent_t* e = !strcmp(from, "/") ? NULL : find_path(fs, from);
    if (!e) return MCS_VFS_ENOENT;
    if (find_path(fs, to) || !strcmp(to, "/")) return MCS_VFS_EEXIST;
    size_t n;
    int r = check_len(to, &n);
    if (r || (r = check_parent(fs, to))) return r;
    if (e->dir && has_children(fs, from)) return MCS_VFS_EINVAL;   /* only empty directories move */
    uint32_t oldrec = rsize(fs, e->nlen), need = rsize(fs, (uint32_t)n), addr;
    if (fs->live - oldrec + need > fs->allowed) return MCS_VFS_ENOSPC;
    if ((r = put_name(fs, e->id, e->dir, to, n, &addr))) return r;
    e->name_addr = addr; e->nlen = (uint8_t)n; e->hash = hash_path(to, n);
    e->rec = e->rec - oldrec + need; fs->live = fs->live - oldrec + need;
    return MCS_VFS_OK;
}

static int t_list(void* ctx, const char* path, mcs_vfs_list_cb cb, void* ud) {
    tfs_t* fs = (tfs_t*)ctx;
    flush_all(fs);
    if (strcmp(path, "/")) {
        ent_t* d = find_path(fs, path);
        if (!d) return MCS_VFS_ENOENT;
        if (!d->dir) return MCS_VFS_ENOTDIR;
    }
    size_t pl = strcmp(path, "/") ? strlen(path) : 0;
    uint32_t last = 0;
    for (;;) {                                   /* creation (id) order */
        ent_t* e = NULL;
        for (int i = 0; i < MCS_TINYFS_MAX_FILES; i++)
            if (fs->ent[i].id > last && (!e || fs->ent[i].id < e->id)) e = &fs->ent[i];
        if (!e) break;
        last = e->id;
        char nm[MCS_VFS_PATH_MAX];
        if (e->nlen <= pl || read_name(fs, e, nm)) continue;
        if (pl && (strncmp(nm, path, pl) || nm[pl] != '/')) continue;
        const char* leaf = nm + pl + 1;
        if (strchr(leaf, '/')) continue;
        mcs_vfs_stat_t st = { e->size, e->dir };
        if (cb(ud, leaf, &st)) break;
    }
    return MCS_VFS_OK;
}

static int t_statfs(void* ctx, mcs_vfs_statfs_t* st) {
    tfs_t* fs = (tfs_t*)ctx;
    uint64_t per = rsize(fs, MCS_TINYFS_CHUNK);       /* flash bytes per full data record */
    st->total = (uint64_t)fs->allowed * MCS_TINYFS_CHUNK / per;
    st->free = fs->live < fs->allowed ? (uint64_t)(fs->allowed - fs->live) * MCS_TINYFS_CHUNK / per : 0;
    st->format = "tinyfs";
    return MCS_VFS_OK;
}

const mcs_vfs_ops_t mcs_tinyfs_ops = { t_open, t_read, t_write, t_close, t_stat, t_remove, t_mkdir, t_rename, t_list, t_statfs };
#endif
