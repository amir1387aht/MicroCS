/*
 * MicroCS - optional LVGL 8.x / 9.x binding (MCS_ENABLE_LVGL=1). docs/LVGL.md.
 *
 * The C# API is LVGL's own: every function, enum, LV_* constant, LV_SYMBOL_* string,
 * built-in font and C type keeps its C name. The tables and one wrapper per function
 * are generated (tools/lvgl_gen, mcs_lvgl_api.inc) for every LVGL release 8.x-9.x and
 * guarded by the version and lv_conf.h features, so the same sources build against
 * whatever LVGL the firmware links. This file is the hand-written part: value
 * conversion, object lifetime, callbacks (events, timers, animations, async calls)
 * and the LVGL file-system driver on top of the MicroCS VFS.
 *
 * Pointers are wrapped in small userdata objects whose class is the C type name
 * (lv_obj_t, lv_style_t, lv_font_t, ...). An lv_obj_t has one wrapper for its whole
 * life (so == works), and LVGL deleting the object disposes it: using it afterwards
 * raises ObjectDisposedException instead of touching freed memory.
 */
#include "mcs.h"
#include "mcs_config.h"
#if MCS_ENABLE_LVGL
#if defined(MCS_LVGL_INCLUDE)
#include MCS_LVGL_INCLUDE
#elif defined(__has_include)
#if __has_include(<lvgl.h>)
#include <lvgl.h>
#elif __has_include("lvgl/lvgl.h")
#include "lvgl/lvgl.h"
#elif __has_include(<lvgl/lvgl.h>)
#include <lvgl/lvgl.h>
#else
#error "MCS_ENABLE_LVGL=1 needs LVGL 8.x or 9.x on the include path (lvgl.h) - MicroCS does not bundle it; set MCS_LVGL_INCLUDE=\"path/lvgl.h\" or see docs/LVGL.md"
#endif
#else
#include <lvgl.h>
#endif
#include "mcs_lvgl.h"
#if MCS_ENABLE_FS
#include "mcs_vfs.h"
#endif
#include <string.h>
#include <stdio.h>

#define MCS_LV_VER (LVGL_VERSION_MAJOR * 10000 + LVGL_VERSION_MINOR * 100 + LVGL_VERSION_PATCH)
#if LVGL_VERSION_MAJOR < 8 || LVGL_VERSION_MAJOR > 9
#error "MicroCS LVGL binding: LVGL 8.x or 9.x required"
#endif

/* ---------------------------------------------------------------- v8 / v9 spelling */
#if LVGL_VERSION_MAJOR >= 9
#define LVB_MALLOC(n) lv_malloc(n)
#define LVB_FREE(p) lv_free(p)
#define LVB_OBJ_DELETE(o) lv_obj_delete(o)
#define LVB_SCREEN_ACTIVE() lv_screen_active()
#define LVB_SCREEN_LOAD(s) lv_screen_load(s)
#define LVB_TIMER_DELETE(t) lv_timer_delete(t)
#define LVB_TIMER_UD(t) lv_timer_get_user_data(t)
#define LVB_COLOR_TO_INT(c) ((mcs_int_t)(lv_color_to_u32(c) & 0xFFFFFFu))
#define LVB_ANIM_DELETE(v, cb) lv_anim_delete(v, cb)
#else
#define LVB_MALLOC(n) lv_mem_alloc(n)
#define LVB_FREE(p) lv_mem_free(p)
#define LVB_OBJ_DELETE(o) lv_obj_del(o)
#define LVB_SCREEN_ACTIVE() lv_scr_act()
#define LVB_SCREEN_LOAD(s) lv_scr_load(s)
#define LVB_TIMER_DELETE(t) lv_timer_del(t)
#define LVB_TIMER_UD(t) ((t)->user_data)
#define LVB_COLOR_TO_INT(c) ((mcs_int_t)(lv_color_to32(c) & 0xFFFFFFu))
#define LVB_ANIM_DELETE(v, cb) lv_anim_del(v, cb)
#endif

/* ---------------------------------------------------------------- state */
#define LVB_MAGIC 0x4C56474Cu   /* 'LVGL' */
#define LVB_DEAD  0x44454144u
typedef struct {
    uint32_t magic;
    uint16_t cls;
    uint8_t owned;     /* storage allocated for `new T()` */
    uint8_t retained;  /* that storage lives until the VM closes (LVGL keeps pointers to it) */
    uint8_t dying;     /* lv_obj_t being deleted: valid only inside its LV_EVENT_DELETE */
    void* p;
} lvb_ud_t;

enum { CB_EVENT = 1, CB_TIMER, CB_ASYNC, CB_ANIM, CB_OBSERVER, CB_MSG, CB_MSGOBJ, CB_LOG };
typedef struct lvb_cb {
    uint32_t magic;
    uint8_t kind;
    struct lvb_cb* next;
    struct lvb_state* st;
    void* target;     /* object / timer / anim template storage */
    void* handle;     /* lv_timer_t* */
    int fn, ud;       /* keep-list indexes (C# callable, C# user data) */
    int var, ready, start, deleted;   /* animations */
    int code;         /* event filter (CB_EVENT) */
    int getval;       /* anim get_value_cb */
    uint8_t custom;   /* anim exec cb takes (anim, value) instead of (var, value) */
} lvb_cb_t;

typedef struct lvb_kept { struct lvb_kept* next; const char* key; int idx; } lvb_kept_t;
typedef struct {
    void* p;           /* lv_obj_t*; NULL = free slot, (void*)1 = tombstone */
    int wrap;          /* keep-list index of the wrapper */
    int ud;            /* C# user data (lv_obj_set_user_data) or -1 */
    uint8_t created;   /* returned by an *_create() call of a script */
    uint8_t dying;
    lvb_kept_t* kept;  /* values LVGL points into (static texts, maps) */
} lvb_objrec_t;

typedef struct lvb_own { struct lvb_own* next; size_t size; } lvb_own_t;   /* + storage */

/* arrays / buffers LVGL keeps pointing to, replaced when the same (kind, k1, k2) is set again */
typedef struct lvb_slot { struct lvb_slot* next; int kind; const void* k1; const void* k2; size_t size; void* raw; void* mem; } lvb_slot_t;

typedef struct lvb_state {
    mcs_vm_t* vm;
    int keep_pin;           /* mcs_pin of the keep List */
    mcs_value_t keep;
    int* free_idx; uint32_t nfree, capfree;
    lvb_objrec_t* objs; uint32_t cap, used;
    lvb_cb_t* cbs;          /* live callbacks */
    lvb_cb_t* zombies;      /* removed, freed at the next safe point */
    lvb_own_t* owned;       /* retained `new` storage */
    lvb_slot_t* slots;
    int event_depth;
    void* del_target;
    uint8_t wrapping, closing, has_dying;
    lv_obj_t* initial_screen;
    mcs_idle_fn prev_idle; void* prev_idle_ud;
} lvb_state_t;

static lvb_state_t* ST(mcs_vm_t* vm) { return (lvb_state_t*)mcs_get_ext(vm, MCS_EXT_LVGL); }

/* ---- keep list: one pinned C# List roots every value LVGL refers to */
static int keep(lvb_state_t* st, mcs_value_t v) {
    if (st->nfree) { int i = st->free_idx[--st->nfree]; mcs_set_index(st->keep, (uint32_t)i, v); return i; }
    int i = (int)mcs_len(st->keep);
    mcs_list_add(st->vm, st->keep, v);
    return i;
}
static void unkeep(lvb_state_t* st, int i) {
    if (i < 0) return;
    mcs_set_index(st->keep, (uint32_t)i, mcs_null());
    if (st->nfree == st->capfree) {
        uint32_t nc = st->capfree ? st->capfree * 2u : 16u;
        int* n = (int*)mcs_mem_realloc(st->vm, st->free_idx, st->capfree * sizeof(int), nc * sizeof(int));
        if (!n) return;   /* the slot just stays unused */
        st->free_idx = n; st->capfree = nc;
    }
    st->free_idx[st->nfree++] = i;
}
static mcs_value_t kept(lvb_state_t* st, int i) { return i < 0 ? mcs_null() : mcs_index(st->keep, (uint32_t)i); }

