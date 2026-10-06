/* MicroCS - memory management, objects, hash tables and garbage collector. */
#include "mcs_internal.h"
#include <stdlib.h>
#include <stdio.h>

/* ================================================================ memory */
void mcs_panic(mcs_vm_t* vm, mcs_result_t code, const char* msg) {
    snprintf(vm->error, sizeof vm->error, "%s", msg);
    if (vm->panic) longjmp(*vm->panic, (int)code);
    mcs_report_error(vm, "fatal: %s\n", msg);
    abort();
}

static void* raw_realloc(mcs_vm_t* vm, void* p, size_t old, size_t nsz) {
    if (vm->cfg.realloc_fn) return vm->cfg.realloc_fn(vm->cfg.alloc_ud, p, old, nsz);
    if (nsz == 0) { free(p); return NULL; }
    return realloc(p, nsz);
}

void* mcs_realloc(mcs_vm_t* vm, void* p, size_t old, size_t nsz) {
    /* The collector never runs from inside an allocation: native code may hold
     * unrooted objects in C locals. Allocation only *requests* a collection,
     * which the interpreter performs at its next safepoint (instruction
     * boundary: calls, backward jumps, allocating opcodes), where every live
     * value is on the VM stack or in a root. */
    if (nsz > old) {
        vm->bytes_allocated += nsz - old;
        if (vm->bytes_allocated > vm->next_gc) vm->gc_wanted = true;
        if (vm->bytes_allocated > vm->peak_bytes) vm->peak_bytes = vm->bytes_allocated;
#if MCS_GC_STRESS
        vm->gc_wanted = true; /* torture test: collect at every safepoint */
#endif
        if (vm->cfg.heap_limit && vm->bytes_allocated > vm->cfg.heap_limit) {
            vm->bytes_allocated -= nsz - old;
            char m[64]; snprintf(m, sizeof m, "out of memory (heap limit %u bytes)", (unsigned)vm->cfg.heap_limit);
            mcs_panic(vm, MCS_ERR_MEMORY, m);
        }
    } else {
        vm->bytes_allocated -= old - nsz;
    }
    void* r = raw_realloc(vm, p, old, nsz);
    if (r == NULL && nsz > 0) {
        vm->bytes_allocated -= nsz - old;
        char m[64]; snprintf(m, sizeof m, "out of memory (allocation of %u bytes failed)", (unsigned)nsz);
        mcs_panic(vm, MCS_ERR_MEMORY, m);
    }
    return r;
}

/* ============================================================== pool heap */
#if MCS_ENABLE_POOL_HEAP
typedef struct pool_blk { size_t size; struct pool_blk* next; } pool_blk_t;
#define POOL_ALIGN 8u
#define POOL_HDR ((sizeof(size_t) + POOL_ALIGN - 1) & ~(size_t)(POOL_ALIGN - 1))
#define POOL_ROUND(n) (((n) + POOL_ALIGN - 1) & ~(size_t)(POOL_ALIGN - 1))

void mcs_pool_init(mcs_pool_t* pool, void* buf, size_t size) {
    uintptr_t a = ((uintptr_t)buf + POOL_ALIGN - 1) & ~(uintptr_t)(POOL_ALIGN - 1);
    size -= (size_t)(a - (uintptr_t)buf);
    size &= ~(size_t)(POOL_ALIGN - 1);
    pool->base = (uint8_t*)a; pool->size = size; pool->used = 0; pool->peak = 0;
    pool_blk_t* b = (pool_blk_t*)a; b->size = size; b->next = NULL;
    pool->free_list = b;
}

static void pool_free(mcs_pool_t* pool, void* ptr) {
    pool_blk_t* b = (pool_blk_t*)((uint8_t*)ptr - POOL_HDR);
    pool->used -= b->size;
    /* insert sorted by address and coalesce */
    pool_blk_t** pp = (pool_blk_t**)&pool->free_list;
    while (*pp && *pp < b) pp = &(*pp)->next;
    b->next = *pp; *pp = b;
    if (b->next && (uint8_t*)b + b->size == (uint8_t*)b->next) { b->size += b->next->size; b->next = b->next->next; }
    if (pp != (pool_blk_t**)&pool->free_list) {
        pool_blk_t* prev = (pool_blk_t*)((uint8_t*)pp - offsetof(pool_blk_t, next));
        if ((uint8_t*)prev + prev->size == (uint8_t*)b) { prev->size += b->size; prev->next = b->next; }
    }
}

static void* pool_alloc(mcs_pool_t* pool, size_t n) {
    size_t need = POOL_ROUND(n + POOL_HDR);
    if (need < sizeof(pool_blk_t)) need = sizeof(pool_blk_t);
    pool_blk_t** pp = (pool_blk_t**)&pool->free_list;
    while (*pp) {
        pool_blk_t* b = *pp;
        if (b->size >= need) {
            if (b->size - need >= sizeof(pool_blk_t) + POOL_ALIGN) {
                pool_blk_t* rest = (pool_blk_t*)((uint8_t*)b + need);
                rest->size = b->size - need; rest->next = b->next;
                *pp = rest; b->size = need;
            } else *pp = b->next;
            pool->used += b->size;
            if (pool->used > pool->peak) pool->peak = pool->used;
            return (uint8_t*)b + POOL_HDR;
        }
        pp = &b->next;
    }
    return NULL;
}

void* mcs_pool_realloc(void* ud, void* ptr, size_t old, size_t nsz) {
    mcs_pool_t* pool = (mcs_pool_t*)ud;
    if (nsz == 0) { if (ptr) pool_free(pool, ptr); return NULL; }
    if (ptr) {
        pool_blk_t* b = (pool_blk_t*)((uint8_t*)ptr - POOL_HDR);
        if (b->size - POOL_HDR >= nsz) return ptr;
    }
    void* n = pool_alloc(pool, nsz);
    if (!n) return NULL;
    if (ptr) { memcpy(n, ptr, old < nsz ? old : nsz); pool_free(pool, ptr); }
    return n;
}
#endif

/* ================================================================ objects */
mcs_obj_t* mcs_alloc_obj(mcs_vm_t* vm, size_t size, uint8_t kind) {
    mcs_obj_t* o = (mcs_obj_t*)mcs_realloc(vm, NULL, 0, size);
    o->kind = kind; o->marked = 0;
    o->next = vm->objects; vm->objects = o;
    vm->object_count++;
    return o;
}

uint32_t mcs_hash_bytes(const char* s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h;
}

static mcs_string_t* alloc_string(mcs_vm_t* vm, const char* s, size_t len, uint32_t hash) {
    mcs_string_t* str = (mcs_string_t*)mcs_alloc_obj(vm, sizeof(mcs_string_t) + len, MCS_O_STRING);
    str->len = (uint32_t)len; str->hash = hash;
    if (len) memcpy(str->chars, s, len);
    str->chars[len] = 0;
    vm->gc_pause++;
    mcs_table_set(vm, &vm->strings, OBJ_VAL(str), mcs_null());
    vm->gc_pause--;
    return str;
}

mcs_string_t* mcs_intern(mcs_vm_t* vm, const char* s, size_t len) {
    uint32_t h = mcs_hash_bytes(s, len);
    mcs_string_t* f = mcs_table_find_string(&vm->strings, s, len, h);
    if (f) return f;
    return alloc_string(vm, s, len, h);
}
mcs_string_t* mcs_intern_c(mcs_vm_t* vm, const char* s) { return mcs_intern(vm, s, strlen(s)); }

mcs_string_t* mcs_take_buffer(mcs_vm_t* vm, char* buf, size_t len, size_t cap) {
    mcs_string_t* s = mcs_intern(vm, buf ? buf : "", len);
    if (buf) mcs_realloc(vm, buf, cap, 0);
    return s;
}

mcs_function_t* mcs_new_function(mcs_vm_t* vm) {
    mcs_function_t* f = (mcs_function_t*)mcs_alloc_obj(vm, sizeof(mcs_function_t), MCS_O_FUNCTION);
    memset((uint8_t*)f + sizeof(mcs_obj_t), 0, sizeof(*f) - sizeof(mcs_obj_t));
    return f;
}

mcs_closure_t* mcs_new_closure(mcs_vm_t* vm, mcs_function_t* fn) {
    size_t n = fn->upvalue_count;
    mcs_closure_t* c = (mcs_closure_t*)mcs_alloc_obj(vm, sizeof(mcs_closure_t) + sizeof(mcs_upvalue_t*) * (n ? n - 1 : 0), MCS_O_CLOSURE);
    c->fn = fn; c->upvalue_count = (uint32_t)n;
    for (size_t i = 0; i < n; i++) c->upvalues[i] = NULL;
    return c;
}

mcs_upvalue_t* mcs_new_upvalue(mcs_vm_t* vm, mcs_value_t* slot) {
    mcs_upvalue_t* u = (mcs_upvalue_t*)mcs_alloc_obj(vm, sizeof(mcs_upvalue_t), MCS_O_UPVALUE);
    u->location = slot; u->closed = mcs_null(); u->next_open = NULL;
    return u;
}

mcs_native_t* mcs_new_native(mcs_vm_t* vm, mcs_native_fn fn, int arity, mcs_string_t* name) {
    mcs_native_t* n = (mcs_native_t*)mcs_alloc_obj(vm, sizeof(mcs_native_t), MCS_O_NATIVE);
    n->fn = fn; n->arity = (int16_t)arity; n->name = name;
    return n;
}

mcs_class_t* mcs_new_class(mcs_vm_t* vm, mcs_string_t* name, uint8_t ckind) {
    mcs_class_t* c = (mcs_class_t*)mcs_alloc_obj(vm, sizeof(mcs_class_t), MCS_O_CLASS);
    c->ckind = ckind; c->field_count = 0; c->name = name; c->super = NULL;
    mcs_table_init(&c->methods); mcs_table_init(&c->getters); mcs_table_init(&c->setters);
    mcs_table_init(&c->statics); mcs_table_init(&c->fields); mcs_table_init(&c->ifaces);
    c->field_defaults = NULL; c->native_ctor = NULL; c->def = NULL; c->rom = NULL;
    return c;
}

void mcs_class_add_field(mcs_vm_t* vm, mcs_class_t* cls, mcs_string_t* name, mcs_value_t def) {
    mcs_value_t slot;
    if (mcs_table_get_s(&cls->fields, name, &slot)) { cls->field_defaults[slot.as.i] = def; return; }
    cls->field_defaults = MCS_GROW(vm, mcs_value_t, cls->field_defaults, cls->field_count, cls->field_count + 1);
    cls->field_defaults[cls->field_count] = def;
    mcs_table_set(vm, &cls->fields, OBJ_VAL(name), mcs_int(cls->field_count));
    cls->field_count++;
}