/* ---- lv_obj_t records: open addressing on the pointer */
#define TOMB ((void*)1)
static uint32_t ptr_hash(const void* p) { uintptr_t x = (uintptr_t)p; x ^= x >> 16; x *= 0x45d9f3bu; x ^= x >> 16; return (uint32_t)x; }
static lvb_objrec_t* obj_find(lvb_state_t* st, const void* p) {
    if (!st->cap) return NULL;
    for (uint32_t i = ptr_hash(p) & (st->cap - 1u), n = 0; n < st->cap; i = (i + 1u) & (st->cap - 1u), n++) {
        if (!st->objs[i].p) return NULL;
        if (st->objs[i].p == p) return &st->objs[i];
    }
    return NULL;
}
static lvb_objrec_t* obj_insert(lvb_state_t* st, void* p) {
    if ((st->used + 1u) * 4u >= st->cap * 3u) {
        uint32_t oc = st->cap, nc = oc ? oc * 2u : 32u;
        lvb_objrec_t* n = (lvb_objrec_t*)mcs_mem_realloc(st->vm, NULL, 0, nc * sizeof *n);
        if (!n) return NULL;
        memset(n, 0, nc * sizeof *n);
        lvb_objrec_t* old = st->objs;
        st->objs = n; st->cap = nc; st->used = 0;
        for (uint32_t i = 0; i < oc; i++) if (old[i].p && old[i].p != TOMB) { *obj_insert(st, old[i].p) = old[i]; }
        if (old) mcs_mem_realloc(st->vm, old, oc * sizeof *old, 0);
    }
    uint32_t i = ptr_hash(p) & (st->cap - 1u);
    while (st->objs[i].p && st->objs[i].p != TOMB) i = (i + 1u) & (st->cap - 1u);
    lvb_objrec_t* r = &st->objs[i];
    memset(r, 0, sizeof *r);
    r->p = p; r->wrap = -1; r->ud = -1;
    st->used++;
    return r;
}

static void cb_zombify(lvb_state_t* st, lvb_cb_t* c) {
    for (lvb_cb_t** pp = &st->cbs; *pp; pp = &(*pp)->next)
        if (*pp == c) { *pp = c->next; break; }
    c->magic = LVB_DEAD;
    unkeep(st, c->fn); unkeep(st, c->ud); unkeep(st, c->var); unkeep(st, c->ready); unkeep(st, c->start); unkeep(st, c->deleted);
    c->fn = c->ud = c->var = c->ready = c->start = c->deleted = c->getval = -1;
    c->next = st->zombies; st->zombies = c;
}

/* finish deletions: dispose the wrappers of deleted objects, free removed callbacks */
static void reap(lvb_state_t* st) {
    if (st->event_depth) return;
    if (st->has_dying) {
        st->has_dying = 0;
        for (uint32_t i = 0; i < st->cap; i++) {
            lvb_objrec_t* r = &st->objs[i];
            if (!r->p || r->p == TOMB || !r->dying) continue;
            mcs_value_t w = kept(st, r->wrap);
            lvb_ud_t* u = (lvb_ud_t*)mcs_userdata(w);
            if (u) { u->p = NULL; u->dying = 0; }
            unkeep(st, r->wrap); unkeep(st, r->ud);
            while (r->kept) { lvb_kept_t* k = r->kept; r->kept = k->next; unkeep(st, k->idx); mcs_mem_realloc(st->vm, k, sizeof *k, 0); }
            for (lvb_cb_t* c = st->cbs, *nx; c; c = nx) { nx = c->next; if (c->kind == CB_EVENT && c->target == r->p) cb_zombify(st, c); }
            r->p = TOMB; st->used--;
        }
    }
    while (st->zombies) { lvb_cb_t* c = st->zombies; st->zombies = c->next; LVB_FREE(c); }
}
#define REAP(st) do { if ((st) && ((st)->has_dying || (st)->zombies)) reap(st); } while (0)

static void on_obj_delete(lv_event_t* e);

/* ---------------------------------------------------------------- class helpers */
static mcs_value_t lvb_ret_ptr(mcs_vm_t* vm, void* p, uint16_t cls);
static void lvb_ctor(mcs_vm_t* vm, mcs_value_t self, size_t size, uint16_t cls, int retain);
static void lvb_finalize(mcs_vm_t* vm, void* data);
static void* lvb_self(mcs_vm_t* vm, mcs_value_t self, uint16_t cls);
static mcs_value_t lvb_op_eq(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv);
static mcs_value_t lvb_op_ne(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv);
static mcs_value_t lvb_m_equals(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv);
static mcs_value_t lvb_m_hash(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv);
static mcs_value_t lvb_m_tostring(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv);
static const mcs_reg_t lvb_statics[] = {
    MCS_FN("op_Equality", lvb_op_eq, 2), MCS_FN("op_Inequality", lvb_op_ne, 2), MCS_REG_END
};
#define LVB_MEMBERS_COMMON MCS_FN("Equals", lvb_m_equals, 1), MCS_FN("GetHashCode", lvb_m_hash, 0), \
    MCS_FN("ToString", lvb_m_tostring, 0), MCS_REG_END

/* ---------------------------------------------------------------- argument conversion */
static lvb_ud_t* ud_of(mcs_value_t v) {
    lvb_ud_t* u = (lvb_ud_t*)mcs_userdata(v);
    return u && u->magic == LVB_MAGIC ? u : NULL;
}
static uint16_t lvb_canon(uint16_t c);
static const char* const lvb_class_names[];

static void* lvb_arg_ptr(mcs_vm_t* vm, mcs_value_t v, uint16_t cls, int nullable) {
    lvb_state_t* st = ST(vm);
    REAP(st);
    if (mcs_is_null(v)) {
        if (!nullable) mcs_raise(vm, "ArgumentNullException", "%s must not be null", lvb_class_names[cls]);
        return NULL;
    }
    lvb_ud_t* u = ud_of(v);
    if (!u || lvb_canon(u->cls) != lvb_canon(cls)) {
        mcs_raise(vm, "InvalidCastException", "%s expected, got %s", lvb_class_names[cls], u ? lvb_class_names[u->cls] : "a non-LVGL value");
        return NULL;
    }
    if (!u->p || (u->dying && u->p != st->del_target)) {
        mcs_raise(vm, "ObjectDisposedException", "%s was deleted", lvb_class_names[cls]);
        return NULL;
    }
    return u->p;
}
static void* lvb_self(mcs_vm_t* vm, mcs_value_t self, uint16_t cls) { return lvb_arg_ptr(vm, self, cls, 0); }

static void lvb_arg_struct(mcs_vm_t* vm, mcs_value_t v, uint16_t cls, void* out, size_t size) {
    void* p = lvb_arg_ptr(vm, v, cls, 0);
    if (p) memcpy(out, p, size); else memset(out, 0, size);
}
#if defined(__GNUC__)
__attribute__((unused))
#endif
static double lvb_arg_float(mcs_vm_t* vm, mcs_value_t v) {
#if MCS_ENABLE_FLOAT
    return (double)mcs_to_float(vm, v);
#else
    return (double)mcs_to_int(vm, v);
#endif
}
#if defined(__GNUC__)
__attribute__((unused))
#endif
static mcs_value_t lvb_ret_float(double d) {
#if MCS_ENABLE_FLOAT
    return mcs_float((mcs_float_t)d);
#else
    return mcs_int((mcs_int_t)d);
#endif
}
static lv_color_t lvb_arg_color(mcs_vm_t* vm, mcs_value_t v) {
    return lv_color_hex((uint32_t)mcs_to_int(vm, v) & 0xFFFFFFu);
}
static mcs_value_t lvb_ret_color(lv_color_t c) { return mcs_int(LVB_COLOR_TO_INT(c)); }
static const char* lvb_arg_str(mcs_vm_t* vm, mcs_value_t v) {
    if (mcs_is_null(v)) return NULL;
    return mcs_to_cstr(vm, v);
}
static mcs_value_t lvb_ret_str(mcs_vm_t* vm, const char* s) { return s ? mcs_string(vm, s) : mcs_null(); }
/* void*: null, an LVGL wrapper (its pointer), a string (image sources, paths) or byte[] */
static void* lvb_arg_voidp(mcs_vm_t* vm, mcs_value_t v) {
    if (mcs_is_null(v)) return NULL;
    lvb_ud_t* u = ud_of(v);
    if (u) return lvb_arg_ptr(vm, v, u->cls, 0);
    if (mcs_is_string(v)) return (void*)mcs_cstr(v);
    mcs_raise(vm, "InvalidCastException", "LVGL pointer, string or null expected");
    return NULL;
}
static mcs_value_t lvb_ret_struct(mcs_vm_t* vm, const void* p, size_t size, uint16_t cls) {
    mcs_value_t o;
    if (mcs_new_object(vm, lvb_class_names[cls], 0, NULL, &o) != MCS_OK) return mcs_null();
    lvb_ud_t* u = ud_of(o);
    if (u && u->p) memcpy(u->p, p, size);
    else if (u && !u->p) { lvb_ctor(vm, o, size, cls, 0); if (u->p) memcpy(u->p, p, size); }
    return o;
}

/* "/dir/file.png" -> "M:/dir/file.png": script paths are VFS paths */
#ifndef MCS_VFS_PATH_MAX
#define MCS_VFS_PATH_MAX 128
#endif
static const char* lvb_path(const char* s) {
    static char ring[4][MCS_VFS_PATH_MAX + 4];
    static unsigned at;
    if (!s || s[0] != '/' || !MCS_LVGL_FS_LETTER) return s;
    char* b = ring[at++ & 3u];
    snprintf(b, sizeof ring[0], "%c:%s", (char)MCS_LVGL_FS_LETTER, s);
    return b;
}
static const char* lvb_arg_path(mcs_vm_t* vm, mcs_value_t v) { return lvb_path(lvb_arg_str(vm, v)); }
static void* lvb_arg_src(mcs_vm_t* vm, mcs_value_t v) {
    void* p = lvb_arg_voidp(vm, v);
    return mcs_is_string(v) ? (void*)lvb_path((const char*)p) : p;
}