void mcs_class_inherit(mcs_vm_t* vm, mcs_class_t* cls, mcs_class_t* super) {
    cls->super = super;
    mcs_table_copy(vm, &super->methods, &cls->methods);
    mcs_table_copy(vm, &super->getters, &cls->getters);
    mcs_table_copy(vm, &super->setters, &cls->setters);
    mcs_table_copy(vm, &super->fields, &cls->fields);
    mcs_table_copy(vm, &super->ifaces, &cls->ifaces);
    if (super->field_count) {
        cls->field_defaults = MCS_GROW(vm, mcs_value_t, cls->field_defaults, cls->field_count, super->field_count);
        memcpy(cls->field_defaults, super->field_defaults, sizeof(mcs_value_t) * super->field_count);
        cls->field_count = super->field_count;
    }
    if (!cls->native_ctor) cls->native_ctor = super->native_ctor;
    if (!cls->def) cls->def = super->def;
}

static int fn_arity(mcs_value_t f, const uint8_t** pt) {
    *pt = NULL;
    if (IS_KIND(f, MCS_O_CLOSURE)) { *pt = AS_CLOSURE(f)->fn->param_types; return AS_CLOSURE(f)->fn->arity; }
    if (IS_KIND(f, MCS_O_NATIVE)) return AS_NATIVE(f)->arity;
    return -2;
}
static bool same_sig(mcs_value_t a, mcs_value_t b) {
    const uint8_t *pa, *pb;
    int na = fn_arity(a, &pa), nb = fn_arity(b, &pb);
    if (na != nb) return false;
    if (na > 0 && pa && pb) return memcmp(pa, pb, (size_t)na) == 0;
    return true;
}
static void ovl_push(mcs_vm_t* vm, mcs_overloads_t* o, mcs_value_t fn) {
    if (o->count == o->cap) {
        uint32_t nc = o->cap ? o->cap * 2 : 4;
        o->items = MCS_GROW(vm, mcs_value_t, o->items, o->cap, nc); o->cap = nc;
    }
    o->items[o->count++] = fn;
}

/* Adds a method. `first` = first declaration of this name in the class being
 * built: inherited methods with the same signature are overridden, other
 * inherited overloads are kept. Later same-name declarations form an
 * overload set resolved by argument count/types at call time. */
void mcs_class_add_method(mcs_vm_t* vm, mcs_table_t* t, mcs_string_t* name, mcs_value_t fn, bool first) {
    mcs_value_t old;
    if (!mcs_table_get_s(t, name, &old) || !IS_OBJ(old)) { mcs_table_set(vm, t, OBJ_VAL(name), fn); return; }
    if (!first && OBJ_KIND(old) == MCS_O_OVERLOADS) { ovl_push(vm, AS_OVL(old), fn); return; }
    if (OBJ_KIND(old) != MCS_O_OVERLOADS) {
        if (first && same_sig(old, fn)) { mcs_table_set(vm, t, OBJ_VAL(name), fn); return; }
        mcs_overloads_t* o = mcs_new_overloads(vm);
        ovl_push(vm, o, old); ovl_push(vm, o, fn);
        mcs_table_set(vm, t, OBJ_VAL(name), OBJ_VAL(o));
        return;
    }
    /* first && inherited overload set: copy, overriding matching signature */
    mcs_overloads_t* src = AS_OVL(old);
    mcs_overloads_t* o = mcs_new_overloads(vm);
    bool replaced = false;
    for (uint32_t i = 0; i < src->count; i++) {
        if (!replaced && same_sig(src->items[i], fn)) { ovl_push(vm, o, fn); replaced = true; }
        else ovl_push(vm, o, src->items[i]);
    }
    if (!replaced) ovl_push(vm, o, fn);
    mcs_table_set(vm, t, OBJ_VAL(name), OBJ_VAL(o));
}

/* ------------------------------------------------ lazy (ROM) members
 * Native members registered from `static const mcs_reg_t[]` tables are not
 * turned into heap objects at startup; they are looked up in the const table
 * on first use and cached in the class' hash tables. This keeps the RAM cost
 * of the standard library proportional to what a script actually uses. */
void mcs_class_add_rom(mcs_vm_t* vm, mcs_class_t* c, const mcs_reg_t* regs, bool statics) {
    mcs_rom_t* r = (mcs_rom_t*)mcs_realloc(vm, NULL, 0, sizeof(mcs_rom_t));
    r->regs = regs; r->next = NULL; r->statics = statics;
    mcs_rom_t** pp = &c->rom;
    while (*pp) pp = &(*pp)->next;
    *pp = r;
}
static mcs_table_t* cls_tab(mcs_class_t* c, int which) {
    switch (which) {
    case MCS_TAB_METHODS: return &c->methods;
    case MCS_TAB_STATICS: return &c->statics;
    case MCS_TAB_GETTERS: return &c->getters;
    default: return &c->setters;
    }
}
bool mcs_cls_get(mcs_vm_t* vm, mcs_class_t* c, int which, mcs_string_t* name, mcs_value_t* out) {
    mcs_table_t* t = cls_tab(c, which);
    if (mcs_table_get_s(t, name, out)) return true;
    bool any = false;
    mcs_value_t inh;
    vm->gc_pause++;
    if (which != MCS_TAB_STATICS && c->super && mcs_cls_get(vm, c->super, which, name, &inh)) {
        mcs_table_set(vm, t, OBJ_VAL(name), inh); /* copy-down */
        any = true;
    }
    char k = which == MCS_TAB_GETTERS ? 'g' : which == MCS_TAB_SETTERS ? 's' : 'f';
    for (mcs_rom_t* r = c->rom; r; r = r->next) {
        if (k == 'f' && r->statics != (which == MCS_TAB_STATICS)) continue;
        bool first = true;
        for (const mcs_reg_t* q = r->regs; q->name; q++) {
            if (q->kind != k || strcmp(q->name, name->chars) != 0) continue;
            mcs_value_t nf = OBJ_VAL(mcs_new_native(vm, q->fn, q->arity, name));
            if (k == 'f') mcs_class_add_method(vm, t, name, nf, first);
            else mcs_table_set(vm, t, OBJ_VAL(name), nf);
            first = false; any = true;
        }
    }
    vm->gc_pause--;
    return any && mcs_table_get_s(t, name, out);
}

mcs_instance_t* mcs_new_instance(mcs_vm_t* vm, mcs_class_t* cls) {
    size_t n = cls->field_count;
    mcs_instance_t* in = (mcs_instance_t*)mcs_alloc_obj(vm, sizeof(mcs_instance_t) + sizeof(mcs_value_t) * (n ? n - 1 : 0), MCS_O_INSTANCE);
    in->cls = cls;
    if (n) memcpy(in->fields, cls->field_defaults, sizeof(mcs_value_t) * n);
    return in;
}

mcs_userdata_t* mcs_new_userdata(mcs_vm_t* vm, mcs_class_t* cls, size_t size) {
    mcs_userdata_t* u = (mcs_userdata_t*)mcs_alloc_obj(vm, sizeof(mcs_userdata_t) + size, MCS_O_USERDATA);
    u->cls = cls; u->size = size;
    memset(u->data, 0, size);
    return u;
}

mcs_bound_t* mcs_new_bound(mcs_vm_t* vm, mcs_value_t recv, mcs_value_t method) {
    mcs_bound_t* b = (mcs_bound_t*)mcs_alloc_obj(vm, sizeof(mcs_bound_t), MCS_O_BOUND);
    b->receiver = recv; b->method = method;
    return b;
}

mcs_overloads_t* mcs_new_overloads(mcs_vm_t* vm) {
    mcs_overloads_t* o = (mcs_overloads_t*)mcs_alloc_obj(vm, sizeof(mcs_overloads_t), MCS_O_OVERLOADS);
    o->count = o->cap = 0; o->items = NULL;
    return o;
}

mcs_list_t* mcs_new_listobj(mcs_vm_t* vm, uint8_t kind, uint32_t count) {
    mcs_list_t* l = (mcs_list_t*)mcs_alloc_obj(vm, sizeof(mcs_list_t), kind);
    l->count = 0; l->cap = 0; l->items = NULL;
    if (count) {
        l->items = MCS_ALLOC(vm, mcs_value_t, count);
        l->cap = count;
        for (uint32_t i = 0; i < count; i++) l->items[i] = mcs_null();
        l->count = count;
    }
    return l;
}

void mcs_listobj_push(mcs_vm_t* vm, mcs_list_t* l, mcs_value_t v) {
    if (l->count == l->cap) {
        uint32_t nc = l->cap < 4 ? 4 : l->cap * 2;
        l->items = MCS_GROW(vm, mcs_value_t, l->items, l->cap, nc); l->cap = nc;
    }
    l->items[l->count++] = v;
}

void mcs_listobj_insert(mcs_vm_t* vm, mcs_list_t* l, uint32_t at, mcs_value_t v) {
    mcs_listobj_push(vm, l, v);
    memmove(&l->items[at + 1], &l->items[at], sizeof(mcs_value_t) * (l->count - 1 - at));
    l->items[at] = v;
}

void mcs_listobj_remove_at(mcs_list_t* l, uint32_t at) {
    memmove(&l->items[at], &l->items[at + 1], sizeof(mcs_value_t) * (l->count - at - 1));
    l->count--;
}

mcs_dict_t* mcs_new_dict(mcs_vm_t* vm) {
    mcs_dict_t* d = (mcs_dict_t*)mcs_alloc_obj(vm, sizeof(mcs_dict_t), MCS_O_DICT);
    mcs_table_init(&d->index); d->count = d->cap = 0; d->keys = d->vals = NULL;
    return d;
}

bool mcs_dict_get(mcs_dict_t* d, mcs_value_t key, mcs_value_t* out) {
    mcs_value_t idx;
    if (!mcs_table_get(&d->index, key, &idx)) return false;
    if (out) *out = d->vals[idx.as.i];
    return true;
}

void mcs_dict_set(mcs_vm_t* vm, mcs_dict_t* d, mcs_value_t key, mcs_value_t v) {
    mcs_value_t idx;
    if (mcs_table_get(&d->index, key, &idx)) { d->vals[idx.as.i] = v; return; }
    if (d->count == d->cap) {
        uint32_t nc = d->cap < 4 ? 4 : d->cap * 2;
        d->keys = MCS_GROW(vm, mcs_value_t, d->keys, d->cap, nc);
        d->vals = MCS_GROW(vm, mcs_value_t, d->vals, d->cap, nc);
        d->cap = nc;
    }
    d->keys[d->count] = key; d->vals[d->count] = v;
    mcs_table_set(vm, &d->index, key, mcs_int((mcs_int_t)d->count));
    d->count++;
}