/* a C string kept alive for as long as the object exists (lv_label_set_text_static...) */
static void lvb_keep_for(mcs_vm_t* vm, void* obj, const char* key, mcs_value_t v) {
    lvb_state_t* st = ST(vm);
    lvb_objrec_t* r = st ? obj_find(st, obj) : NULL;
    if (!r) return;
    for (lvb_kept_t* k = r->kept; k; k = k->next)
        if (k->key == key) { mcs_set_index(st->keep, (uint32_t)k->idx, v); return; }
    lvb_kept_t* k = (lvb_kept_t*)mcs_mem_realloc(vm, NULL, 0, sizeof *k);
    if (!k) return;
    k->key = key; k->idx = keep(st, v); k->next = r->kept; r->kept = k;
}

/* ---------------------------------------------------------------- wrappers */
static mcs_value_t lvb_ret_created(mcs_vm_t* vm, void* p);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"   /* the binding wraps deprecated API too */
#endif
#ifdef MCS_LVGL_API_INC
#include MCS_LVGL_API_INC   /* make mcs-lvgl: tables generated for exactly this LVGL */
#else
#include "mcs_lvgl_api.inc"  /* python3 tools/lvgl_gen/build_api.py --all (generated, not committed) */
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static int is_obj_class(uint16_t cls) {
    uint16_t c = lvb_canon(cls);
    return c == lvb_canon(LVB_C_lv_obj_t)
#ifdef LVB_C__lv_obj_t
        || c == lvb_canon(LVB_C__lv_obj_t)
#endif
        ;
}

static mcs_value_t new_wrapper(mcs_vm_t* vm, void* p, uint16_t cls) {
    lvb_state_t* st = ST(vm);
    mcs_value_t o;
    st->wrapping = 1;
    mcs_result_t r = mcs_new_object(vm, lvb_class_names[cls], 0, NULL, &o);
    st->wrapping = 0;
    if (r != MCS_OK) return mcs_null();
    lvb_ud_t* u = (lvb_ud_t*)mcs_userdata(o);
    u->magic = LVB_MAGIC; u->cls = cls; u->p = p;
    return o;
}

static mcs_value_t lvb_ret_ptr(mcs_vm_t* vm, void* p, uint16_t cls) {
    lvb_state_t* st = ST(vm);
    if (!p) return mcs_null();
    REAP(st);
    if (!is_obj_class(cls)) return new_wrapper(vm, p, cls);
    lvb_objrec_t* r = obj_find(st, p);
    if (r && !r->dying) return kept(st, r->wrap);
    if (r && r->dying) return kept(st, r->wrap);   /* inside its own delete event */
    mcs_value_t o = new_wrapper(vm, p, LVB_C_lv_obj_t);
    if (mcs_is_null(o)) return o;
    mcs_push_root(vm, o);
    r = obj_insert(st, p);
    if (!r) { mcs_pop_root(vm, 1); mcs_raise(vm, "OutOfMemoryException", "LVGL object table"); return mcs_null(); }
    r->wrap = keep(st, o);
    mcs_pop_root(vm, 1);
    MCS_LVGL_LOCK();
    lv_obj_add_event_cb((lv_obj_t*)p, on_obj_delete, LV_EVENT_DELETE, st);
    MCS_LVGL_UNLOCK();
    return o;
}

/* an object a script created: deleted when the VM closes (MCS_LVGL_CLEANUP_ON_CLOSE) */
static mcs_value_t lvb_ret_created(mcs_vm_t* vm, void* p) {
    mcs_value_t o = lvb_ret_ptr(vm, p, LVB_C_lv_obj_t);
    lvb_objrec_t* r = p ? obj_find(ST(vm), p) : NULL;
    if (r) r->created = 1;
    return o;
}

static void on_obj_delete(lv_event_t* e) {
    lvb_state_t* st = (lvb_state_t*)lv_event_get_user_data(e);
    void* p = (void*)lv_event_get_target(e);
    lvb_objrec_t* r = st ? obj_find(st, p) : NULL;
    if (!r || r->dying) return;
    r->dying = 1; st->has_dying = 1;
    lvb_ud_t* u = ud_of(kept(st, r->wrap));
    if (u) u->dying = 1;
    /* animations of this object run through our exec callback: stop them */
    for (lvb_cb_t* c = st->cbs, *nx; c; c = nx) {
        nx = c->next;
        if (c->kind == CB_ANIM && c->var >= 0) {
            lvb_ud_t* vu = ud_of(kept(st, c->var));
            if (vu && vu->p == p) LVB_ANIM_DELETE(c, NULL);
        }
    }
}

static void lvb_ctor(mcs_vm_t* vm, mcs_value_t self, size_t size, uint16_t cls, int retain) {
    lvb_state_t* st = ST(vm);
    lvb_ud_t* u = (lvb_ud_t*)mcs_userdata(self);
    u->magic = LVB_MAGIC; u->cls = cls;
    if (st && st->wrapping) return;
    lvb_own_t* o = (lvb_own_t*)LVB_MALLOC(sizeof(lvb_own_t) + size + 8u);
    if (!o) { mcs_raise(vm, "OutOfMemoryException", "%s", lvb_class_names[cls]); return; }
    memset(o, 0, sizeof(lvb_own_t) + size + 8u);
    o->size = size;
    u->owned = 1; u->retained = (uint8_t)(retain && st);
    u->p = (void*)(((uintptr_t)(o + 1) + 7u) & ~(uintptr_t)7u);
    if (u->retained) { o->next = st->owned; st->owned = o; }
}
static void lvb_finalize(mcs_vm_t* vm, void* data) {
    (void)vm;
    lvb_ud_t* u = (lvb_ud_t*)data;
    if (u->magic != LVB_MAGIC || !u->owned || u->retained || !u->p) return;
    /* storage header sits right before the (aligned) payload */
    uintptr_t base = (uintptr_t)u->p;
    for (uintptr_t off = sizeof(lvb_own_t); off <= sizeof(lvb_own_t) + 8u; off++) {
        lvb_own_t* o = (lvb_own_t*)(base - off);
        if ((((uintptr_t)(o + 1) + 7u) & ~(uintptr_t)7u) == base) { LVB_FREE(o); break; }
    }
    u->p = NULL;
}
static int same_ptr(mcs_value_t a, mcs_value_t b) {
    if (mcs_is_null(a) || mcs_is_null(b)) {
        lvb_ud_t* x = ud_of(mcs_is_null(a) ? b : a);
        return mcs_is_null(a) && mcs_is_null(b) ? 1 : (x && !x->p);
    }
    lvb_ud_t* x = ud_of(a); lvb_ud_t* y = ud_of(b);
    return x && y && x->p == y->p;
}
static mcs_value_t lvb_op_eq(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { (void)vm; (void)self; (void)argc; return mcs_bool(same_ptr(argv[0], argv[1])); }
static mcs_value_t lvb_op_ne(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { (void)vm; (void)self; (void)argc; return mcs_bool(!same_ptr(argv[0], argv[1])); }
static mcs_value_t lvb_m_equals(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { (void)vm; (void)argc; return mcs_bool(same_ptr(self, argv[0])); }
static mcs_value_t lvb_m_hash(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)vm; (void)argc; (void)argv; lvb_ud_t* u = ud_of(self); return mcs_int(u ? (mcs_int_t)(ptr_hash(u->p) & 0x7FFFFFFF) : 0);
}
static mcs_value_t lvb_m_tostring(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)argc; (void)argv; lvb_ud_t* u = ud_of(self); char b[96];
    snprintf(b, sizeof b, "%s(%s)", u ? lvb_class_names[u->cls] : "lv", u && u->p ? "live" : "deleted");
    return mcs_string(vm, b);
}

/* ---------------------------------------------------------------- callbacks */
static lvb_cb_t* cb_new(lvb_state_t* st, int kind, void* target) {
    lvb_cb_t* c = (lvb_cb_t*)LVB_MALLOC(sizeof *c);
    if (!c) { mcs_raise(st->vm, "OutOfMemoryException", "LVGL callback"); return NULL; }
    memset(c, 0, sizeof *c);
    c->magic = LVB_MAGIC; c->kind = (uint8_t)kind; c->st = st; c->target = target;
    c->fn = c->ud = c->var = c->ready = c->start = c->deleted = -1;
    c->next = st->cbs; st->cbs = c;
    return c;
}
static mcs_value_t check_callable(mcs_vm_t* vm, mcs_value_t f, int nullable) {
    if (mcs_is_null(f) && nullable) return f;
    int k = mcs_obj_kind(f);
    if (k != MCS_O_CLOSURE && k != MCS_O_NATIVE && k != MCS_O_BOUND && k != MCS_O_FUNCTION && k != MCS_O_OVERLOADS)
        mcs_raise(vm, "ArgumentException", "a callback (lambda, method or delegate) expected");
    return f;
}
/* call a script callback from LVGL; errors stay pending for the native that started
 * LVGL (lv_timer_handler, lv_obj_send_event...) or are reported when there is none */
static void cb_call(lvb_state_t* st, int fn_idx, int argc, mcs_value_t* argv) {
    mcs_vm_t* vm = st->vm;
    if (st->closing || fn_idx < 0 || mcs_has_exception(vm)) return;
    mcs_value_t f = kept(st, fn_idx), res;
    int ar = mcs_arity(f);
    if (ar >= 0 && ar < argc) argc = ar;
    st->event_depth++;
    mcs_result_t r = mcs_call_value(vm, f, argc, argv, &res);
    st->event_depth--;
    if (r != MCS_OK && !mcs_running(vm)) mcs_fail(vm, r, "%s", mcs_last_error(vm));
}
/* a short-lived wrapper (lv_event_t*, a running lv_anim_t*) disposed after the call */
static void tmp_dispose(mcs_value_t w) { lvb_ud_t* u = ud_of(w); if (u) u->p = NULL; }

static void event_tramp(lv_event_t* e) {
    lvb_cb_t* c = (lvb_cb_t*)lv_event_get_user_data(e);
    if (!c || c->magic != LVB_MAGIC) return;
    lvb_state_t* st = c->st;
    mcs_vm_t* vm = st->vm;
    if (st->closing || mcs_has_exception(vm)) return;
    void* prev = st->del_target;
    if (lv_event_get_code(e) == LV_EVENT_DELETE) st->del_target = (void*)lv_event_get_target(e);
    mcs_value_t ev = new_wrapper(vm, e, LVB_C_lv_event_t);
    mcs_push_root(vm, ev);
    cb_call(st, c->fn, 1, &ev);
    tmp_dispose(ev);
    mcs_pop_root(vm, 1);
    st->del_target = prev;
}

static mcs_value_t lvo_lv_obj_add_event_cb(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    check_callable(vm, argv[1], 0);
    lv_event_code_t code = (lv_event_code_t)mcs_to_int(vm, argv[2]);
    if (mcs_has_exception(vm)) return mcs_null();
    lvb_ret_ptr(vm, obj, LVB_C_lv_obj_t);   /* make sure it has a record (deletion tracking) */
    lvb_cb_t* c = cb_new(st, CB_EVENT, obj);
    if (!c) return mcs_null();
    c->fn = keep(st, argv[1]);
    c->ud = keep(st, argv[3]);
    c->code = (int)code;
    MCS_LVGL_LOCK();
    lv_obj_add_event_cb(obj, event_tramp, code, c);
    MCS_LVGL_UNLOCK();
    return mcs_null();
}
/* LVGL < 8.1 can only remove event handlers by callback: drop all of ours, add the others back (oldest first) */
#if MCS_LV_VER < 80100
static void readd_events(lv_obj_t* obj, lvb_cb_t* x, lvb_cb_t* skip) {
    if (!x) return;
    readd_events(obj, x->next, skip);
    if (x->kind == CB_EVENT && x->target == obj && x != skip) lv_obj_add_event_cb(obj, event_tramp, (lv_event_code_t)x->code, x);
}
#endif
static void remove_one_event(lv_obj_t* obj, lvb_cb_t* c) {
#if MCS_LV_VER >= 80100
    lv_obj_remove_event_cb_with_user_data(obj, event_tramp, c);
#else
    while (lv_obj_remove_event_cb(obj, event_tramp)) { }
    readd_events(obj, c->st->cbs, c);
#endif
}
/* remove the handlers of `obj` whose callable (and user data) match; -1 = any */
static int remove_events(mcs_vm_t* vm, lv_obj_t* obj, mcs_value_t* fn, mcs_value_t* ud) {
    lvb_state_t* st = ST(vm);
    int n = 0;
    for (lvb_cb_t* c = st->cbs, *nx; c; c = nx) {
        nx = c->next;
        if (c->kind != CB_EVENT || c->target != obj) continue;
        if (fn && !mcs_is_null(*fn) && !same_ptr(kept(st, c->fn), *fn)) {
            mcs_value_t a = kept(st, c->fn);
            if (!(a.type == fn->type && a.as.o == fn->as.o)) continue;
        }
        if (ud) {
            mcs_value_t a = kept(st, c->ud);
            if (!(a.type == ud->type && (a.type < MCS_T_OBJ ? a.as.i == ud->as.i : a.as.o == ud->as.o))) continue;
        }
        MCS_LVGL_LOCK();
        remove_one_event(obj, c);
        MCS_LVGL_UNLOCK();
        cb_zombify(st, c);
        n++;
    }
    REAP(st);
    return n;
}
static mcs_value_t lvo_lv_obj_remove_event_cb(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    if (mcs_has_exception(vm)) return mcs_null();
    int n = remove_events(vm, obj, &argv[1], NULL);
#if LVGL_VERSION_MAJOR >= 9
    return mcs_int(n);
#else
    return mcs_bool(n > 0);
#endif
}
static mcs_value_t lvo_lv_obj_remove_event_cb_with_user_data(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    if (mcs_has_exception(vm)) return mcs_null();
    int n = remove_events(vm, obj, &argv[1], &argv[2]);
#if LVGL_VERSION_MAJOR >= 9
    return mcs_int(n);
#else
    return mcs_bool(n > 0);
#endif
}
static mcs_value_t lvo_lv_event_get_user_data(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_event_t* e = (lv_event_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_event_t, 0);
    if (!e) return mcs_null();
    lvb_cb_t* c = (lvb_cb_t*)lv_event_get_user_data(e);
    return c && c->magic == LVB_MAGIC ? kept(c->st, c->ud) : mcs_null();
}
static mcs_value_t lvo_lv_event_get_target(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_event_t* e = (lv_event_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_event_t, 0);
    return e ? lvb_ret_ptr(vm, (void*)lv_event_get_target(e), LVB_C_lv_obj_t) : mcs_null();
}
static mcs_value_t lvo_lv_event_get_current_target(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_event_t* e = (lv_event_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_event_t, 0);
    return e ? lvb_ret_ptr(vm, (void*)lv_event_get_current_target(e), LVB_C_lv_obj_t) : mcs_null();
}
/* user data is any C# value, stored in the object's record */
static mcs_value_t lvo_lv_obj_set_user_data(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    void* obj = lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    if (!obj) return mcs_null();
    lvb_ret_ptr(vm, obj, LVB_C_lv_obj_t);
    lvb_objrec_t* r = obj_find(st, obj);
    if (!r) return mcs_null();
    if (r->ud >= 0) mcs_set_index(st->keep, (uint32_t)r->ud, argv[1]);
    else r->ud = keep(st, argv[1]);
    return mcs_null();
}
static mcs_value_t lvo_lv_obj_get_user_data(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    void* obj = lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    lvb_objrec_t* r = obj ? obj_find(st, obj) : NULL;
    return r ? kept(st, r->ud) : mcs_null();
}

/* ---- timers */
static void timer_tramp(lv_timer_t* t) {
    lvb_cb_t* c = (lvb_cb_t*)LVB_TIMER_UD(t);
    if (!c || c->magic != LVB_MAGIC) return;
    mcs_value_t w = new_wrapper(c->st->vm, t, LVB_C_lv_timer_t);
    mcs_push_root(c->st->vm, w);
    cb_call(c->st, c->fn, 1, &w);
    mcs_pop_root(c->st->vm, 1);
}
static lvb_cb_t* timer_rec(lvb_state_t* st, lv_timer_t* t) {
    for (lvb_cb_t* c = st->cbs; c; c = c->next) if (c->kind == CB_TIMER && c->handle == t) return c;
    return NULL;
}
static int timer_alive(lv_timer_t* t) {
    for (lv_timer_t* x = lv_timer_get_next(NULL); x; x = lv_timer_get_next(x)) if (x == t) return 1;
    return 0;
}
static mcs_value_t lvo_lv_timer_create(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    check_callable(vm, argv[0], 0);
    uint32_t period = (uint32_t)mcs_to_int(vm, argv[1]);
    if (mcs_has_exception(vm)) return mcs_null();
    REAP(st);
    /* timers whose repeat count ran out were freed by LVGL: forget them */
    for (lvb_cb_t* c = st->cbs, *nx; c; c = nx) { nx = c->next; if (c->kind == CB_TIMER && !timer_alive((lv_timer_t*)c->handle)) cb_zombify(st, c); }
    lvb_cb_t* c = cb_new(st, CB_TIMER, NULL);
    if (!c) return mcs_null();
    c->fn = keep(st, argv[0]);
    c->ud = keep(st, argv[2]);
    MCS_LVGL_LOCK();
    lv_timer_t* t = lv_timer_create(timer_tramp, period, c);
    MCS_LVGL_UNLOCK();
    if (!t) { cb_zombify(st, c); mcs_raise(vm, "OutOfMemoryException", "lv_timer_create"); return mcs_null(); }
    c->handle = t;
    return lvb_ret_ptr(vm, t, LVB_C_lv_timer_t);
}
static mcs_value_t lvo_lv_timer_delete(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    lvb_ud_t* u = ud_of(argv[0]);
    lv_timer_t* t = (lv_timer_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_timer_t, 0);
    if (!t) return mcs_null();
    lvb_cb_t* c = timer_rec(st, t);
    if (!timer_alive(t)) { if (c) cb_zombify(st, c); mcs_raise(vm, "ObjectDisposedException", "lv_timer_t was deleted"); return mcs_null(); }
    MCS_LVGL_LOCK();
    LVB_TIMER_DELETE(t);
    MCS_LVGL_UNLOCK();
    if (c) cb_zombify(st, c);
    if (u) u->p = NULL;
    return mcs_null();
}
static mcs_value_t lvo_lv_timer_set_cb(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    lv_timer_t* t = (lv_timer_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_timer_t, 0);
    check_callable(vm, argv[1], 0);
    if (mcs_has_exception(vm)) return mcs_null();
    lvb_cb_t* c = timer_rec(st, t);
    if (!c) { mcs_raise(vm, "InvalidOperationException", "lv_timer_set_cb: the timer was not created by lv_timer_create in C#"); return mcs_null(); }
    mcs_set_index(st->keep, (uint32_t)c->fn, argv[1]);
    return mcs_null();
}
static mcs_value_t lvo_lv_timer_get_user_data(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    lv_timer_t* t = (lv_timer_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_timer_t, 0);
    lvb_cb_t* c = t ? timer_rec(st, t) : NULL;
    return c ? kept(st, c->ud) : mcs_null();
}
static mcs_value_t lvo_lv_timer_set_user_data(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    lv_timer_t* t = (lv_timer_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_timer_t, 0);
    lvb_cb_t* c = t ? timer_rec(st, t) : NULL;
    if (c) mcs_set_index(st->keep, (uint32_t)c->ud, argv[1]);
    else if (t) mcs_raise(vm, "InvalidOperationException", "lv_timer_set_user_data: the timer was not created in C#");
    return mcs_null();
}

/* ---- lv_async_call(cb, user_data) */
static void async_tramp(void* p) {
    lvb_cb_t* c = (lvb_cb_t*)p;
    if (!c || c->magic != LVB_MAGIC) return;
    mcs_value_t a = kept(c->st, c->ud);
    cb_call(c->st, c->fn, 1, &a);
    if (c->magic == LVB_MAGIC) cb_zombify(c->st, c);
}
static mcs_value_t lvo_lv_async_call(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    check_callable(vm, argv[0], 0);
    if (mcs_has_exception(vm)) return mcs_null();
    lvb_cb_t* c = cb_new(st, CB_ASYNC, NULL);
    if (!c) return mcs_null();
    c->fn = keep(st, argv[0]); c->ud = keep(st, argv[1]);
    MCS_LVGL_LOCK();
    int res = (int)lv_async_call(async_tramp, c);
    MCS_LVGL_UNLOCK();
    return mcs_int(res);
}

/* ---- animations: the exec callback is any C# callable (var, value), including
 * LVGL functions like lv_obj_set_x; path callbacks are LVGL's lv_anim_path_* */
static lvb_cb_t* anim_rec(mcs_vm_t* vm, lv_anim_t* a) {
    lvb_state_t* st = ST(vm);
    lvb_cb_t* c = (lvb_cb_t*)a->user_data;
    if (c && c->magic == LVB_MAGIC && c->kind == CB_ANIM) return c;
    c = cb_new(st, CB_ANIM, a);
    if (c) a->user_data = c;
    return c;
}
static void anim_exec_tramp(lv_anim_t* a, int32_t v) {
    lvb_cb_t* c = (lvb_cb_t*)a->user_data;
    if (!c || c->magic != LVB_MAGIC) return;
    lvb_ud_t* vu = ud_of(kept(c->st, c->var));
    if (vu && vu->dying) return;       /* the animated object is being deleted */
    if (c->custom) {   /* lv_anim_set_custom_exec_cb: (anim, value) */
        mcs_value_t w = new_wrapper(c->st->vm, a, LVB_C_lv_anim_t);
        mcs_push_root(c->st->vm, w);
        mcs_value_t cargs[2] = { w, mcs_int(v) };
        cb_call(c->st, c->fn, 2, cargs);
        tmp_dispose(w);
        mcs_pop_root(c->st->vm, 1);
        return;
    }
    mcs_value_t args[2] = { kept(c->st, c->var), mcs_int(v) };
    cb_call(c->st, c->fn, 2, args);
}
static void anim_ready_tramp(lv_anim_t* a) {
    lvb_cb_t* c = (lvb_cb_t*)a->user_data;
    if (!c || c->magic != LVB_MAGIC || c->ready < 0) return;
    mcs_value_t w = new_wrapper(c->st->vm, a, LVB_C_lv_anim_t);
    mcs_push_root(c->st->vm, w);
    cb_call(c->st, c->ready, 1, &w);
    tmp_dispose(w);
    mcs_pop_root(c->st->vm, 1);
}
static void anim_start_tramp(lv_anim_t* a) {
    lvb_cb_t* c = (lvb_cb_t*)a->user_data;
    if (!c || c->magic != LVB_MAGIC || c->start < 0) return;
    mcs_value_t w = new_wrapper(c->st->vm, a, LVB_C_lv_anim_t);
    mcs_push_root(c->st->vm, w);
    cb_call(c->st, c->start, 1, &w);
    tmp_dispose(w);
    mcs_pop_root(c->st->vm, 1);
}
#define ANIM_ARG(i) lv_anim_t* a = (lv_anim_t*)lvb_arg_ptr(vm, argv[i], LVB_C_lv_anim_t, 0); if (!a) return mcs_null(); \
    lvb_state_t* st = ST(vm); lvb_cb_t* c = anim_rec(vm, a); if (!c) return mcs_null()
static void set_kept(lvb_state_t* st, int* slot, mcs_value_t v) {
    if (*slot >= 0) mcs_set_index(st->keep, (uint32_t)*slot, v); else *slot = keep(st, v);
}
static mcs_value_t lvo_lv_anim_set_var(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; ANIM_ARG(0);
    set_kept(st, &c->var, argv[1]);
    lv_anim_set_var(a, c);   /* our record is the var: lv_anim_delete(var) is mapped below */
    return mcs_null();
}
static mcs_value_t lvo_lv_anim_set_exec_cb(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; check_callable(vm, argv[1], 1); if (mcs_has_exception(vm)) return mcs_null(); ANIM_ARG(0);
    c->custom = 0;
    set_kept(st, &c->fn, argv[1]);
    if (!a->var) lv_anim_set_var(a, c);
    lv_anim_set_custom_exec_cb(a, mcs_is_null(argv[1]) ? NULL : anim_exec_tramp);
    return mcs_null();
}
static mcs_value_t lvo_lv_anim_set_ready_cb(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; check_callable(vm, argv[1], 1); if (mcs_has_exception(vm)) return mcs_null(); ANIM_ARG(0);
    set_kept(st, &c->ready, argv[1]);
    lv_anim_set_ready_cb(a, mcs_is_null(argv[1]) ? NULL : anim_ready_tramp);
    return mcs_null();
}
static mcs_value_t lvo_lv_anim_set_start_cb(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; check_callable(vm, argv[1], 1); if (mcs_has_exception(vm)) return mcs_null(); ANIM_ARG(0);
    set_kept(st, &c->start, argv[1]);
    lv_anim_set_start_cb(a, mcs_is_null(argv[1]) ? NULL : anim_start_tramp);
    return mcs_null();
}
static mcs_value_t lvo_lv_anim_set_path_cb(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_anim_t* a = (lv_anim_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_anim_t, 0);
    if (!a) return mcs_null();
    mcs_native_fn f = mcs_native_fn_of(argv[1]);
    for (int i = 0; lvb_anim_paths[i].w; i++)
        if (lvb_anim_paths[i].w == f) { lv_anim_set_path_cb(a, lvb_anim_paths[i].c); return mcs_null(); }
    mcs_raise(vm, "ArgumentException", "lv_anim_set_path_cb: pass one of LVGL's lv_anim_path_* functions");
    return mcs_null();
}
static mcs_value_t lvo_lv_anim_set_user_data(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; ANIM_ARG(0);
    set_kept(st, &c->ud, argv[1]);
    return mcs_null();
}
static mcs_value_t lvo_lv_anim_get_user_data(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_anim_t* a = (lv_anim_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_anim_t, 0);
    lvb_cb_t* c = a ? (lvb_cb_t*)a->user_data : NULL;
    return c && c->magic == LVB_MAGIC ? kept(c->st, c->ud) : mcs_null();
}
static mcs_value_t lvo_lv_anim_start(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_anim_t* a = (lv_anim_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_anim_t, 0);
    if (!a) return mcs_null();
    MCS_LVGL_LOCK();
    lv_anim_t* r = lv_anim_start(a);
    MCS_LVGL_UNLOCK();
    (void)r;
    return mcs_null();
}
/* lv_anim_delete(var, exec_cb): var is the C# value given to lv_anim_set_var */
static mcs_value_t lvo_lv_anim_delete(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lvb_state_t* st = ST(vm);
    int n = 0;
    for (lvb_cb_t* c = st->cbs; c; c = c->next) {
        if (c->kind != CB_ANIM) continue;
        mcs_value_t v = kept(st, c->var);
        if (!mcs_is_null(argv[0]) && !same_ptr(v, argv[0]) && !(v.type == argv[0].type && v.as.o == argv[0].as.o)) continue;
        if (!mcs_is_null(argv[1]) && !(kept(st, c->fn).as.o == argv[1].as.o)) continue;
        if (LVB_ANIM_DELETE(c, NULL)) n++;
    }
    return mcs_bool(n > 0);
}

/* ---- arrays LVGL keeps pointing to: copied into storage owned by the object */
static void* own_block(mcs_vm_t* vm, size_t size) {
    lvb_state_t* st = ST(vm);
    lvb_own_t* b = (lvb_own_t*)LVB_MALLOC(sizeof(lvb_own_t) + size + 8u);
    if (!b) { mcs_raise(vm, "OutOfMemoryException", "LVGL array"); return NULL; }
    memset(b, 0, sizeof(lvb_own_t) + size + 8u);
    b->size = size;
    b->next = st->owned; st->owned = b;   /* LVGL keeps the pointer: freed when the VM closes */
    return (void*)(((uintptr_t)(b + 1) + 7u) & ~(uintptr_t)7u);
}
#if LVGL_VERSION_MAJOR >= 9
typedef lv_point_precise_t lvb_line_point_t;
#define LVB_C_LINE_POINT LVB_C_lv_point_precise_t
#else
typedef lv_point_t lvb_line_point_t;
#define LVB_C_LINE_POINT LVB_C_lv_point_t
#endif
/* lv_line_set_points(line, points, count): points is lv_point_t[] / lv_point_precise_t[]
 * or an int[] of x,y pairs */
static mcs_value_t lvo_lv_line_set_points(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    mcs_int_t n = mcs_to_int(vm, argv[2]);
    if (mcs_has_exception(vm)) return mcs_null();
    int k = mcs_obj_kind(argv[1]);
    if (k != MCS_O_ARRAY && k != MCS_O_LIST) { mcs_raise(vm, "ArgumentException", "points: array expected"); return mcs_null(); }
    uint32_t len = mcs_len(argv[1]);
    int pairs = len && mcs_is_number(mcs_index(argv[1], 0));
    if (n < 0 || (uint32_t)n > (pairs ? len / 2u : len)) { mcs_raise(vm, "ArgumentOutOfRangeException", "point count"); return mcs_null(); }
    lvb_line_point_t* pts = (lvb_line_point_t*)own_block(vm, (size_t)(n ? n : 1) * sizeof *pts);
    if (!pts) return mcs_null();
    for (mcs_int_t i = 0; i < n; i++) {
        if (pairs) { pts[i].x = (int32_t)mcs_to_int(vm, mcs_index(argv[1], (uint32_t)(2 * i))); pts[i].y = (int32_t)mcs_to_int(vm, mcs_index(argv[1], (uint32_t)(2 * i + 1))); }
        else lvb_arg_struct(vm, mcs_index(argv[1], (uint32_t)i), LVB_C_LINE_POINT, &pts[i], sizeof pts[i]);
    }
    if (mcs_has_exception(vm)) return mcs_null();
    lv_line_set_points(obj, pts, (uint32_t)n);
    return mcs_null();
}
/* button-matrix / keyboard maps: string[] ending with "" (as in C); copied */
static const char** copy_map(mcs_vm_t* vm, mcs_value_t arr) {
    int k = mcs_obj_kind(arr);
    if (k != MCS_O_ARRAY && k != MCS_O_LIST) { mcs_raise(vm, "ArgumentException", "map: string[] expected (\"\\n\" starts a row, \"\" ends it)"); return NULL; }
    uint32_t n = mcs_len(arr);
    size_t bytes = (n + 2u) * sizeof(char*) + 1u;
    for (uint32_t i = 0; i < n; i++) bytes += strlen(mcs_to_cstr(vm, mcs_index(arr, i))) + 1u;
    if (mcs_has_exception(vm)) return NULL;
    char** m = (char**)own_block(vm, bytes);
    if (!m) return NULL;
    char* text = (char*)(m + n + 2u);
    uint32_t i = 0;
    for (; i < n; i++) { const char* s = mcs_cstr(mcs_index(arr, i)); size_t l = strlen(s); memcpy(text, s, l + 1u); m[i] = text; text += l + 1u; }
    if (!n || m[n - 1u][0]) { *text = 0; m[i++] = text; }   /* the terminating "" */
    m[i] = NULL;
    return (const char**)m;
}
#define CTRL_T_V9 lv_buttonmatrix_ctrl_t
static void* copy_ctrl(mcs_vm_t* vm, mcs_value_t arr, size_t elem) {
    if (mcs_is_null(arr)) return NULL;
    int k = mcs_obj_kind(arr);
    if (k != MCS_O_ARRAY && k != MCS_O_LIST) { mcs_raise(vm, "ArgumentException", "ctrl map: int[] expected"); return NULL; }
    uint32_t n = mcs_len(arr);
    uint8_t* m = (uint8_t*)own_block(vm, (n ? n : 1u) * elem);
    if (!m) return NULL;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = (uint32_t)mcs_to_int(vm, mcs_index(arr, i));
        if (elem == 1) m[i] = (uint8_t)v; else if (elem == 2) ((uint16_t*)m)[i] = (uint16_t)v; else ((uint32_t*)m)[i] = v;
    }
    return m;
}
#define MAP_SETTER(NAME, FN) \
static mcs_value_t lvo_##NAME(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) { \
    (void)self; (void)argc; \
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0); \
    if (!obj) return mcs_null(); \
    const char** m = copy_map(vm, argv[1]); \
    if (!m) return mcs_null(); \
    FN; return mcs_null(); }