bool mcs_dict_remove(mcs_vm_t* vm, mcs_dict_t* d, mcs_value_t key) {
    mcs_value_t idx;
    if (!mcs_table_get(&d->index, key, &idx)) return false;
    uint32_t i = (uint32_t)idx.as.i, last = d->count - 1;
    mcs_table_delete(&d->index, key);
    /* keep insertion order (matches .NET enumeration order for remove-only use) */
    for (uint32_t j = i; j < last; j++) {
        d->keys[j] = d->keys[j + 1]; d->vals[j] = d->vals[j + 1];
        mcs_table_set(vm, &d->index, d->keys[j], mcs_int((mcs_int_t)j));
    }
    d->count--;
    return true;
}

void mcs_dict_clear(mcs_vm_t* vm, mcs_dict_t* d) {
    mcs_table_free(vm, &d->index);
    d->count = 0;
}

/* ================================================================= tables */
#define TABLE_MAX_LOAD_NUM 3
#define TABLE_MAX_LOAD_DEN 4

void mcs_table_init(mcs_table_t* t) { t->count = 0; t->cap = 0; t->entries = NULL; }
void mcs_table_free(mcs_vm_t* vm, mcs_table_t* t) { MCS_FREE(vm, mcs_entry_t, t->entries, t->cap); mcs_table_init(t); }

static inline uint32_t mix32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

uint32_t mcs_value_hash(mcs_value_t v) {
    switch (v.type) {
    case MCS_T_BOOL: return v.as.b ? 0x9e3779b9u : 0x7f4a7c15u;
    case MCS_T_INT: case MCS_T_CHAR: return mix32((uint32_t)v.as.i ^ (uint32_t)((uint64_t)v.as.i >> 32) ^ v.type);
#if MCS_ENABLE_FLOAT
    case MCS_T_FLOAT: { mcs_float_t f = v.as.f; if (f == 0) f = 0; uint32_t h = 0; uint8_t b[sizeof f]; memcpy(b, &f, sizeof f);
        for (size_t i = 0; i < sizeof f; i++) { h = h * 31 + b[i]; }
        return mix32(h); }
#endif
    case MCS_T_OBJ:
        if (OBJ_KIND(v) == MCS_O_STRING) return AS_STRING(v)->hash;
        if (OBJ_KIND(v) == MCS_O_INSTANCE && AS_INSTANCE(v)->cls->ckind == CLS_TUPLE) { /* structural */
            mcs_instance_t* t = AS_INSTANCE(v); uint32_t h = 0x811c9dc5u;
            for (uint16_t i = 0; i < t->cls->field_count; i++) h = (h ^ mcs_value_hash(t->fields[i])) * 16777619u;
            return h;
        }
        return mix32((uint32_t)((uintptr_t)v.as.o >> 3));
    default: return 0;
    }
}

/* ValueTuple: element-wise comparison (same = identity per element, used for hashing) */
bool mcs_tuple_equal(mcs_value_t a, mcs_value_t b, bool same) {
    if (!IS_KIND(a, MCS_O_INSTANCE) || !IS_KIND(b, MCS_O_INSTANCE)) return false;
    mcs_instance_t *x = AS_INSTANCE(a), *y = AS_INSTANCE(b);
    if (x->cls->ckind != CLS_TUPLE || y->cls->ckind != CLS_TUPLE || x->cls->field_count != y->cls->field_count) return false;
    for (uint16_t i = 0; i < x->cls->field_count; i++)
        if (!(same ? mcs_values_same(x->fields[i], y->fields[i]) : mcs_values_equal(x->fields[i], y->fields[i]))) return false;
    return true;
}

bool mcs_values_same(mcs_value_t a, mcs_value_t b) {
    if (a.type != b.type) return false;
    switch (a.type) {
    case MCS_T_NULL: case MCS_T_UNDEF: return true;
    case MCS_T_BOOL: return a.as.b == b.as.b;
    case MCS_T_INT: case MCS_T_CHAR: return a.as.i == b.as.i;
#if MCS_ENABLE_FLOAT
    case MCS_T_FLOAT: return a.as.f == b.as.f;
#endif
    case MCS_T_OBJ: return a.as.o == b.as.o || (a.as.o->kind == MCS_O_INSTANCE && mcs_tuple_equal(a, b, true));
    }
    return false;
}

static mcs_entry_t* find_entry(mcs_entry_t* entries, uint32_t cap, mcs_value_t key) {
    uint32_t idx = mcs_value_hash(key) & (cap - 1);
    mcs_entry_t* tomb = NULL;
    for (;;) {
        mcs_entry_t* e = &entries[idx];
        if (e->key.type == MCS_T_NULL) {
            if (e->value.type == MCS_T_NULL) return tomb ? tomb : e;
            if (!tomb) tomb = e;
        } else if (mcs_values_same(e->key, key)) return e;
        idx = (idx + 1) & (cap - 1);
    }
}