#if LVGL_VERSION_MAJOR >= 9
#if LV_USE_BUTTONMATRIX
MAP_SETTER(lv_buttonmatrix_set_map, lv_buttonmatrix_set_map(obj, m))
#endif
#if LV_USE_KEYBOARD
/* lv_keyboard_set_map(kb, mode, map, ctrl_map) */
static mcs_value_t lvo_lv_keyboard_set_map(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    lv_keyboard_mode_t mode = (lv_keyboard_mode_t)mcs_to_int(vm, argv[1]);
    if (!obj || mcs_has_exception(vm)) return mcs_null();
    const char** m = copy_map(vm, argv[2]);
    if (!m) return mcs_null();
    const lv_buttonmatrix_ctrl_t* ctrl = (const lv_buttonmatrix_ctrl_t*)copy_ctrl(vm, argv[3], sizeof(lv_buttonmatrix_ctrl_t));
    if (mcs_has_exception(vm)) return mcs_null();
    lv_keyboard_set_map(obj, mode, m, ctrl);
    return mcs_null();
}
#endif
#else
#if LV_USE_BTNMATRIX
MAP_SETTER(lv_btnmatrix_set_map, lv_btnmatrix_set_map(obj, m))
#endif
#if LV_USE_KEYBOARD
static mcs_value_t lvo_lv_keyboard_set_map(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    lv_keyboard_mode_t mode = (lv_keyboard_mode_t)mcs_to_int(vm, argv[1]);
    if (!obj || mcs_has_exception(vm)) return mcs_null();
    const char** m = copy_map(vm, argv[2]);
    if (!m) return mcs_null();
    const lv_btnmatrix_ctrl_t* ctrl = (const lv_btnmatrix_ctrl_t*)copy_ctrl(vm, argv[3], sizeof(lv_btnmatrix_ctrl_t));
    if (mcs_has_exception(vm)) return mcs_null();
    lv_keyboard_set_map(obj, mode, m, ctrl);
    return mcs_null();
}
#endif
#endif
/* lv_buttonmatrix_set_ctrl_map(btnm, int[]) */
#if (LVGL_VERSION_MAJOR >= 9 && LV_USE_BUTTONMATRIX) || (LVGL_VERSION_MAJOR < 9 && LV_USE_BTNMATRIX)
#if LVGL_VERSION_MAJOR >= 9
typedef lv_buttonmatrix_ctrl_t lvb_btnm_ctrl_t;
#define LVB_BTNM_SET_CTRL lv_buttonmatrix_set_ctrl_map
#else
typedef lv_btnmatrix_ctrl_t lvb_btnm_ctrl_t;
#define LVB_BTNM_SET_CTRL lv_btnmatrix_set_ctrl_map
#endif
static mcs_value_t lvo_btnm_set_ctrl_map(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc;
    lv_obj_t* obj = (lv_obj_t*)lvb_arg_ptr(vm, argv[0], LVB_C_lv_obj_t, 0);
    if (!obj) return mcs_null();
    const lvb_btnm_ctrl_t* ctrl = (const lvb_btnm_ctrl_t*)copy_ctrl(vm, argv[1], sizeof(lvb_btnm_ctrl_t));
    if (!ctrl || mcs_has_exception(vm)) return mcs_null();
    LVB_BTNM_SET_CTRL(obj, ctrl);   /* copied by LVGL */
    return mcs_null();
}
#define LVB_HAS_BTNM 1
#endif