static void adjust_cap(mcs_vm_t* vm, mcs_table_t* t, uint32_t cap) {
    mcs_entry_t* ne = MCS_ALLOC(vm, mcs_entry_t, cap);
    for (uint32_t i = 0; i < cap; i++) { ne[i].key = mcs_null(); ne[i].value = mcs_null(); }
    t->count = 0;
    for (uint32_t i = 0; i < t->cap; i++) {
        mcs_entry_t* e = &t->entries[i];
        if (e->key.type == MCS_T_NULL) continue;
        mcs_entry_t* d = find_entry(ne, cap, e->key);
        d->key = e->key; d->value = e->value; t->count++;
    }
    MCS_FREE(vm, mcs_entry_t, t->entries, t->cap);
    t->entries = ne; t->cap = cap;
}

static void shrink_strings(mcs_vm_t* vm, uint32_t nc) { adjust_cap(vm, &vm->strings, nc); }

bool mcs_table_get(const mcs_table_t* t, mcs_value_t key, mcs_value_t* out) {
    if (t->count == 0) return false;
    mcs_entry_t* e = find_entry(t->entries, t->cap, key);
    if (e->key.type == MCS_T_NULL) return false;
    if (out) *out = e->value;
    return true;
}

bool mcs_table_set(mcs_vm_t* vm, mcs_table_t* t, mcs_value_t key, mcs_value_t v) {
    if ((t->count + 1) * TABLE_MAX_LOAD_DEN > t->cap * TABLE_MAX_LOAD_NUM) {
        /* count includes tombstones: when at least half of the used slots are
         * tombstones (weak intern table after a GC, Dictionary.Remove churn)
         * rehash in place instead of doubling, so the table does not grow forever */
        uint32_t live = 0, nc = t->cap < 8 ? 8 : t->cap * 2;
        for (uint32_t i = 0; i < t->cap; i++) live += t->entries[i].key.type != MCS_T_NULL;
        if (t->cap >= 8 && live * 2 <= t->count) nc = t->cap;
        adjust_cap(vm, t, nc);
    }
    mcs_entry_t* e = find_entry(t->entries, t->cap, key);
    bool is_new = e->key.type == MCS_T_NULL;
    if (is_new && e->value.type == MCS_T_NULL) t->count++;
    e->key = key; e->value = v;
    return is_new;
}

bool mcs_table_delete(mcs_table_t* t, mcs_value_t key) {
    if (t->count == 0) return false;
    mcs_entry_t* e = find_entry(t->entries, t->cap, key);
    if (e->key.type == MCS_T_NULL) return false;
    e->key = mcs_null(); e->value = mcs_bool(true);
    return true;
}

void mcs_table_copy(mcs_vm_t* vm, const mcs_table_t* from, mcs_table_t* to) {
    for (uint32_t i = 0; i < from->cap; i++) {
        mcs_entry_t* e = &from->entries[i];
        if (e->key.type != MCS_T_NULL) mcs_table_set(vm, to, e->key, e->value);
    }
}

mcs_string_t* mcs_table_find_string(const mcs_table_t* t, const char* s, size_t len, uint32_t hash) {
    if (t->count == 0) return NULL;
    uint32_t idx = hash & (t->cap - 1);
    for (;;) {
        mcs_entry_t* e = &t->entries[idx];
        if (e->key.type == MCS_T_NULL) {
            if (e->value.type == MCS_T_NULL) return NULL;
        } else {
            mcs_string_t* k = AS_STRING(e->key);
            if (k->len == len && k->hash == hash && memcmp(k->chars, s, len) == 0) return k;
        }
        idx = (idx + 1) & (t->cap - 1);
    }
}

/* ===================================================================== GC */
static void mark_obj(mcs_vm_t* vm, mcs_obj_t* o) {
    if (!o || o->marked) return;
    o->marked = 1;
    if (vm->gray_count == vm->gray_cap) {
        uint32_t nc = vm->gray_cap < 32 ? 32 : vm->gray_cap * 2;
        mcs_obj_t** g = (mcs_obj_t**)raw_realloc(vm, vm->gray, sizeof(mcs_obj_t*) * vm->gray_cap, sizeof(mcs_obj_t*) * nc);
        if (!g) mcs_panic(vm, MCS_ERR_MEMORY, "out of memory (gc)");
        vm->gray = g; vm->gray_cap = nc;
    }
    vm->gray[vm->gray_count++] = o;
}
static inline void mark_val(mcs_vm_t* vm, mcs_value_t v) { if (IS_OBJ(v)) mark_obj(vm, v.as.o); }
static void mark_table(mcs_vm_t* vm, mcs_table_t* t) {
    for (uint32_t i = 0; i < t->cap; i++) { mark_val(vm, t->entries[i].key); mark_val(vm, t->entries[i].value); }
}