/* Like the generated wrappers, these expose APIs that LVGL 9.6 deprecates. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "mcs_lvgl_more.inc"   /* more hand-written wrappers: arrays, observers, tree walk, log, msg... */
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static const mcs_reg_t lvb_override_fns[] = {   /* sorted */
    MCS_FN("lv_anim_del", lvo_lv_anim_delete, 2),
    MCS_FN("lv_anim_delete", lvo_lv_anim_delete, 2),
    MCS_FN("lv_anim_get_user_data", lvo_lv_anim_get_user_data, 1),
    MCS_FN("lv_anim_set_exec_cb", lvo_lv_anim_set_exec_cb, 2),
    MCS_FN("lv_anim_set_path_cb", lvo_lv_anim_set_path_cb, 2),
    MCS_FN("lv_anim_set_ready_cb", lvo_lv_anim_set_ready_cb, 2),
    MCS_FN("lv_anim_set_start_cb", lvo_lv_anim_set_start_cb, 2),
    MCS_FN("lv_anim_set_user_data", lvo_lv_anim_set_user_data, 2),
    MCS_FN("lv_anim_set_var", lvo_lv_anim_set_var, 2),
    MCS_FN("lv_anim_start", lvo_lv_anim_start, 1),
    MCS_FN("lv_async_call", lvo_lv_async_call, 2),
#if LVGL_VERSION_MAJOR >= 9
#if LV_USE_BUTTONMATRIX
    MCS_FN("lv_btnmatrix_set_ctrl_map", lvo_btnm_set_ctrl_map, 2),
    MCS_FN("lv_btnmatrix_set_map", lvo_lv_buttonmatrix_set_map, 2),
    MCS_FN("lv_buttonmatrix_set_ctrl_map", lvo_btnm_set_ctrl_map, 2),
    MCS_FN("lv_buttonmatrix_set_map", lvo_lv_buttonmatrix_set_map, 2),
#endif
#else
#if LV_USE_BTNMATRIX
    MCS_FN("lv_btnmatrix_set_ctrl_map", lvo_btnm_set_ctrl_map, 2),
    MCS_FN("lv_btnmatrix_set_map", lvo_lv_btnmatrix_set_map, 2),
    MCS_FN("lv_buttonmatrix_set_ctrl_map", lvo_btnm_set_ctrl_map, 2),
    MCS_FN("lv_buttonmatrix_set_map", lvo_lv_btnmatrix_set_map, 2),
#endif
#endif
    MCS_FN("lv_event_get_current_target", lvo_lv_event_get_current_target, 1),
    MCS_FN("lv_event_get_target", lvo_lv_event_get_target, 1),
    MCS_FN("lv_event_get_user_data", lvo_lv_event_get_user_data, 1),
#if LV_USE_KEYBOARD
    MCS_FN("lv_keyboard_set_map", lvo_lv_keyboard_set_map, 4),
#endif
#if LV_USE_LINE
    MCS_FN("lv_line_set_points", lvo_lv_line_set_points, 3),
#endif
    MCS_FN("lv_obj_add_event_cb", lvo_lv_obj_add_event_cb, 4),
    MCS_FN("lv_obj_get_user_data", lvo_lv_obj_get_user_data, 1),
    MCS_FN("lv_obj_remove_event_cb", lvo_lv_obj_remove_event_cb, 2),
    MCS_FN("lv_obj_remove_event_cb_with_user_data", lvo_lv_obj_remove_event_cb_with_user_data, 3),
    MCS_FN("lv_obj_set_user_data", lvo_lv_obj_set_user_data, 2),
    MCS_FN("lv_timer_create", lvo_lv_timer_create, 3),
    MCS_FN("lv_timer_del", lvo_lv_timer_delete, 1),
    MCS_FN("lv_timer_delete", lvo_lv_timer_delete, 1),
    MCS_FN("lv_timer_get_user_data", lvo_lv_timer_get_user_data, 1),
    MCS_FN("lv_timer_set_cb", lvo_lv_timer_set_cb, 2),
    MCS_FN("lv_timer_set_user_data", lvo_lv_timer_set_user_data, 2),
    MCS_REG_END
};

/* ---------------------------------------------------------------- LVGL fs driver on the VFS */
#if MCS_ENABLE_FS
static mcs_vfs_t* g_fs_vfs;
static lv_fs_drv_t g_fs_drv;
static int g_fs_registered;
static void* fs_open(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
    (void)drv;
    mcs_vfs_file_t* f = (mcs_vfs_file_t*)LVB_MALLOC(sizeof *f);
    if (!f) return NULL;
    int fl = (mode & LV_FS_MODE_WR) ? ((mode & LV_FS_MODE_RD) ? (MCS_VFS_UPDATE | MCS_VFS_CREATE) : MCS_VFS_WRITE) : MCS_VFS_READ;
    if (mcs_vfs_open(g_fs_vfs, path, fl, f) != 0) { LVB_FREE(f); return NULL; }
    return f;
}
static lv_fs_res_t fs_close(lv_fs_drv_t* drv, void* fp) { (void)drv; mcs_vfs_close((mcs_vfs_file_t*)fp); LVB_FREE(fp); return LV_FS_RES_OK; }
static lv_fs_res_t fs_read(lv_fs_drv_t* drv, void* fp, void* buf, uint32_t btr, uint32_t* br) {
    (void)drv; int n = mcs_vfs_read((mcs_vfs_file_t*)fp, buf, btr);
    if (n < 0) { *br = 0; return LV_FS_RES_FS_ERR; }
    *br = (uint32_t)n; return LV_FS_RES_OK;
}
static lv_fs_res_t fs_write(lv_fs_drv_t* drv, void* fp, const void* buf, uint32_t btw, uint32_t* bw) {
    (void)drv; int n = mcs_vfs_write((mcs_vfs_file_t*)fp, buf, btw);
    if (n < 0) { *bw = 0; return LV_FS_RES_FS_ERR; }
    *bw = (uint32_t)n; return LV_FS_RES_OK;
}
static lv_fs_res_t fs_seek(lv_fs_drv_t* drv, void* fp, uint32_t pos, lv_fs_whence_t w) {
    (void)drv; int32_t at;
    int o = w == LV_FS_SEEK_CUR ? 1 : w == LV_FS_SEEK_END ? 2 : 0;
    return mcs_vfs_seek((mcs_vfs_file_t*)fp, (int32_t)pos, o, &at) == 0 ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}
static lv_fs_res_t fs_tell(lv_fs_drv_t* drv, void* fp, uint32_t* pos) {
    (void)drv; int32_t at = 0;
    if (mcs_vfs_seek((mcs_vfs_file_t*)fp, 0, 1, &at) != 0) return LV_FS_RES_FS_ERR;
    *pos = (uint32_t)at; return LV_FS_RES_OK;
}
typedef struct { char path[MCS_VFS_PATH_MAX]; uint32_t index; } fs_dir_t;
typedef struct { uint32_t want, at; char* out; uint32_t cap; int found; } fs_list_t;
static int fs_list_cb(void* ud, const char* name, const mcs_vfs_stat_t* st) {
    fs_list_t* l = (fs_list_t*)ud;
    if (l->at++ != l->want) return 0;
    snprintf(l->out, l->cap, "%s%s", st->is_dir ? "/" : "", name);   /* LVGL marks folders with '/' */
    l->found = 1;
    return 1;
}
static void* fs_dir_open(lv_fs_drv_t* drv, const char* path) {
    (void)drv; mcs_vfs_stat_t s;
    if (mcs_vfs_stat(g_fs_vfs, path, &s) != 0 || !s.is_dir) return NULL;
    fs_dir_t* d = (fs_dir_t*)LVB_MALLOC(sizeof *d);
    if (!d) return NULL;
    snprintf(d->path, sizeof d->path, "%s", path); d->index = 0;
    return d;
}
static lv_fs_res_t fs_dir_read_n(void* dp, char* fn, uint32_t cap) {
    fs_dir_t* d = (fs_dir_t*)dp;
    fs_list_t l = { d->index, 0, fn, cap, 0 };
    fn[0] = 0;
    if (mcs_vfs_list(g_fs_vfs, d->path, fs_list_cb, &l) < 0) return LV_FS_RES_FS_ERR;
    if (l.found) d->index++;
    return LV_FS_RES_OK;
}
#if LVGL_VERSION_MAJOR >= 9
static lv_fs_res_t fs_dir_read(lv_fs_drv_t* drv, void* dp, char* fn, uint32_t len) { (void)drv; return fs_dir_read_n(dp, fn, len); }
#else
static lv_fs_res_t fs_dir_read(lv_fs_drv_t* drv, void* dp, char* fn) { (void)drv; return fs_dir_read_n(dp, fn, 256); }
#endif
static lv_fs_res_t fs_dir_close(lv_fs_drv_t* drv, void* dp) { (void)drv; LVB_FREE(dp); return LV_FS_RES_OK; }
#endif