static void blacken(mcs_vm_t* vm, mcs_obj_t* o) {
    switch (o->kind) {
    case MCS_O_STRING: case MCS_O_NATIVE: break;
    case MCS_O_FUNCTION: {
        mcs_function_t* f = (mcs_function_t*)o;
        mark_obj(vm, (mcs_obj_t*)f->name); mark_obj(vm, (mcs_obj_t*)f->source);
        for (uint32_t i = 0; i < f->const_count; i++) mark_val(vm, f->consts[i]);
#if MCS_FIELD_CACHE
        /* keep cached classes alive so a freed class's address can never alias a new one */
        for (uint32_t i = 0; i < f->fcache_n; i++) mark_obj(vm, (mcs_obj_t*)f->fcache[i].cls);
#endif
        break;
    }
    case MCS_O_CLOSURE: {
        mcs_closure_t* c = (mcs_closure_t*)o;
        mark_obj(vm, (mcs_obj_t*)c->fn);
        for (uint32_t i = 0; i < c->upvalue_count; i++) mark_obj(vm, (mcs_obj_t*)c->upvalues[i]);
        break;
    }
    case MCS_O_UPVALUE: mark_val(vm, ((mcs_upvalue_t*)o)->closed); break;
    case MCS_O_CLASS: {
        mcs_class_t* c = (mcs_class_t*)o;
        mark_obj(vm, (mcs_obj_t*)c->name); mark_obj(vm, (mcs_obj_t*)c->super);
        mark_table(vm, &c->methods); mark_table(vm, &c->getters); mark_table(vm, &c->setters);
        mark_table(vm, &c->statics); mark_table(vm, &c->fields); mark_table(vm, &c->ifaces);
        for (uint32_t i = 0; i < c->field_count; i++) mark_val(vm, c->field_defaults[i]);
        break;
    }
    case MCS_O_INSTANCE: {
        mcs_instance_t* in = (mcs_instance_t*)o;
        mark_obj(vm, (mcs_obj_t*)in->cls);
        for (uint32_t i = 0; i < in->cls->field_count; i++) mark_val(vm, in->fields[i]);
        break;
    }
    case MCS_O_USERDATA: mark_obj(vm, (mcs_obj_t*)((mcs_userdata_t*)o)->cls); break;
    case MCS_O_BOUND: mark_val(vm, ((mcs_bound_t*)o)->receiver); mark_val(vm, ((mcs_bound_t*)o)->method); break;
    case MCS_O_OVERLOADS: { mcs_overloads_t* ov = (mcs_overloads_t*)o; for (uint32_t i = 0; i < ov->count; i++) mark_val(vm, ov->items[i]); break; }
    case MCS_O_ARRAY: case MCS_O_LIST: { mcs_list_t* l = (mcs_list_t*)o; for (uint32_t i = 0; i < l->count; i++) mark_val(vm, l->items[i]); break; }
    case MCS_O_DICT: {
        mcs_dict_t* d = (mcs_dict_t*)o;
        for (uint32_t i = 0; i < d->count; i++) { mark_val(vm, d->keys[i]); mark_val(vm, d->vals[i]); }
        break;
    }
    }
}

static void free_obj(mcs_vm_t* vm, mcs_obj_t* o) {
    vm->object_count--;
    switch (o->kind) {
    case MCS_O_STRING: mcs_realloc(vm, o, sizeof(mcs_string_t) + ((mcs_string_t*)o)->len, 0); break;
    case MCS_O_FUNCTION: {
        mcs_function_t* f = (mcs_function_t*)o;
        MCS_FREE(vm, uint8_t, f->code, f->code_cap);
        MCS_FREE(vm, mcs_value_t, f->consts, f->const_cap);
        MCS_FREE(vm, mcs_line_t, f->lines, f->line_cap);
        if (f->param_types) MCS_FREE(vm, uint8_t, f->param_types, f->arity);
#if MCS_FIELD_CACHE
        if (f->fcache) MCS_FREE(vm, mcs_fcache_t, f->fcache, f->fcache_n);
#endif
        mcs_realloc(vm, o, sizeof(mcs_function_t), 0);
        break;
    }
    case MCS_O_CLOSURE: {
        mcs_closure_t* c = (mcs_closure_t*)o;
        size_t n = c->upvalue_count;
        mcs_realloc(vm, o, sizeof(mcs_closure_t) + sizeof(mcs_upvalue_t*) * (n ? n - 1 : 0), 0);
        break;
    }
    case MCS_O_UPVALUE: mcs_realloc(vm, o, sizeof(mcs_upvalue_t), 0); break;
    case MCS_O_NATIVE: mcs_realloc(vm, o, sizeof(mcs_native_t), 0); break;
    case MCS_O_CLASS: {
        mcs_class_t* c = (mcs_class_t*)o;
        mcs_table_free(vm, &c->methods); mcs_table_free(vm, &c->getters); mcs_table_free(vm, &c->setters);
        for (mcs_rom_t* r = c->rom; r;) { mcs_rom_t* nx = r->next; mcs_realloc(vm, r, sizeof(mcs_rom_t), 0); r = nx; }
        mcs_table_free(vm, &c->statics); mcs_table_free(vm, &c->fields); mcs_table_free(vm, &c->ifaces);
        MCS_FREE(vm, mcs_value_t, c->field_defaults, c->field_count);
        mcs_realloc(vm, o, sizeof(mcs_class_t), 0);
        break;
    }
    case MCS_O_INSTANCE: {
        size_t n = ((mcs_instance_t*)o)->cls->field_count;
        mcs_realloc(vm, o, sizeof(mcs_instance_t) + sizeof(mcs_value_t) * (n ? n - 1 : 0), 0);
        break;
    }
    case MCS_O_USERDATA: {
        mcs_userdata_t* u = (mcs_userdata_t*)o;
        if (u->cls && u->cls->def && u->cls->def->finalizer) u->cls->def->finalizer(vm, u->data);
        mcs_realloc(vm, o, sizeof(mcs_userdata_t) + u->size, 0);
        break;
    }
    case MCS_O_BOUND: mcs_realloc(vm, o, sizeof(mcs_bound_t), 0); break;
    case MCS_O_OVERLOADS: {
        mcs_overloads_t* ov = (mcs_overloads_t*)o;
        MCS_FREE(vm, mcs_value_t, ov->items, ov->cap);
        mcs_realloc(vm, o, sizeof(mcs_overloads_t), 0);
        break;
    }
    case MCS_O_ARRAY: case MCS_O_LIST: {
        mcs_list_t* l = (mcs_list_t*)o;
        MCS_FREE(vm, mcs_value_t, l->items, l->cap);
        mcs_realloc(vm, o, sizeof(mcs_list_t), 0);
        break;
    }
    case MCS_O_DICT: {
        mcs_dict_t* d = (mcs_dict_t*)o;
        mcs_table_free(vm, &d->index);
        MCS_FREE(vm, mcs_value_t, d->keys, d->cap);
        MCS_FREE(vm, mcs_value_t, d->vals, d->cap);
        mcs_realloc(vm, o, sizeof(mcs_dict_t), 0);
        break;
    }
    }
}