bool mcs_lvgl_fs_register(void* vfs, char letter) {
#if MCS_ENABLE_FS
    if (!vfs || !letter) return false;
    g_fs_vfs = (mcs_vfs_t*)vfs;
    if (g_fs_registered) return true;
    lv_fs_drv_init(&g_fs_drv);
    g_fs_drv.letter = letter;
    g_fs_drv.open_cb = fs_open; g_fs_drv.close_cb = fs_close; g_fs_drv.read_cb = fs_read; g_fs_drv.write_cb = fs_write;
    g_fs_drv.seek_cb = fs_seek; g_fs_drv.tell_cb = fs_tell;
    g_fs_drv.dir_open_cb = fs_dir_open; g_fs_drv.dir_read_cb = fs_dir_read; g_fs_drv.dir_close_cb = fs_dir_close;
    lv_fs_drv_register(&g_fs_drv);
    g_fs_registered = 1;
    return true;
#else
    (void)vfs; (void)letter;
    return false;
#endif
}

/* Image sources, font and file paths: "/x.png" means "M:/x.png" (the VFS drive) */
static mcs_value_t lvg_LV_FS_LETTER(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; (void)argv; char s[2] = { (char)MCS_LVGL_FS_LETTER, 0 }; return mcs_string(vm, s);
}
#include "mcs_lvgl_macros.inc"   /* LV_PCT(), LV_GRID_FR()... + MCS_LVGL_FS_LETTER: lvb_extra_fns */

/* ---------------------------------------------------------------- open / close */
static const mcs_rom_globals_t lvb_rom_override = {
    lvb_override_fns, (uint32_t)(sizeof lvb_override_fns / sizeof lvb_override_fns[0] - 1u), NULL, 0, NULL, 0 };
static const mcs_rom_globals_t lvb_rom_override2 = {
    lvb_override2_fns, (uint32_t)(sizeof lvb_override2_fns / sizeof lvb_override2_fns[0] - 1u), NULL, 0, NULL, 0 };
static const mcs_rom_globals_t lvb_rom_main = {
    lvb_rom_fns, (uint32_t)(sizeof lvb_rom_fns / sizeof lvb_rom_fns[0] - 1u),
    lvb_rom_consts, (uint32_t)(sizeof lvb_rom_consts / sizeof lvb_rom_consts[0] - 1u),
    lvb_rom_classes, (uint32_t)(sizeof lvb_rom_classes / sizeof lvb_rom_classes[0]) };
static const mcs_rom_globals_t lvb_rom_extra = {
    lvb_extra_fns, (uint32_t)(sizeof lvb_extra_fns / sizeof lvb_extra_fns[0] - 1u), NULL, 0, NULL, 0 };

#if MCS_LVGL_AUTO_HANDLER
static int lvb_idle(mcs_vm_t* vm, void* ud) {
    lvb_state_t* st = (lvb_state_t*)ud;
    MCS_LVGL_LOCK();
    lv_timer_handler();
    MCS_LVGL_UNLOCK();
    if (mcs_has_exception(vm)) return 1;
    return st->prev_idle ? st->prev_idle(vm, st->prev_idle_ud) : 0;
}
#endif

bool mcs_lvgl_open_lib(mcs_vm_t* vm) {
    if (!vm) return false;
    if (ST(vm)) return true;
    lvb_state_t* st = (lvb_state_t*)mcs_mem_realloc(vm, NULL, 0, sizeof *st);
    if (!st) return false;
    memset(st, 0, sizeof *st);
    st->vm = vm;
    st->keep = mcs_new_list(vm);
    st->keep_pin = mcs_pin(vm, st->keep);
    if (st->keep_pin < 0) { mcs_mem_realloc(vm, st, sizeof *st, 0); return false; }
    mcs_set_ext(vm, MCS_EXT_LVGL, st);
    /* the overrides come first: they replace generated entries of the same name */
    mcs_register_rom_globals(vm, &lvb_rom_override);
    mcs_register_rom_globals(vm, &lvb_rom_override2);
    mcs_register_rom_globals(vm, &lvb_rom_main);
    mcs_register_rom_globals(vm, &lvb_rom_extra);
    st->initial_screen = LVB_SCREEN_ACTIVE();
#if MCS_ENABLE_FS
    if (MCS_LVGL_FS_LETTER && mcs_get_ext(vm, MCS_EXT_VFS)) mcs_lvgl_fs_register(mcs_get_ext(vm, MCS_EXT_VFS), MCS_LVGL_FS_LETTER);
#endif
#if MCS_LVGL_AUTO_HANDLER
    st->prev_idle = mcs_get_idle(vm, &st->prev_idle_ud);
    mcs_set_idle(vm, lvb_idle, st);
#endif
    return true;
}

void mcs_lvgl_close_lib(mcs_vm_t* vm) {
    lvb_state_t* st = vm ? ST(vm) : NULL;
    if (!st) return;
    MCS_LVGL_LOCK();
    st->closing = 1;
    lvb_more_close_early(st);
    /* callbacks first: nothing may call into the VM any more */
    for (lvb_cb_t* c = st->cbs, *nx; c; c = nx) {
        nx = c->next;
        if (c->kind == CB_EVENT && c->target && lv_obj_is_valid((lv_obj_t*)c->target)) remove_one_event((lv_obj_t*)c->target, c);
        else if (c->kind == CB_TIMER && timer_alive((lv_timer_t*)c->handle)) LVB_TIMER_DELETE((lv_timer_t*)c->handle);
        else if (c->kind == CB_ANIM) LVB_ANIM_DELETE(c, NULL);
#if MCS_LV_VER >= 80300
        else if (c->kind == CB_ASYNC) lv_async_call_cancel(async_tramp, c);
#else
        else if (c->kind == CB_ASYNC) c->magic = LVB_DEAD;   /* cannot cancel: the dead record is kept for LVGL */
#endif
    }
#if MCS_LVGL_CLEANUP_ON_CLOSE
    if (st->initial_screen && lv_obj_is_valid(st->initial_screen) && LVB_SCREEN_ACTIVE() != st->initial_screen)
        LVB_SCREEN_LOAD(st->initial_screen);
#endif
    for (uint32_t i = 0; i < st->cap; i++) {
        lvb_objrec_t* r = &st->objs[i];
        if (!r->p || r->p == TOMB || r->dying) continue;
        lv_obj_t* o = (lv_obj_t*)r->p;
        if (!lv_obj_is_valid(o)) continue;
#if MCS_LV_VER >= 80100
        lv_obj_remove_event_cb_with_user_data(o, on_obj_delete, st);
#else
        while (lv_obj_remove_event_cb(o, on_obj_delete)) { }
#endif
    }
#if MCS_LVGL_CLEANUP_ON_CLOSE
    /* delete the screens / widgets the scripts created (parents delete their children) */
    for (uint32_t i = 0; i < st->cap; i++) {
        lvb_objrec_t* r = &st->objs[i];
        if (!r->p || r->p == TOMB || !r->created) continue;
        lv_obj_t* o = (lv_obj_t*)r->p;
        if (lv_obj_is_valid(o) && o != st->initial_screen) LVB_OBJ_DELETE(o);
    }
#endif
    MCS_LVGL_UNLOCK();
    for (uint32_t i = 0; i < st->cap; i++) {
        lvb_objrec_t* r = &st->objs[i];
        if (!r->p || r->p == TOMB) continue;
        lvb_ud_t* u = ud_of(kept(st, r->wrap));
        if (u) u->p = NULL;
        while (r->kept) { lvb_kept_t* k = r->kept; r->kept = k->next; mcs_mem_realloc(vm, k, sizeof *k, 0); }
    }
    while (st->cbs) { lvb_cb_t* c = st->cbs; st->cbs = c->next; if (c->magic != LVB_DEAD) LVB_FREE(c); }
    while (st->zombies) { lvb_cb_t* c = st->zombies; st->zombies = c->next; LVB_FREE(c); }
    lvb_more_close_late(st);
    while (st->owned) { lvb_own_t* o = st->owned; st->owned = o->next; LVB_FREE(o); }
#if MCS_LVGL_AUTO_HANDLER
    mcs_set_idle(vm, st->prev_idle, st->prev_idle_ud);
#endif
    if (st->objs) mcs_mem_realloc(vm, st->objs, st->cap * sizeof *st->objs, 0);
    if (st->free_idx) mcs_mem_realloc(vm, st->free_idx, st->capfree * sizeof(int), 0);
    mcs_unpin(vm, st->keep_pin);
    mcs_set_ext(vm, MCS_EXT_LVGL, NULL);
    mcs_mem_realloc(vm, st, sizeof *st, 0);
}

uint32_t mcs_lvgl_poll(mcs_vm_t* vm) {
    (void)vm;
    MCS_LVGL_LOCK();
    uint32_t r = lv_timer_handler();
    MCS_LVGL_UNLOCK();
    return r;
}
uint32_t mcs_lvgl_version(void) { return (uint32_t)MCS_LV_VER; }

#else  /* !MCS_ENABLE_LVGL */
#include "mcs_lvgl.h"
bool mcs_lvgl_open_lib(mcs_vm_t* vm) { (void)vm; return false; }
void mcs_lvgl_close_lib(mcs_vm_t* vm) { (void)vm; }
bool mcs_lvgl_fs_register(void* vfs, char letter) { (void)vfs; (void)letter; return false; }
uint32_t mcs_lvgl_poll(mcs_vm_t* vm) { (void)vm; return 0; }
uint32_t mcs_lvgl_version(void) { return 0; }
#endif