static void mark_roots(mcs_vm_t* vm) {
    for (mcs_value_t* v = vm->stack; v < vm->sp; v++) mark_val(vm, *v);
    for (int i = 0; i < vm->frame_count; i++) mark_obj(vm, (mcs_obj_t*)vm->frames[i].closure);
    for (mcs_upvalue_t* u = vm->open_upvalues; u; u = u->next_open) mark_obj(vm, (mcs_obj_t*)u);
    for (uint32_t i = 0; i < vm->global_count; i++) { mark_val(vm, vm->globals[i]); mark_obj(vm, (mcs_obj_t*)vm->global_names[i]); }
    mark_table(vm, &vm->global_index);
    mark_table(vm, &vm->tuple_classes);
    for (int i = 0; i < vm->root_count; i++) mark_val(vm, vm->roots[i]);
    for (int i = 0; i < MCS_MAX_PINS; i++) mark_val(vm, vm->pins[i]);
    mark_val(vm, vm->exc_value);
    mark_val(vm, vm->order_last); mark_val(vm, vm->order_spec);
    mark_obj(vm, (mcs_obj_t*)vm->compiling);
    mcs_class_t** cls = &vm->cls_object;
    for (mcs_class_t** c = cls; c <= &vm->cls_delegate; c++) mark_obj(vm, (mcs_obj_t*)*c);
    for (int i = 0; i < EXC__COUNT; i++) mark_obj(vm, (mcs_obj_t*)vm->exc[i]);
    mcs_string_t** s = &vm->s_ctor;
    for (mcs_string_t** p = s; p <= &vm->s_equals; p++) mark_obj(vm, (mcs_obj_t*)*p);
}

void mcs_collect(mcs_vm_t* vm) {
    if (vm->gc_pause) return;
    vm->gc_pause++;
    mark_roots(vm);
    while (vm->gray_count) blacken(vm, vm->gray[--vm->gray_count]);
    /* weak intern table */
    uint32_t live = 0;
    for (uint32_t i = 0; i < vm->strings.cap; i++) {
        mcs_entry_t* e = &vm->strings.entries[i];
        if (e->key.type == MCS_T_OBJ && !e->key.as.o->marked) { e->key = mcs_null(); e->value = mcs_bool(true); }
        else if (e->key.type == MCS_T_OBJ) live++;
    }
    /* compact a mostly-dead intern table (one burst of temporary strings must
     * not pin a large table for the rest of the run) */
    if (vm->strings.cap > 64 && live * 8 < vm->strings.cap) {
        uint32_t nc = 64;
        while ((live + 1) * TABLE_MAX_LOAD_DEN * 2 > nc * TABLE_MAX_LOAD_NUM) nc *= 2;
        if (vm->cfg.heap_limit == 0 || vm->bytes_allocated + nc * sizeof(mcs_entry_t) <= vm->cfg.heap_limit) shrink_strings(vm, nc);
    }
    mcs_obj_t** pp = &vm->objects;
    while (*pp) {
        mcs_obj_t* o = *pp;
        if (o->marked) { o->marked = 0; pp = &o->next; }
        else { *pp = o->next; free_obj(vm, o); }
    }
    vm->next_gc = vm->bytes_allocated * MCS_GC_GROW;
    if (vm->next_gc < MCS_GC_INITIAL) vm->next_gc = MCS_GC_INITIAL;
    /* collect early enough that the hard limit is not hit between safepoints */
    if (vm->cfg.heap_limit && vm->next_gc > vm->cfg.heap_limit - vm->cfg.heap_limit / 4) vm->next_gc = vm->cfg.heap_limit - vm->cfg.heap_limit / 4;
    vm->gc_wanted = false;
    vm->gc_count++;
    vm->gc_pause--;
}

void mcs_maybe_gc(mcs_vm_t* vm) { if (vm->gc_wanted && !vm->gc_pause) mcs_collect(vm); }

void mcs_free_objects(mcs_vm_t* vm) {
    mcs_obj_t* o = vm->objects;
    while (o) { mcs_obj_t* n = o->next; free_obj(vm, o); o = n; }
    vm->objects = NULL;
    raw_realloc(vm, vm->gray, sizeof(mcs_obj_t*) * vm->gray_cap, 0);
    vm->gray = NULL; vm->gray_cap = 0;
}
