/* MicroCS - registration API and core standard library:
 * Object, Type, exceptions, primitive types, Console, Convert, Math,
 * Environment, Thread, GC, Random, Stopwatch, Debug, Delegate.          */
#include "mcs_lib.h"
#if (!MCS_ENABLE_CONVERT || !MCS_ENABLE_DIAGNOSTICS) && defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#if MCS_ENABLE_FLOAT
#include <math.h>
#endif

/* ============================================================ registration */
mcs_class_t* mcs_define_builtin_class(mcs_vm_t* vm, const char* name, mcs_class_t* super, uint8_t ckind) {
    vm->gc_pause++;
    mcs_class_t* c = mcs_new_class(vm, mcs_intern_c(vm, name), ckind);
    if (super) mcs_class_inherit_lazy(vm, c, super);
    /* a class created on demand never replaces a global the script already
     * defined with the same name (e.g. its own `class Stack`) */
    { uint32_t gs_ = mcs_global_slot(vm, c->name); if (vm->globals[gs_].type == MCS_T_UNDEF) vm->globals[gs_] = OBJ_VAL(c); }
    vm->gc_pause--;
    return c;
}

void mcs_add_regs(mcs_vm_t* vm, mcs_class_t* cls, const mcs_reg_t* regs, bool statics) {
    if (!regs) return;
#if MCS_LAZY_REGS
    vm->gc_pause++;
    mcs_class_add_rom(vm, cls, regs, statics);
    vm->gc_pause--;
#else
    mcs_add_regs_eager(vm, cls, regs, statics);
#endif
}

void mcs_add_regs_eager(mcs_vm_t* vm, mcs_class_t* cls, const mcs_reg_t* regs, bool statics) {
    if (!regs) return;
    vm->gc_pause++;
    for (const mcs_reg_t* r = regs; r->name; r++) {
        mcs_string_t* nm = mcs_intern_c(vm, r->name);
        mcs_value_t nf = OBJ_VAL(mcs_new_native(vm, r->fn, r->arity, nm));
        if (r->kind == 'g') { mcs_table_set(vm, &cls->getters, OBJ_VAL(nm), nf); continue; }
        if (r->kind == 's') { mcs_table_set(vm, &cls->setters, OBJ_VAL(nm), nf); continue; }
        /* first occurrence of a name in this table overrides inherited entries
         * with the same signature; repeated names form overload sets */
        bool first = true;
        for (const mcs_reg_t* q = regs; q < r; q++)
            if (q->kind == 'f' && !strcmp(q->name, r->name)) { first = false; break; }
        mcs_class_add_method(vm, statics ? &cls->statics : &cls->methods, nm, nf, first);
    }
    vm->gc_pause--;
}

mcs_class_t* lib_global_class(mcs_vm_t* vm, const char* name) {
    mcs_value_t g = mcs_get_global(vm, name);
    return IS_KIND(g, MCS_O_CLASS) ? AS_CLASS(g) : NULL;
}

/* the class bound to `name` right now (does not create pending classes) */
static mcs_class_t* peek_global_class(mcs_vm_t* vm, const char* name) {
    mcs_string_t* s = mcs_find_interned(vm, name, strlen(name));
    if (!s || !s->obj.aux) return NULL;
    mcs_value_t g = vm->globals[s->obj.aux - 1u];
    return IS_KIND(g, MCS_O_CLASS) ? AS_CLASS(g) : NULL;
}

static mcs_class_t* module_for(mcs_vm_t* vm, const char* name) {
    mcs_class_t* c = peek_global_class(vm, name);
    if (!c) c = mcs_define_builtin_class(vm, name, NULL, CLS_STATIC);
    return c;
}

static void add_consts(mcs_vm_t* vm, mcs_class_t* c, const mcs_const_t* k) {
    vm->gc_pause++;
    for (; k && k->name; k++) mcs_table_set(vm, &c->statics, lib_cstr(vm, k->name), mcs_int(k->value));
    vm->gc_pause--;
}
static void define_native_class(mcs_vm_t* vm, const mcs_class_def_t* def) {
    mcs_class_t* c = mcs_define_builtin_class(vm, def->name, MCS_CLS(vm, object), CLS_USERDATA);
    c->def = def;
    mcs_add_regs(vm, c, def->members, false);
    mcs_add_regs(vm, c, def->statics, true);
}

#if MCS_LAZY_CLASSES
/* name already bound to a value (script or C) or to a pending registration */
static bool global_defined(mcs_vm_t* vm, const char* name) {
    mcs_string_t* s = mcs_find_interned(vm, name, strlen(name));
    return s && s->obj.aux && vm->globals[s->obj.aux - 1u].type != MCS_T_UNDEF;
}
/* queue a registration; it is applied when the name is first resolved */
static bool lazy_add(mcs_vm_t* vm, const char* name, const mcs_reg_t* regs, const mcs_class_def_t* def, const mcs_const_t* k) {
    if (global_defined(vm, name)) return false;   /* exists: extend it now */
    size_t n = strlen(name);
    mcs_lazy_t* z = (mcs_lazy_t*)mcs_realloc(vm, NULL, 0, sizeof(mcs_lazy_t) + n);
    z->next = NULL; z->regs = regs; z->def = def; z->consts = k;
    memcpy(z->name, name, n + 1);
    mcs_lazy_t** pp = &vm->lazy;            /* append: keeps registration order */
    while (*pp) pp = &(*pp)->next;
    *pp = z;
    return true;
}
#endif

void mcs_register_module(mcs_vm_t* vm, const char* name, const mcs_reg_t* fns) {
#if MCS_LAZY_CLASSES && MCS_LAZY_REGS
    if (lazy_add(vm, name, fns, NULL, NULL)) return;
#endif
    mcs_add_regs(vm, module_for(vm, name), fns, true);
}

void mcs_register_consts(mcs_vm_t* vm, const char* name, const mcs_const_t* consts) {
#if MCS_LAZY_CLASSES
    if (lazy_add(vm, name, NULL, NULL, consts)) return;
#endif
    add_consts(vm, module_for(vm, name), consts);
}

void mcs_module_set(mcs_vm_t* vm, const char* module, const char* name, mcs_value_t v) {
    vm->gc_pause++;
#if MCS_LAZY_CLASSES
    if (!peek_global_class(vm, module)) mcs_lazy_resolve(vm, module, strlen(module));   /* apply pending registrations first */
#endif
    mcs_class_t* c = module_for(vm, module);
    mcs_table_set(vm, &c->statics, OBJ_VAL(mcs_intern_c(vm, name)), v);
    vm->gc_pause--;
}

void mcs_register_class(mcs_vm_t* vm, const mcs_class_def_t* def) {
#if MCS_LAZY_CLASSES && MCS_LAZY_REGS
    if (lazy_add(vm, def->name, NULL, def, NULL)) return;
#endif
    define_native_class(vm, def);
}

void mcs_register_function(mcs_vm_t* vm, const char* name, mcs_native_fn fn, int arity) {
    vm->gc_pause++;
    mcs_string_t* nm = mcs_intern_c(vm, name);
    { uint32_t gs_ = mcs_global_slot(vm, nm); vm->globals[gs_] = OBJ_VAL(mcs_new_native(vm, fn, arity, nm)); }
    vm->gc_pause--;
}

/* ============================================================ helpers */
static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

bool lib_parse_int(const char* s, size_t n, int base, mcs_int_t* out, bool* overflow) {
    size_t i = 0;
    *overflow = false;
    while (i < n && is_ws(s[i])) i++;
    while (n > i && is_ws(s[n - 1])) n--;
    bool neg = false;
    if (i < n && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
    if (base == 16 && i + 1 < n && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) i += 2;
    if (i >= n) return false;
    mcs_uint_t v = 0, limit = neg ? (mcs_uint_t)1 << (sizeof(mcs_int_t) * 8 - 1) : ((mcs_uint_t)1 << (sizeof(mcs_int_t) * 8 - 1)) - 1;
    if (base != 10) limit = ~(mcs_uint_t)0;
    for (; i < n; i++) {
        int d;
        char c = s[i];
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        if (d >= base) return false;
        if (v > (limit - (mcs_uint_t)d) / (mcs_uint_t)base) { *overflow = true; return false; }
        v = v * (mcs_uint_t)base + (mcs_uint_t)d;
    }
    *out = neg ? (mcs_int_t)(0 - v) : (mcs_int_t)v;
    return true;
}

#if MCS_ENABLE_FLOAT
bool lib_parse_float(const char* s, size_t n, mcs_float_t* out) {
    char tmp[64];
    while (n && is_ws(*s)) { s++; n--; }
    while (n && is_ws(s[n - 1])) n--;
    if (!n || n >= sizeof tmp) return false;
    memcpy(tmp, s, n); tmp[n] = 0;
    if (!strcmp(tmp, "NaN")) { *out = (mcs_float_t)NAN; return true; }
    if (!strcmp(tmp, "Infinity") || !strcmp(tmp, "\xE2\x88\x9E")) { *out = (mcs_float_t)INFINITY; return true; }
    if (!strcmp(tmp, "-Infinity") || !strcmp(tmp, "-\xE2\x88\x9E")) { *out = -(mcs_float_t)INFINITY; return true; }
    for (size_t i = 0; i < n; i++) {
        char c = tmp[i];
        if (!((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')) return false;
    }
    char* end;
    double d = strtod(tmp, &end);
    if (*end) return false;
    *out = (mcs_float_t)d;
    return true;
}
#endif

mcs_string_t* lib_need_str(mcs_vm_t* vm, mcs_value_t v, const char* param) {
    if (IS_STRING(v)) return AS_STRING(v);
    if (v.type == MCS_T_NULL) mcs_throw(vm, EXC_ARGNULL, "Value cannot be null. (Parameter '%s')", param);
    else mcs_throw(vm, EXC_ARGUMENT, "Argument '%s' must be a string, got %s", param, mcs_type_name(vm, v));
    return NULL;
}

bool lib_equals(mcs_vm_t* vm, mcs_value_t a, mcs_value_t b) {
    if (IS_KIND(a, MCS_O_INSTANCE) && !mcs_values_same(a, b)) {
        mcs_value_t m, r;
        if (mcs_table_get_s(&AS_INSTANCE(a)->cls->methods, vm->s_equals, &m) && !IS_KIND(m, MCS_O_NATIVE)) {
            if (mcs_call_internal(vm, m, a, 1, &b, &r) != MCS_OK) return false;
            return mcs_truthy(r);
        }
    }
    return mcs_values_equal(a, b);
}

uint32_t lib_ticks(mcs_vm_t* vm) { return mcs_ticks(vm); }

static mcs_value_t fmt_value(mcs_vm_t* vm, int argc, mcs_value_t* argv) {
    mcs_buf_t b; mcs_buf_init(&b, vm);
    bool ok;
    if (argc >= 2 && IS_STRING(argv[0])) {
        int n = argc - 1; mcs_value_t* args = argv + 1;
        if (n == 1 && IS_KIND(args[0], MCS_O_ARRAY)) { n = (int)AS_LIST(args[0])->count; args = AS_LIST(args[0])->items; }
        ok = mcs_format_string(vm, &b, AS_CSTR(argv[0]), AS_STRING(argv[0])->len, n, args);
    } else ok = argc ? mcs_value_to_buf(vm, &b, argv[0]) : true;
    if (!ok) { mcs_buf_free(&b); return mcs_null(); }
    return OBJ_VAL(mcs_buf_to_string(&b));
}

/* ============================================================ Object / Type */
static mcs_value_t make_type(mcs_vm_t* vm, const char* name) {
    mcs_class_t* tc = lib_global_class(vm, "Type");
    if (!tc) return lib_cstr(vm, name);
    vm->gc_pause++;
    mcs_instance_t* t = mcs_new_instance(vm, tc);
    char full[96];
    snprintf(full, sizeof full, "System.%s", name);
    t->fields[0] = lib_cstr(vm, name);
    t->fields[1] = lib_cstr(vm, full);
    vm->gc_pause--;
    return OBJ_VAL(t);
}

NATIVE(obj_tostring) {
    if (IS_KIND(self, MCS_O_INSTANCE) || IS_KIND(self, MCS_O_USERDATA)) return OBJ_VAL(mcs_class_of(vm, self)->name);
    mcs_string_t* s = mcs_value_to_string(vm, self);
    return s ? OBJ_VAL(s) : mcs_null();
}
NATIVE(obj_tostring_fmt) {
    if (!IS_STRING(argv[0])) return obj_tostring(vm, self, 0, argv);
    mcs_buf_t b; mcs_buf_init(&b, vm);
    if (!mcs_format_spec(vm, &b, self, AS_CSTR(argv[0]), AS_STRING(argv[0])->len)) { mcs_buf_free(&b); return mcs_null(); }
    return OBJ_VAL(mcs_buf_to_string(&b));
}
NATIVE(obj_equals) { return mcs_bool(mcs_values_equal(self, argv[0])); }
NATIVE(obj_hash) { return mcs_int((mcs_int_t)(mcs_value_hash(self) & 0x7FFFFFFF)); }
NATIVE(obj_gettype) { return make_type(vm, mcs_type_name(vm, self)); }
NATIVE(obj_refeq) { return mcs_bool(mcs_values_same(argv[0], argv[1])); }
NATIVE(obj_eq_static) { return mcs_bool(lib_equals(vm, argv[0], argv[1])); }

static const mcs_reg_t object_methods[] = {
    MCS_FN("ToString", obj_tostring, 0), MCS_FN("ToString", obj_tostring_fmt, 1),
    MCS_FN("Equals", obj_equals, 1), MCS_FN("GetHashCode", obj_hash, 0), MCS_FN("GetType", obj_gettype, 0),
    MCS_REG_END
};
static const mcs_reg_t object_statics[] = {
    MCS_FN("ReferenceEquals", obj_refeq, 2), MCS_FN("Equals", obj_eq_static, 2), MCS_REG_END
};

static bool type_name_eq(mcs_value_t a, mcs_value_t b) {
    if (IS_KIND(a, MCS_O_INSTANCE) && IS_KIND(b, MCS_O_INSTANCE)) return mcs_values_same(AS_INSTANCE(a)->fields[1], AS_INSTANCE(b)->fields[1]);
    return mcs_values_same(a, b);
}
NATIVE(type_tostring) { return IS_KIND(self, MCS_O_INSTANCE) ? AS_INSTANCE(self)->fields[1] : mcs_null(); }
NATIVE(type_equals) { return mcs_bool(type_name_eq(self, argv[0])); }
NATIVE(type_op_eq) { return mcs_bool(type_name_eq(argv[0], argv[1])); }
NATIVE(type_op_ne) { return mcs_bool(!type_name_eq(argv[0], argv[1])); }
static const mcs_reg_t type_methods[] = { MCS_FN("ToString", type_tostring, 0), MCS_FN("Equals", type_equals, 1), MCS_REG_END };
static const mcs_reg_t type_statics[] = { MCS_FN("op_Equality", type_op_eq, 2), MCS_FN("op_Inequality", type_op_ne, 2), MCS_REG_END };

/* ============================================================ exceptions */
static bool is_builtin_exc(mcs_vm_t* vm, mcs_class_t* c) {
    for (int i = 0; i < EXC__COUNT; i++) if (vm->exc[i] == c) return true;
    return c->name && c->def == NULL && c->ckind == CLS_BUILTIN;
}
static bool derives(mcs_class_t* c, mcs_class_t* base) { for (; c; c = c->super) if (c == base) return true; return false; }
static void set_named(mcs_vm_t* vm, mcs_instance_t* in, const char* name, mcs_value_t v) {
    mcs_value_t slot;
    if (mcs_table_get_s(&in->cls->fields, mcs_intern_c(vm, name), &slot)) in->fields[slot.as.i] = v;
}
static mcs_value_t get_named(mcs_vm_t* vm, mcs_instance_t* in, const char* name) {
    mcs_value_t slot;
    if (mcs_table_get_s(&in->cls->fields, mcs_intern_c(vm, name), &slot)) return in->fields[slot.as.i];
    return mcs_null();
}

NATIVE(exc_ctor) {
    if (!IS_KIND(self, MCS_O_INSTANCE)) return self;
    mcs_instance_t* in = AS_INSTANCE(self);
    mcs_class_t* c = in->cls;
    char msg[240];
    const char* a0 = argc > 0 && IS_STRING(argv[0]) ? AS_CSTR(argv[0]) : NULL;
    const char* a1 = argc > 1 && IS_STRING(argv[1]) ? AS_CSTR(argv[1]) : NULL;
    bool param_first = derives(c, vm->exc[EXC_ARGNULL]) || derives(c, vm->exc[EXC_ARGRANGE]);
    if (argc == 0 || (argc >= 1 && argv[0].type == MCS_T_NULL)) {
        if (derives(c, vm->exc[EXC_ARGNULL])) snprintf(msg, sizeof msg, "Value cannot be null.");
        else if (derives(c, vm->exc[EXC_ARGRANGE])) snprintf(msg, sizeof msg, "Specified argument was out of the range of valid values.");
        else snprintf(msg, sizeof msg, "Exception of type '%s%s' was thrown.", is_builtin_exc(vm, c) ? "System." : "", c->name->chars);
    } else if (param_first && argc == 1 && a0) {
        snprintf(msg, sizeof msg, "%s (Parameter '%s')",
                 derives(c, vm->exc[EXC_ARGNULL]) ? "Value cannot be null." : "Specified argument was out of the range of valid values.", a0);
    } else if (param_first && a0 && a1) {
        snprintf(msg, sizeof msg, "%s (Parameter '%s')", a1, a0);
    } else if (derives(c, vm->exc[EXC_ARGUMENT]) && a0 && a1) {
        snprintf(msg, sizeof msg, "%s (Parameter '%s')", a0, a1);
    } else if (a0) {
        set_named(vm, in, "Message", argv[0]);
        if (argc > 1 && IS_KIND(argv[1], MCS_O_INSTANCE)) set_named(vm, in, "InnerException", argv[1]);
        return self;
    } else {
        mcs_string_t* s = mcs_value_to_string(vm, argv[0]);
        CHECK();
        set_named(vm, in, "Message", OBJ_VAL(s));
        return self;
    }
    set_named(vm, in, "Message", lib_cstr(vm, msg));
    if (argc > 1 && IS_KIND(argv[argc - 1], MCS_O_INSTANCE)) set_named(vm, in, "InnerException", argv[argc - 1]);
    return self;
}

NATIVE(exc_tostring) {
    if (!IS_KIND(self, MCS_O_INSTANCE)) return mcs_null();
    mcs_instance_t* in = AS_INSTANCE(self);
    mcs_buf_t b; mcs_buf_init(&b, vm);
    mcs_buf_puts(&b, in->cls->name->chars);
    mcs_value_t m = get_named(vm, in, "Message");
    if (IS_STRING(m) && AS_STRING(m)->len) { mcs_buf_puts(&b, ": "); mcs_buf_putn(&b, AS_CSTR(m), AS_STRING(m)->len); }
    mcs_value_t inner = get_named(vm, in, "InnerException");
    if (IS_KIND(inner, MCS_O_INSTANCE)) {
        mcs_buf_puts(&b, " ---> ");
        if (!mcs_value_to_buf(vm, &b, inner)) { mcs_buf_free(&b); return mcs_null(); }
    }
    return OBJ_VAL(mcs_buf_to_string(&b));
}
static const mcs_reg_t exc_methods[] = { MCS_FN(".ctor", exc_ctor, -1), MCS_FN("ToString", exc_tostring, 0), MCS_REG_END };

/* Built-in exception classes. parent = index of the base class in this table. */
static const struct { const char* name; int8_t kind; int8_t parent; } exc_tbl[] = {
    { "Exception", EXC_EXCEPTION, -1 },                  /*  0 */
    { "SystemException", EXC_SYSTEM, 0 },                /*  1 */
    { "ArithmeticException", -1, 1 },                    /*  2 */
    { "NullReferenceException", EXC_NULLREF, 1 },
    { "IndexOutOfRangeException", EXC_INDEX, 1 },
    { "DivideByZeroException", EXC_DIVZERO, 2 },
    { "OverflowException", EXC_OVERFLOW, 2 },
    { "InvalidCastException", EXC_INVCAST, 1 },
    { "ArgumentException", EXC_ARGUMENT, 1 },            /*  8 */
    { "ArgumentNullException", EXC_ARGNULL, 8 },
    { "ArgumentOutOfRangeException", EXC_ARGRANGE, 8 },
    { "InvalidOperationException", EXC_INVOP, 1 },       /* 11 */
    { "ObjectDisposedException", -1, 11 },
    { "KeyNotFoundException", EXC_KEYNOTFOUND, 1 },
    { "FormatException", EXC_FORMAT, 1 },
    { "NotSupportedException", EXC_NOTSUPPORTED, 1 },
    { "NotImplementedException", EXC_NOTIMPL, 1 },
    { "StackOverflowException", EXC_STACKOVF, 1 },
    { "OutOfMemoryException", EXC_OOM, 1 },
    { "MissingMemberException", EXC_MISSINGMEMBER, 1 },
    { "TimeoutException", -1, 0 },
    { "IOException", EXC_IO, 1 },                        /* 21 */
    { "FileNotFoundException", -1, 21 },
    { "DirectoryNotFoundException", -1, 21 },
    { "UnauthorizedAccessException", -1, 1 },
};
#define EXC_TBL_N ((int)(sizeof exc_tbl / sizeof exc_tbl[0]))

static mcs_class_t* make_exception(mcs_vm_t* vm, int i) {
    int kind = exc_tbl[i].kind;
    if (kind >= 0 && vm->exc[kind]) return vm->exc[kind];
    mcs_class_t* c;
    if (i == 0) {
        c = mcs_define_builtin_class(vm, "Exception", MCS_CLS(vm, object), CLS_SCRIPT);
        vm->gc_pause++;
        mcs_class_add_field(vm, c, vm->s_message, mcs_null());
        mcs_class_add_field(vm, c, mcs_intern_c(vm, "StackTrace"), mcs_null());
        mcs_class_add_field(vm, c, mcs_intern_c(vm, "InnerException"), mcs_null());
        vm->gc_pause--;
        mcs_add_regs(vm, c, exc_methods, false);
    } else {
        mcs_class_t* parent = make_exception(vm, exc_tbl[i].parent);
        if (kind < 0) {   /* no C-side slot: the global is the only reference */
            mcs_class_t* g = peek_global_class(vm, exc_tbl[i].name);
            if (g) return g;
        }
        c = mcs_define_builtin_class(vm, exc_tbl[i].name, NULL, CLS_SCRIPT);
        vm->gc_pause++;
        mcs_class_inherit_shared(vm, c, parent);   /* Exception's layout is final here */
        vm->gc_pause--;
    }
    if (kind >= 0) vm->exc[kind] = c;
    return c;
}
mcs_class_t* mcs_exc_class(mcs_vm_t* vm, int kind) {
    if (vm->exc[kind]) return vm->exc[kind];
    for (int i = 0; i < EXC_TBL_N; i++) if (exc_tbl[i].kind == kind) return make_exception(vm, i);
    return make_exception(vm, 0);
}

/* ============================================================ Console */
NATIVE(con_write) {
    mcs_value_t s = fmt_value(vm, argc, argv);
    CHECK();
    if (IS_STRING(s)) mcs_write(vm, AS_CSTR(s), AS_STRING(s)->len);
    return mcs_null();
}
NATIVE(con_writeline) {
    if (argc) { con_write(vm, self, argc, argv); CHECK(); }
    mcs_write(vm, "\n", 1);
    return mcs_null();
}
NATIVE(con_readline) {
    char buf[512];
    int n;
    if (vm->cfg.readline_fn) n = vm->cfg.readline_fn(vm->cfg.user_data, buf, sizeof buf);
    else {
#if MCS_ENABLE_STDIO
        if (!fgets(buf, sizeof buf, stdin)) return mcs_null();
        n = (int)strlen(buf);
#else
        return mcs_null();
#endif
    }
    if (n < 0) return mcs_null();
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
    return lib_str(vm, buf, (size_t)n);
}
NATIVE(con_clear) { mcs_write(vm, "\x1b[2J\x1b[H", 7); return mcs_null(); }
static const mcs_reg_t console_fns[] = {
    MCS_FN("WriteLine", con_writeline, -1), MCS_FN("Write", con_write, -1),
    MCS_FN("ReadLine", con_readline, 0), MCS_FN("Clear", con_clear, 0), MCS_REG_END
};

/* ============================================================ numbers */
#if MCS_ENABLE_FLOAT
#if MCS_FLOAT_DOUBLE
#define FM(f) f
#else
#define FM(fn) fn##f
#endif
static mcs_float_t round_even(mcs_float_t x) { return FM(rint)(x); } /* default FE_TONEAREST = banker's */
#endif

static mcs_value_t to_int_checked(mcs_vm_t* vm, mcs_value_t v, const char* tname, mcs_int_t lo, mcs_int_t hi) {
    mcs_int_t r = 0;
    if (is_intlike_v(v)) r = v.as.i;
    else if (v.type == MCS_T_BOOL) r = v.as.b;
    else if (v.type == MCS_T_NULL) r = 0;
#if MCS_ENABLE_FLOAT
    else if (v.type == MCS_T_FLOAT) {
        mcs_float_t f = round_even(v.as.f);
        if (f != f || f < (mcs_float_t)lo - 0.5 || f > (mcs_float_t)hi + 0.5) { mcs_throw(vm, EXC_OVERFLOW, "Value was either too large or too small for %s.", tname); return mcs_null(); }
        r = (mcs_int_t)f;
    }
#endif
    else if (IS_STRING(v)) {
        bool ovf;
        if (!lib_parse_int(AS_CSTR(v), AS_STRING(v)->len, 10, &r, &ovf)) {
            if (ovf) mcs_throw(vm, EXC_OVERFLOW, "Value was either too large or too small for %s.", tname);
            else mcs_throw(vm, EXC_FORMAT, "The input string '%s' was not in a correct format.", AS_CSTR(v));
            return mcs_null();
        }
    } else { mcs_throw(vm, EXC_INVCAST, "Unable to cast object of type '%s' to type '%s'.", mcs_type_name(vm, v), tname); return mcs_null(); }
    if (r < lo || r > hi) { mcs_throw(vm, EXC_OVERFLOW, "Value was either too large or too small for %s.", tname); return mcs_null(); }
    return mcs_int(r);
}

#define INT_MIN_V ((mcs_int_t)((mcs_uint_t)1 << (sizeof(mcs_int_t) * 8 - 1)))
#define INT_MAX_V ((mcs_int_t)(((mcs_uint_t)1 << (sizeof(mcs_int_t) * 8 - 1)) - 1))

NATIVE(int_parse) {
    mcs_string_t* s = lib_need_str(vm, argv[0], "s"); CHECK();
    mcs_int_t r; bool ovf;
    int base = (argc > 1 && argv[1].type == MCS_T_INT && argv[1].as.i == 515) ? 16 : 10; /* NumberStyles.HexNumber */
    if (!lib_parse_int(s->chars, s->len, base, &r, &ovf)) {
        if (ovf) mcs_throw(vm, EXC_OVERFLOW, "Value was either too large or too small for an Int32.");
        else mcs_throw(vm, EXC_FORMAT, "The input string '%s' was not in a correct format.", s->chars);
        return mcs_null();
    }
    return mcs_int(r);
}
NATIVE(int_compareto) {
    bool ok; int r = mcs_compare_values(vm, self, argv[0], &ok);
    CHECK();
    return mcs_int(r);
}
NATIVE(val_hasvalue) { return mcs_bool(true); }
NATIVE(val_value) { return self; }
NATIVE(val_getvalueordefault) { return self; }
static const mcs_reg_t value_methods[] = {
    MCS_FN("CompareTo", int_compareto, 1), MCS_GET("HasValue", val_hasvalue), MCS_GET("Value", val_value),
    MCS_FN("GetValueOrDefault", val_getvalueordefault, -1), MCS_REG_END
};
NATIVE(int_tryparse) { /* int.TryParse(s, out int v) */
    mcs_int_t r = 0; bool ovf;
    bool ok = IS_STRING(argv[0]) && lib_parse_int(AS_CSTR(argv[0]), AS_STRING(argv[0])->len, 10, &r, &ovf);
    lib_out_set(argv[argc - 1], mcs_int(ok ? r : 0));
    return mcs_bool(ok);
}
static const mcs_reg_t int_statics[] = { MCS_FN("Parse", int_parse, -1), MCS_FN("TryParse", int_tryparse, 2), MCS_REG_END };

#define SMALL_INT_PARSE(fname, tname, lo, hi) \
    NATIVE(fname) { return to_int_checked(vm, IS_STRING(argv[0]) ? argv[0] : mcs_null(), tname, lo, hi); }
SMALL_INT_PARSE(byte_parse, "a Byte", 0, 255)
SMALL_INT_PARSE(sbyte_parse, "an SByte", -128, 127)
SMALL_INT_PARSE(short_parse, "an Int16", -32768, 32767)
SMALL_INT_PARSE(ushort_parse, "a UInt16", 0, 65535)

static mcs_class_t* small_int_class(mcs_vm_t* vm, const char* name, mcs_native_fn parse, mcs_int_t lo, mcs_int_t hi) {
    mcs_class_t* c = mcs_define_builtin_class(vm, name, MCS_CLS(vm, int), CLS_BUILTIN);
    mcs_reg_t r[] = { MCS_FN("Parse", parse, 1), MCS_REG_END };
    mcs_add_regs_eager(vm, c, r, true);
    vm->gc_pause++;
    mcs_table_set(vm, &c->statics, lib_cstr(vm, "MaxValue"), mcs_int(hi));
    mcs_table_set(vm, &c->statics, lib_cstr(vm, "MinValue"), mcs_int(lo));
    vm->gc_pause--;
    return c;
}

#if MCS_ENABLE_FLOAT
NATIVE(dbl_parse) {
    mcs_string_t* s = lib_need_str(vm, argv[0], "s"); CHECK();
    mcs_float_t f;
    if (!lib_parse_float(s->chars, s->len, &f)) { mcs_throw(vm, EXC_FORMAT, "The input string '%s' was not in a correct format.", s->chars); return mcs_null(); }
    return mcs_float(f);
}
NATIVE(dbl_tryparse) { /* double.TryParse(s, out double v) */
    mcs_float_t f = 0;
    bool ok = IS_STRING(argv[0]) && lib_parse_float(AS_CSTR(argv[0]), AS_STRING(argv[0])->len, &f);
    lib_out_set(argv[argc - 1], mcs_float(ok ? f : 0));
    return mcs_bool(ok);
}
static mcs_float_t numf(mcs_vm_t* vm, mcs_value_t v) { return mcs_to_float(vm, v); }
NATIVE(dbl_isnan) { mcs_float_t f = numf(vm, argv[0]); return mcs_bool(f != f); }
NATIVE(dbl_isinf) { mcs_float_t f = numf(vm, argv[0]); return mcs_bool(FM(fabs)(f) == (mcs_float_t)INFINITY); }
NATIVE(dbl_isposinf) { return mcs_bool(numf(vm, argv[0]) == (mcs_float_t)INFINITY); }
NATIVE(dbl_isneginf) { return mcs_bool(numf(vm, argv[0]) == -(mcs_float_t)INFINITY); }
NATIVE(dbl_isfinite) { mcs_float_t f = numf(vm, argv[0]); return mcs_bool(f == f && FM(fabs)(f) != (mcs_float_t)INFINITY); }
static const mcs_reg_t dbl_statics[] = {
    MCS_FN("Parse", dbl_parse, -1), MCS_FN("TryParse", dbl_tryparse, 2), MCS_FN("IsNaN", dbl_isnan, 1), MCS_FN("IsInfinity", dbl_isinf, 1),
    MCS_FN("IsPositiveInfinity", dbl_isposinf, 1), MCS_FN("IsNegativeInfinity", dbl_isneginf, 1),
    MCS_FN("IsFinite", dbl_isfinite, 1), MCS_REG_END
};
#endif

static int bool_text(mcs_string_t* s) { /* 1 = true, 0 = false, -1 = invalid */
    char t[8]; size_t n = 0, i = 0, len = s->len;
    while (i < len && is_ws(s->chars[i])) i++;
    while (len > i && is_ws(s->chars[len - 1])) len--;
    for (; i < len && n < 7; i++) t[n++] = (char)((s->chars[i] >= 'A' && s->chars[i] <= 'Z') ? s->chars[i] + 32 : s->chars[i]);
    t[n] = 0;
    if (i == len && !strcmp(t, "true")) return 1;
    if (i == len && !strcmp(t, "false")) return 0;
    return -1;
}
NATIVE(bool_tryparse) {
    int r = IS_STRING(argv[0]) ? bool_text(AS_STRING(argv[0])) : -1;
    lib_out_set(argv[argc - 1], mcs_bool(r == 1));
    return mcs_bool(r >= 0);
}
NATIVE(bool_parse) {
    mcs_string_t* s = lib_need_str(vm, argv[0], "value"); CHECK();
    int r = bool_text(s);
    if (r >= 0) return mcs_bool(r == 1);
    mcs_throw(vm, EXC_FORMAT, "String '%s' was not recognized as a valid Boolean.", s->chars);
    return mcs_null();
}
static const mcs_reg_t bool_statics[] = { MCS_FN("Parse", bool_parse, 1), MCS_FN("TryParse", bool_tryparse, 2), MCS_REG_END };

/* ---- Char */
static uint32_t char_arg(mcs_vm_t* vm, int argc, mcs_value_t* argv) {
    if (argc == 2 && IS_STRING(argv[0])) {
        mcs_int_t i = mcs_to_int(vm, argv[1]);
        if (i < 0 || (mcs_uint_t)i >= AS_STRING(argv[0])->len) { mcs_throw(vm, EXC_ARGRANGE, "Index was out of range. (Parameter 'index')"); return 0; }
        return (uint8_t)AS_CSTR(argv[0])[i];
    }
    if (argc < 1 || !is_intlike_v(argv[0])) { mcs_throw(vm, EXC_ARGUMENT, "expected a char"); return 0; }
    return (uint32_t)argv[0].as.i;
}
static bool c_upper(uint32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7); }
static bool c_lower(uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7); }
static bool c_letter(uint32_t c) { return c_upper(c) || c_lower(c) || (c >= 0x100 && c != 0x2028 && c != 0x2029 && c != 0x3000); }
static bool c_digit(uint32_t c) { return c >= '0' && c <= '9'; }
static bool c_space(uint32_t c) { return c == ' ' || (c >= 9 && c <= 13) || c == 0xA0 || c == 0x2028 || c == 0x2029 || c == 0x3000; }
static bool c_punct(uint32_t c) { return (c >= 33 && c <= 47 && c != '$' && c != '+') || (c >= 58 && c <= 64 && c != '<' && c != '=' && c != '>') || (c >= 91 && c <= 96 && c != '^' && c != '`') || c == '{' || c == '}'; }
#define CHAR_PRED(fname, expr) NATIVE(fname) { uint32_t c = char_arg(vm, argc, argv); CHECK(); return mcs_bool(expr); }
CHAR_PRED(ch_isdigit, c_digit(c))
CHAR_PRED(ch_isletter, c_letter(c))
CHAR_PRED(ch_isletterordigit, c_letter(c) || c_digit(c))
CHAR_PRED(ch_iswhite, c_space(c))
CHAR_PRED(ch_isupper, c_upper(c))
CHAR_PRED(ch_islower, c_lower(c))
CHAR_PRED(ch_ispunct, c_punct(c))
CHAR_PRED(ch_iscontrol, c < 32 || (c >= 127 && c < 160))
CHAR_PRED(ch_isnumber, c_digit(c))
CHAR_PRED(ch_issymbol, c == '$' || c == '+' || c == '<' || c == '=' || c == '>' || c == '^' || c == '`' || c == '|' || c == '~')
NATIVE(ch_toupper) { uint32_t c = char_arg(vm, argc, argv); CHECK(); return mcs_char(c_lower(c) && c != 0xDF && c != 0xFF ? c - 32 : c); }
NATIVE(ch_tolower) { uint32_t c = char_arg(vm, argc, argv); CHECK(); return mcs_char(c_upper(c) ? c + 32 : c); }
NATIVE(ch_numval) {
    uint32_t c = char_arg(vm, argc, argv); CHECK();
#if MCS_ENABLE_FLOAT
    return mcs_float(c_digit(c) ? (mcs_float_t)(c - '0') : (mcs_float_t)-1);
#else
    return mcs_int(c_digit(c) ? (mcs_int_t)(c - '0') : -1);
#endif
}
NATIVE(ch_parse) {
    mcs_string_t* s = lib_need_str(vm, argv[0], "s"); CHECK();
    if (s->len == 0) { mcs_throw(vm, EXC_FORMAT, "String must be exactly one character long."); return mcs_null(); }
    uint8_t c0 = (uint8_t)s->chars[0]; uint32_t cp = c0; int extra = 0;
    if (c0 >= 0xF0) { cp = c0 & 7; extra = 3; } else if (c0 >= 0xE0) { cp = c0 & 15; extra = 2; } else if (c0 >= 0xC0) { cp = c0 & 31; extra = 1; }
    if ((uint32_t)extra + 1 != s->len) { mcs_throw(vm, EXC_FORMAT, "String must be exactly one character long."); return mcs_null(); }
    for (int k = 1; k <= extra; k++) cp = (cp << 6) | ((uint8_t)s->chars[k] & 0x3F);
    return mcs_char(cp);
}
static const mcs_reg_t char_statics[] = {
    MCS_FN("IsDigit", ch_isdigit, -1), MCS_FN("IsLetter", ch_isletter, -1), MCS_FN("IsLetterOrDigit", ch_isletterordigit, -1),
    MCS_FN("IsWhiteSpace", ch_iswhite, -1), MCS_FN("IsUpper", ch_isupper, -1), MCS_FN("IsLower", ch_islower, -1),
    MCS_FN("IsPunctuation", ch_ispunct, -1), MCS_FN("IsControl", ch_iscontrol, -1), MCS_FN("IsNumber", ch_isnumber, -1),
    MCS_FN("IsSymbol", ch_issymbol, -1), MCS_FN("ToUpper", ch_toupper, -1), MCS_FN("ToLower", ch_tolower, -1),
    MCS_FN("ToUpperInvariant", ch_toupper, -1), MCS_FN("ToLowerInvariant", ch_tolower, -1),
    MCS_FN("GetNumericValue", ch_numval, -1), MCS_FN("Parse", ch_parse, 1), MCS_REG_END
};

/* ============================================================ Convert */
NATIVE(cv_toint32) {
    if (argc == 2 && IS_STRING(argv[0])) {
        mcs_int_t base = mcs_to_int(vm, argv[1]); CHECK();
        if (base != 2 && base != 8 && base != 10 && base != 16) { mcs_throw(vm, EXC_ARGUMENT, "Invalid Base."); return mcs_null(); }
        mcs_int_t r; bool ovf;
        if (!lib_parse_int(AS_CSTR(argv[0]), AS_STRING(argv[0])->len, (int)base, &r, &ovf)) {
            mcs_throw(vm, ovf ? EXC_OVERFLOW : EXC_FORMAT, ovf ? "Value was either too large or too small for an Int32." : "Could not find any recognizable digits.");
            return mcs_null();
        }
        if (base != 10 && sizeof(mcs_int_t) == 4) r = (mcs_int_t)(int32_t)(uint32_t)r;
        return mcs_int(r);
    }
    ARGN(1);
    return to_int_checked(vm, argv[0], "an Int32", INT_MIN_V, INT_MAX_V);
}
NATIVE(cv_tobyte) { ARGN(1); return to_int_checked(vm, argv[0], "an unsigned byte", 0, 255); }
NATIVE(cv_tosbyte) { ARGN(1); return to_int_checked(vm, argv[0], "a signed byte", -128, 127); }
NATIVE(cv_toint16) { ARGN(1); return to_int_checked(vm, argv[0], "an Int16", -32768, 32767); }
NATIVE(cv_touint16) { ARGN(1); return to_int_checked(vm, argv[0], "a UInt16", 0, 65535); }
#if MCS_ENABLE_FLOAT
NATIVE(cv_todouble) {
    mcs_value_t v = argv[0];
    if (v.type == MCS_T_FLOAT) return v;
    if (is_intlike_v(v)) return mcs_float((mcs_float_t)v.as.i);
    if (v.type == MCS_T_BOOL) return mcs_float(v.as.b ? 1 : 0);
    if (v.type == MCS_T_NULL) return mcs_float(0);
    if (IS_STRING(v)) return dbl_parse(vm, self, 1, argv);
    mcs_throw(vm, EXC_INVCAST, "Unable to cast object of type '%s' to type 'Double'.", mcs_type_name(vm, v));
    return mcs_null();
}
#endif
NATIVE(cv_tobool) {
    mcs_value_t v = argv[0];
    if (v.type == MCS_T_BOOL) return v;
    if (is_intlike_v(v)) return mcs_bool(v.as.i != 0);
#if MCS_ENABLE_FLOAT
    if (v.type == MCS_T_FLOAT) return mcs_bool(v.as.f != 0);
#endif
    if (v.type == MCS_T_NULL) return mcs_bool(false);
    if (IS_STRING(v)) return bool_parse(vm, self, 1, argv);
    mcs_throw(vm, EXC_INVCAST, "Unable to cast object of type '%s' to type 'Boolean'.", mcs_type_name(vm, v));
    return mcs_null();
}
NATIVE(cv_tochar) {
    mcs_value_t v = argv[0];
    if (IS_STRING(v)) return ch_parse(vm, self, 1, argv);
    mcs_value_t r = to_int_checked(vm, v, "Char", 0, 0x10FFFF); CHECK();
    return mcs_char((uint32_t)r.as.i);
}
NATIVE(cv_tostring) {
    if (argc == 2 && is_intlike_v(argv[0]) && argv[1].type == MCS_T_INT) {
        int base = (int)argv[1].as.i;
        if (base != 2 && base != 8 && base != 10 && base != 16) { mcs_throw(vm, EXC_ARGUMENT, "Invalid Base."); return mcs_null(); }
        if (base == 10) return mcs_tostring(vm, argv[0]);
        mcs_uint_t u = (mcs_uint_t)argv[0].as.i;
        if (sizeof(mcs_int_t) == 8 && argv[0].as.i >= INT32_MIN && argv[0].as.i <= INT32_MAX) u = (uint32_t)(int32_t)argv[0].as.i;
        char tmp[70]; int n = 0;
        do { int d = (int)(u % (mcs_uint_t)base); tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (mcs_uint_t)base; } while (u);
        char out[70];
        for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
        return lib_str(vm, out, (size_t)n);
    }
    if (argc == 2 && IS_STRING(argv[1])) return obj_tostring_fmt(vm, argv[0], 1, argv + 1);
    ARGN(1);
    return mcs_tostring(vm, argv[0]);
}
static const mcs_reg_t convert_fns[] = {
    MCS_FN("ToInt32", cv_toint32, -1), MCS_FN("ToInt64", cv_toint32, -1), MCS_FN("ToUInt32", cv_toint32, -1),
    MCS_FN("ToByte", cv_tobyte, -1), MCS_FN("ToSByte", cv_tosbyte, -1), MCS_FN("ToInt16", cv_toint16, -1),
    MCS_FN("ToUInt16", cv_touint16, -1),
#if MCS_ENABLE_FLOAT
    MCS_FN("ToDouble", cv_todouble, 1), MCS_FN("ToSingle", cv_todouble, 1),
#endif
    MCS_FN("ToBoolean", cv_tobool, 1), MCS_FN("ToChar", cv_tochar, 1), MCS_FN("ToString", cv_tostring, -1),
    MCS_REG_END
};

/* ============================================================ Math */
static bool any_float(int argc, mcs_value_t* argv) {
    for (int i = 0; i < argc; i++) if (argv[i].type == MCS_T_FLOAT) return true;
    return false;
}
static bool check_nums(mcs_vm_t* vm, int argc, mcs_value_t* argv) {
    for (int i = 0; i < argc; i++)
        if (!is_intlike_v(argv[i]) && argv[i].type != MCS_T_FLOAT) { mcs_throw(vm, EXC_ARGUMENT, "Math: argument %d must be a number, got %s", i + 1, mcs_type_name(vm, argv[i])); return false; }
    return true;
}
NATIVE(m_abs) {
    if (!check_nums(vm, 1, argv)) return mcs_null();
#if MCS_ENABLE_FLOAT
    if (argv[0].type == MCS_T_FLOAT) return mcs_float(FM(fabs)(argv[0].as.f));
#endif
    if (argv[0].as.i == INT_MIN_V) { mcs_throw(vm, EXC_OVERFLOW, "Negating the minimum value of a twos complement number is invalid."); return mcs_null(); }
    return mcs_int(argv[0].as.i < 0 ? -argv[0].as.i : argv[0].as.i);
}
static mcs_value_t minmax(mcs_vm_t* vm, int argc, mcs_value_t* argv, bool want_max) {
    if (!check_nums(vm, argc, argv)) return mcs_null();
#if MCS_ENABLE_FLOAT
    if (any_float(argc, argv)) {
        mcs_float_t a = mcs_to_float(vm, argv[0]), b = mcs_to_float(vm, argv[1]);
        if (a != a || b != b) return mcs_float((mcs_float_t)NAN);
        return mcs_float(want_max ? (a > b ? a : b) : (a < b ? a : b));
    }
#else
    (void)any_float;
#endif
    mcs_int_t a = argv[0].as.i, b = argv[1].as.i;
    return mcs_int(want_max ? (a > b ? a : b) : (a < b ? a : b));
}
NATIVE(m_max) { return minmax(vm, argc, argv, true); }
NATIVE(m_min) { return minmax(vm, argc, argv, false); }
NATIVE(m_sign) {
    if (!check_nums(vm, 1, argv)) return mcs_null();
#if MCS_ENABLE_FLOAT
    if (argv[0].type == MCS_T_FLOAT) {
        mcs_float_t f = argv[0].as.f;
        if (f != f) { mcs_throw(vm, EXC_ARGUMENT, "Function does not accept floating point Not-a-Number values."); return mcs_null(); }
        return mcs_int(f > 0 ? 1 : f < 0 ? -1 : 0);
    }
#endif
    return mcs_int(argv[0].as.i > 0 ? 1 : argv[0].as.i < 0 ? -1 : 0);
}
NATIVE(m_clamp) {
    if (!check_nums(vm, 3, argv)) return mcs_null();
#if MCS_ENABLE_FLOAT
    if (any_float(3, argv)) {
        mcs_float_t v = mcs_to_float(vm, argv[0]), lo = mcs_to_float(vm, argv[1]), hi = mcs_to_float(vm, argv[2]);
        if (lo > hi) { mcs_throw(vm, EXC_ARGUMENT, "'%g' cannot be greater than %g.", (double)lo, (double)hi); return mcs_null(); }
        return mcs_float(v < lo ? lo : v > hi ? hi : v);
    }
#endif
    mcs_int_t v = argv[0].as.i, lo = argv[1].as.i, hi = argv[2].as.i;
    if (lo > hi) { mcs_throw(vm, EXC_ARGUMENT, "'%ld' cannot be greater than %ld.", (long)lo, (long)hi); return mcs_null(); }
    return mcs_int(v < lo ? lo : v > hi ? hi : v);
}
NATIVE(m_divrem_floor) { /* Math.DivRem(a, b) -> a / b (remainder unsupported w/o out) */
    if (!check_nums(vm, 2, argv)) return mcs_null();
    if (argv[1].as.i == 0) { mcs_throw(vm, EXC_DIVZERO, "Attempted to divide by zero."); return mcs_null(); }
    return mcs_int(argv[0].as.i / argv[1].as.i);
}

#if MCS_ENABLE_FLOAT
#define MATH1(name, expr) NATIVE(name) { if (!check_nums(vm, 1, argv)) return mcs_null(); mcs_float_t x = mcs_to_float(vm, argv[0]); return mcs_float(expr); }
MATH1(m_sqrt, FM(sqrt)(x))
MATH1(m_sin, FM(sin)(x))
MATH1(m_cos, FM(cos)(x))
MATH1(m_tan, FM(tan)(x))
MATH1(m_asin, FM(asin)(x))
MATH1(m_acos, FM(acos)(x))
MATH1(m_atan, FM(atan)(x))
MATH1(m_sinh, FM(sinh)(x))
MATH1(m_cosh, FM(cosh)(x))
MATH1(m_tanh, FM(tanh)(x))
MATH1(m_exp, FM(exp)(x))
MATH1(m_log10, FM(log10)(x))
MATH1(m_log2, FM(log2)(x))
MATH1(m_cbrt, FM(cbrt)(x))
MATH1(m_floor, FM(floor)(x))
MATH1(m_ceil, FM(ceil)(x))
MATH1(m_trunc, FM(trunc)(x))
NATIVE(m_log) {
    if (!check_nums(vm, argc, argv)) return mcs_null();
    mcs_float_t x = mcs_to_float(vm, argv[0]);
    if (argc >= 2) return mcs_float(FM(log)(x) / FM(log)(mcs_to_float(vm, argv[1])));
    return mcs_float(FM(log)(x));
}
NATIVE(m_pow) { if (!check_nums(vm, 2, argv)) return mcs_null(); return mcs_float(FM(pow)(mcs_to_float(vm, argv[0]), mcs_to_float(vm, argv[1]))); }
NATIVE(m_atan2) { if (!check_nums(vm, 2, argv)) return mcs_null(); return mcs_float(FM(atan2)(mcs_to_float(vm, argv[0]), mcs_to_float(vm, argv[1]))); }
NATIVE(m_hypot) { if (!check_nums(vm, 2, argv)) return mcs_null(); return mcs_float(FM(hypot)(mcs_to_float(vm, argv[0]), mcs_to_float(vm, argv[1]))); }
NATIVE(m_round) {
    if (!check_nums(vm, argc > 2 ? 2 : argc, argv)) return mcs_null();
    if (is_intlike_v(argv[0]) && argc == 1) return mcs_float((mcs_float_t)argv[0].as.i);
    mcs_float_t x = mcs_to_float(vm, argv[0]);
    if (argc >= 2) {
        mcs_int_t d = mcs_to_int(vm, argv[1]); CHECK();
        if (d < 0 || d > 15) { mcs_throw(vm, EXC_ARGRANGE, "Rounding digits must be between 0 and 15, inclusive. (Parameter 'digits')"); return mcs_null(); }
        mcs_float_t p = FM(pow)((mcs_float_t)10, (mcs_float_t)d);
        return mcs_float(round_even(x * p) / p);
    }
    return mcs_float(round_even(x));
}
NATIVE(m_fma) { if (!check_nums(vm, 3, argv)) return mcs_null(); return mcs_float(FM(fma)(mcs_to_float(vm, argv[0]), mcs_to_float(vm, argv[1]), mcs_to_float(vm, argv[2]))); }
NATIVE(m_ieeerem) { if (!check_nums(vm, 2, argv)) return mcs_null(); return mcs_float(FM(remainder)(mcs_to_float(vm, argv[0]), mcs_to_float(vm, argv[1]))); }
#endif

static const mcs_reg_t math_fns[] = {
    MCS_FN("Abs", m_abs, 1), MCS_FN("Max", m_max, 2), MCS_FN("Min", m_min, 2), MCS_FN("Sign", m_sign, 1),
    MCS_FN("Clamp", m_clamp, 3), MCS_FN("DivRem", m_divrem_floor, 2),
#if MCS_ENABLE_FLOAT
    MCS_FN("Sqrt", m_sqrt, 1), MCS_FN("Sin", m_sin, 1), MCS_FN("Cos", m_cos, 1), MCS_FN("Tan", m_tan, 1),
    MCS_FN("Asin", m_asin, 1), MCS_FN("Acos", m_acos, 1), MCS_FN("Atan", m_atan, 1), MCS_FN("Atan2", m_atan2, 2),
    MCS_FN("Sinh", m_sinh, 1), MCS_FN("Cosh", m_cosh, 1), MCS_FN("Tanh", m_tanh, 1),
    MCS_FN("Exp", m_exp, 1), MCS_FN("Log", m_log, -1), MCS_FN("Log10", m_log10, 1), MCS_FN("Log2", m_log2, 1),
    MCS_FN("Pow", m_pow, 2), MCS_FN("Cbrt", m_cbrt, 1), MCS_FN("Hypot", m_hypot, 2),
    MCS_FN("Floor", m_floor, 1), MCS_FN("Ceiling", m_ceil, 1), MCS_FN("Truncate", m_trunc, 1),
    MCS_FN("Round", m_round, -1), MCS_FN("FusedMultiplyAdd", m_fma, 3), MCS_FN("IEEERemainder", m_ieeerem, 2),
#endif
    MCS_REG_END
};

/* ============================================================ System */
NATIVE(env_tickcount) { return mcs_int((mcs_int_t)(int32_t)lib_ticks(vm)); }
NATIVE(env_tickcount64) { return mcs_int((mcs_int_t)lib_ticks(vm)); }
static const mcs_reg_t env_fns[] = { MCS_GET("TickCount", env_tickcount), MCS_GET("TickCount64", env_tickcount64), MCS_REG_END };

NATIVE(th_sleep) {
    mcs_int_t ms = mcs_to_int(vm, argv[0]); CHECK();
    if (ms < 0) ms = 0;
    mcs_sleep(vm, (uint32_t)ms);
    return mcs_null();
}
#if !MCS_ENABLE_THREADS
/* no OS chosen (MCS_OS_NONE): one core, no threads; the real members live in
 * modules/threads/mcs_threads.c. Lets scripts test Thread.Os == "None". */
NATIVE(th_cores) { return mcs_int(1); }
NATIVE(th_core) { return mcs_int(0); }
NATIVE(th_os) { return mcs_string(vm, "None"); }
#endif
static const mcs_reg_t thread_fns[] = {
    MCS_FN("Sleep", th_sleep, 1),
#if !MCS_ENABLE_THREADS
    MCS_GET("Cores", th_cores), MCS_GET("CurrentCore", th_core), MCS_GET("Os", th_os),
#endif
    MCS_REG_END
};

NATIVE(gc_collect) { mcs_collect(vm); return mcs_null(); }
NATIVE(gc_total) {
    if (argc && mcs_truthy(argv[0])) mcs_collect(vm);
    return mcs_int((mcs_int_t)vm->bytes_allocated);
}
NATIVE(gc_count) { return mcs_int((mcs_int_t)vm->gc_count); }
static const mcs_reg_t gc_fns[] = {
    MCS_FN("Collect", gc_collect, -1), MCS_FN("GetTotalMemory", gc_total, -1), MCS_FN("CollectionCount", gc_count, -1), MCS_REG_END
};

NATIVE(dbg_assert) {
    if (argc && !mcs_truthy(argv[0])) {
        mcs_throw(vm, EXC_INVOP, "Assertion failed%s%s", argc > 1 && IS_STRING(argv[1]) ? ": " : ".", argc > 1 && IS_STRING(argv[1]) ? AS_CSTR(argv[1]) : "");
    }
    return mcs_null();
}
static const mcs_reg_t debug_fns[] = { MCS_FN("Assert", dbg_assert, -1), MCS_FN("WriteLine", con_writeline, -1), MCS_FN("Write", con_write, -1), MCS_REG_END };

/* ---- Random (xorshift64*) */
#if MCS_ENABLE_RANDOM
typedef struct { uint64_t s; } rng_t;
static const mcs_class_def_t random_def;
static uint64_t rng_next(rng_t* r) { r->s ^= r->s >> 12; r->s ^= r->s << 25; r->s ^= r->s >> 27; return r->s * 2685821657736338717ULL; }
static void rng_seed(rng_t* r, uint64_t seed) { r->s = seed * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL; if (!r->s) r->s = 1; rng_next(r); }
static void random_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    rng_t* r = (rng_t*)mcs_userdata(self);
    static uint32_t counter;
    uint64_t seed = argc ? (uint64_t)mcs_to_int(vm, argv[0]) : ((uint64_t)lib_ticks(vm) << 20) ^ (uint64_t)(uintptr_t)r ^ (++counter * 7919u);
    rng_seed(r, seed);
}
NATIVE(rnd_next) {
    rng_t* r = (rng_t*)mcs_check_userdata(vm, self, &random_def); CHECK();
    uint32_t x = (uint32_t)(rng_next(r) >> 33); /* 31 bits */
    if (argc == 0) return mcs_int((mcs_int_t)x);
    mcs_int_t lo = 0, hi;
    if (argc == 1) hi = mcs_to_int(vm, argv[0]);
    else { lo = mcs_to_int(vm, argv[0]); hi = mcs_to_int(vm, argv[1]); }
    CHECK();
    if (hi < lo || (argc == 1 && hi < 0)) { mcs_throw(vm, EXC_ARGRANGE, "'minValue' cannot be greater than maxValue. (Parameter 'minValue')"); return mcs_null(); }
    uint64_t range = (uint64_t)(hi - lo);
    if (!range) return mcs_int(lo);
    return mcs_int(lo + (mcs_int_t)(rng_next(r) % range));
}
#if MCS_ENABLE_FLOAT
NATIVE(rnd_nextdouble) {
    rng_t* r = (rng_t*)mcs_check_userdata(vm, self, &random_def); CHECK();
    return mcs_float((mcs_float_t)((double)(rng_next(r) >> 11) * (1.0 / 9007199254740992.0)));
}
#endif
NATIVE(rnd_nextbytes) {
    rng_t* r = (rng_t*)mcs_check_userdata(vm, self, &random_def); CHECK();
    if (!lib_is_seq(argv[0])) { mcs_throw(vm, EXC_ARGNULL, "Value cannot be null. (Parameter 'buffer')"); return mcs_null(); }
    mcs_list_t* l = AS_LIST(argv[0]);
    for (uint32_t i = 0; i < l->count; i++) l->items[i] = mcs_int((mcs_int_t)(rng_next(r) >> 56));
    return mcs_null();
}
static const mcs_reg_t random_members[] = {
    MCS_FN("Next", rnd_next, -1),
#if MCS_ENABLE_FLOAT
    MCS_FN("NextDouble", rnd_nextdouble, 0), MCS_FN("NextSingle", rnd_nextdouble, 0),
#endif
    MCS_FN("NextBytes", rnd_nextbytes, 1), MCS_REG_END
};
static const mcs_class_def_t random_def = { "Random", sizeof(rng_t), random_ctor, NULL, random_members, NULL };
#endif

/* ---- Stopwatch */
typedef struct { uint32_t start, acc; bool running; } sw_t;
static const mcs_class_def_t stopwatch_def;
static uint32_t sw_elapsed(mcs_vm_t* vm, sw_t* s) { return s->acc + (s->running ? lib_ticks(vm) - s->start : 0); }
#define SW() sw_t* s = (sw_t*)mcs_check_userdata(vm, self, &stopwatch_def); CHECK()
NATIVE(sw_start) { SW(); if (!s->running) { s->start = lib_ticks(vm); s->running = true; } return mcs_null(); }
NATIVE(sw_stop) { SW(); if (s->running) { s->acc = sw_elapsed(vm, s); s->running = false; } return mcs_null(); }
NATIVE(sw_reset) { SW(); s->acc = 0; s->running = false; return mcs_null(); }
NATIVE(sw_restart) { SW(); s->acc = 0; s->start = lib_ticks(vm); s->running = true; return mcs_null(); }
NATIVE(sw_ms) { SW(); return mcs_int((mcs_int_t)sw_elapsed(vm, s)); }
NATIVE(sw_ticks) { SW(); return mcs_int((mcs_int_t)sw_elapsed(vm, s) * 10000); }
NATIVE(sw_running) { SW(); return mcs_bool(s->running); }
NATIVE(sw_startnew) {
    mcs_class_t* c = lib_global_class(vm, "Stopwatch");
    mcs_userdata_t* u = mcs_new_userdata(vm, c, sizeof(sw_t));
    sw_t* s = (sw_t*)u->data;
    s->start = lib_ticks(vm); s->running = true;
    return OBJ_VAL(u);
}
static const mcs_reg_t sw_members[] = {
    MCS_FN("Start", sw_start, 0), MCS_FN("Stop", sw_stop, 0), MCS_FN("Reset", sw_reset, 0), MCS_FN("Restart", sw_restart, 0),
    MCS_GET("ElapsedMilliseconds", sw_ms), MCS_GET("ElapsedTicks", sw_ticks), MCS_GET("IsRunning", sw_running), MCS_REG_END
};
static const mcs_reg_t sw_statics[] = { MCS_FN("StartNew", sw_startnew, 0), MCS_REG_END };
static const mcs_class_def_t stopwatch_def = { "Stopwatch", sizeof(sw_t), NULL, NULL, sw_members, sw_statics };

/* ---- Delegate */
NATIVE(del_invoke) {
    mcs_value_t r;
    if (!lib_call(vm, self, argc, argv, &r)) return mcs_null();
    return r;
}
static const mcs_reg_t delegate_methods[] = { MCS_FN("Invoke", del_invoke, -1), MCS_FN("DynamicInvoke", del_invoke, -1), MCS_REG_END };

/* ---- KeyValuePair */
NATIVE(kvp_new) {
    mcs_instance_t* kv = mcs_new_instance(vm, MCS_CLS(vm, kvp));
    if (argc > 0) kv->fields[0] = argv[0];
    if (argc > 1) kv->fields[1] = argv[1];
    return OBJ_VAL(kv);
}
NATIVE(kvp_tostring) {
    if (!IS_KIND(self, MCS_O_INSTANCE)) return mcs_null();
    mcs_buf_t b; mcs_buf_init(&b, vm);
    mcs_buf_putc(&b, '[');
    if (!mcs_value_to_buf(vm, &b, AS_INSTANCE(self)->fields[0])) goto fail;
    mcs_buf_puts(&b, ", ");
    if (!mcs_value_to_buf(vm, &b, AS_INSTANCE(self)->fields[1])) goto fail;
    mcs_buf_putc(&b, ']');
    return OBJ_VAL(mcs_buf_to_string(&b));
fail:
    mcs_buf_free(&b);
    return mcs_null();
}
static const mcs_reg_t kvp_methods[] = { MCS_FN("ToString", kvp_tostring, 0), MCS_REG_END };


NATIVE(tuple_tostring) {
    if (!IS_KIND(self, MCS_O_INSTANCE)) return mcs_null();
    mcs_instance_t* t = AS_INSTANCE(self);
    mcs_buf_t b; mcs_buf_init(&b, vm);
    mcs_buf_putc(&b, '(');
    for (uint16_t i = 0; i < t->cls->field_count; i++) {
        if (i) mcs_buf_puts(&b, ", ");
        if (!mcs_value_to_buf(vm, &b, t->fields[i])) { mcs_buf_free(&b); return mcs_null(); }
    }
    mcs_buf_putc(&b, ')');
    return OBJ_VAL(mcs_buf_to_string(&b));
}
static const mcs_reg_t tuple_methods[] = { MCS_FN("ToString", tuple_tostring, 0), MCS_REG_END };

/* ---- ValueTuple + compiler intrinsics (hidden `__rt` module, emitted by the parser) */
static mcs_class_t* tuple_class(mcs_vm_t* vm, int arity, const char* names, size_t nlen) {
    char key[200];
    if (nlen > 160) nlen = 160;
    int kn = snprintf(key, sizeof key, "%d:%.*s", arity, (int)nlen, names ? names : "");
    mcs_string_t* k = mcs_intern(vm, key, (size_t)kn);
    mcs_value_t c;
    if (mcs_table_get_s(&vm->tuple_classes, k, &c)) return AS_CLASS(c);
    vm->gc_pause++;
    mcs_class_t* cls = mcs_new_class(vm, mcs_intern_c(vm, "ValueTuple"), CLS_TUPLE);
    mcs_table_set(vm, &vm->tuple_classes, OBJ_VAL(k), OBJ_VAL(cls));
    mcs_class_inherit(vm, cls, MCS_CLS(vm, object));
    cls->ckind = CLS_TUPLE;
    for (int i = 0; i < arity; i++) {
        char nm[8]; int n = snprintf(nm, sizeof nm, "Item%d", i + 1);
        mcs_class_add_field(vm, cls, mcs_intern(vm, nm, (size_t)n), mcs_null());
    }
    /* element names are aliases of the ItemN slots */
    int slot = 0; size_t st = 0;
    for (size_t i = 0; names && i <= nlen; i++) {
        if (i == nlen || names[i] == ',') {
            if (i > st && slot < arity) mcs_table_set(vm, &cls->fields, OBJ_VAL(mcs_intern(vm, names + st, i - st)), mcs_int(slot));
            slot++; st = i + 1;
        }
    }
    mcs_add_regs_eager(vm, cls, tuple_methods, false);
    vm->gc_pause--;
    return cls;
}
static mcs_value_t tuple_make(mcs_vm_t* vm, mcs_value_t names, int n, mcs_value_t* items) {
    const char* nm = NULL; size_t nl = 0;
    if (IS_STRING(names)) { nm = AS_STRING(names)->chars; nl = AS_STRING(names)->len; }
    mcs_class_t* cls = tuple_class(vm, n, nm, nl);
    mcs_instance_t* t = mcs_new_instance(vm, cls);
    for (int i = 0; i < n; i++) t->fields[i] = items[i];
    return OBJ_VAL(t);
}
/* used by other library files (LINQ Zip) */
mcs_value_t mcs_lib_tuple(mcs_vm_t* vm, const char* names, int n, mcs_value_t* items) {
    mcs_class_t* cls = tuple_class(vm, n, names, names ? strlen(names) : 0);
    mcs_instance_t* t = mcs_new_instance(vm, cls);
    for (int i = 0; i < n; i++) t->fields[i] = items[i];
    return OBJ_VAL(t);
}
static bool is_tuple(mcs_value_t v) { return IS_KIND(v, MCS_O_INSTANCE) && AS_INSTANCE(v)->cls->ckind == CLS_TUPLE; }
NATIVE(rt_tuple) { ARGN(1); return tuple_make(vm, argv[0], argc - 1, argv + 1); }
NATIVE(rt_names) {
    ARGN(2);
    if (!is_tuple(argv[0]) || !IS_STRING(argv[1])) return argv[0];
    mcs_instance_t* t = AS_INSTANCE(argv[0]);
    int n = t->cls->field_count, commas = 0;
    mcs_string_t* nm = AS_STRING(argv[1]);
    for (uint32_t i = 0; i < nm->len; i++) commas += nm->chars[i] == ',';
    if (commas + 1 != n) return argv[0];
    mcs_class_t* cls = tuple_class(vm, n, nm->chars, nm->len);
    if (cls == t->cls) return argv[0];
    mcs_instance_t* r = mcs_new_instance(vm, cls);
    for (int i = 0; i < n; i++) r->fields[i] = t->fields[i];
    return OBJ_VAL(r);
}
NATIVE(rt_item) {
    ARGN(2);
    mcs_value_t v = argv[0]; mcs_int_t i = argv[1].as.i;
    if (IS_KIND(v, MCS_O_INSTANCE)) {
        mcs_instance_t* in = AS_INSTANCE(v);
        bool kvp = false;
        for (mcs_class_t* c = in->cls; c; c = c->super) if (c == vm->cls_kvp) kvp = true;
        if ((in->cls->ckind == CLS_TUPLE || kvp) && i >= 0 && i < in->cls->field_count) return in->fields[i];
    }
    if (lib_is_seq(v) && i >= 0 && (uint32_t)i < AS_LIST(v)->count) return AS_LIST(v)->items[i];
    if (v.type == MCS_T_NULL) mcs_throw(vm, EXC_NULLREF, "Object reference not set to an instance of an object.");
    else mcs_throw(vm, EXC_INVOP, "Cannot deconstruct a value of type '%s' (element %d)", mcs_type_name(vm, v), (int)i);
    return mcs_null();
}
static bool rt_length(mcs_vm_t* vm, mcs_value_t v, mcs_int_t* n) {
    if (IS_STRING(v)) { *n = (mcs_int_t)AS_STRING(v)->len; return true; }
    if (lib_is_seq(v)) { *n = (mcs_int_t)AS_LIST(v)->count; return true; }
    if (v.type == MCS_T_NULL) mcs_throw(vm, EXC_NULLREF, "Object reference not set to an instance of an object.");
    else mcs_throw(vm, EXC_NOTSUPPORTED, "'%s' does not support ^ indices or ranges", mcs_type_name(vm, v));
    return false;
}
NATIVE(rt_len) { ARGN(1); mcs_int_t n; return rt_length(vm, argv[0], &n) ? mcs_int(n) : mcs_null(); }
NATIVE(rt_atend) {
    ARGN(2);
    mcs_int_t n;
    if (!rt_length(vm, argv[0], &n)) return mcs_null();
    mcs_int_t i = n - argv[1].as.i;
    if (argv[1].type != MCS_T_INT || i < 0 || i >= n) { mcs_throw(vm, EXC_INDEX, "Index was outside the bounds of the array. (index ^%d, length %d)", (int)argv[1].as.i, (int)n); return mcs_null(); }
    if (IS_STRING(argv[0])) return mcs_char((uint8_t)AS_STRING(argv[0])->chars[i]);
    return AS_LIST(argv[0])->items[i];
}
NATIVE(rt_slice) {
    ARGN(5);
    mcs_value_t v = argv[0];
    mcs_int_t n;
    if (!rt_length(vm, v, &n)) return mcs_null();
    mcs_int_t lo = argv[1].as.i, hi = argv[3].as.i;
    if (argv[1].type != MCS_T_INT || argv[3].type != MCS_T_INT) { mcs_throw(vm, EXC_ARGUMENT, "range bounds must be integers"); return mcs_null(); }
    if (argv[2].as.b) lo = n - lo;
    if (argv[4].as.b) hi = n - hi;
    if (lo < 0 || hi > n || lo > hi) { mcs_throw(vm, EXC_ARGRANGE, "Specified argument was out of the range of valid values. (range %d..%d, length %d)", (int)lo, (int)hi, (int)n); return mcs_null(); }
    if (IS_STRING(v)) return lib_str(vm, AS_STRING(v)->chars + lo, (size_t)(hi - lo));
    mcs_list_t* src = AS_LIST(v);
    mcs_list_t* r = mcs_new_listobj(vm, OBJ_KIND(v), (uint32_t)(hi - lo));
    for (mcs_int_t i = lo; i < hi; i++) r->items[i - lo] = src->items[i];
    return OBJ_VAL(r);
}
static const mcs_reg_t rt_fns[] = {
    MCS_FN("Tuple", rt_tuple, -1), MCS_FN("Names", rt_names, 2), MCS_FN("Item", rt_item, 2),
    MCS_FN("Len", rt_len, 1), MCS_FN("AtEnd", rt_atend, 2), MCS_FN("Slice", rt_slice, 5), MCS_REG_END
};

/* ============================================================ open */
static void set_static(mcs_vm_t* vm, mcs_class_t* c, const char* name, mcs_value_t v) {
    vm->gc_pause++;
    mcs_table_set(vm, &c->statics, lib_cstr(vm, name), v);
    vm->gc_pause--;
}
static void alias_global(mcs_vm_t* vm, const char* alias, mcs_class_t* c) {
    vm->gc_pause++;
    { uint32_t gs_ = mcs_global_slot(vm, mcs_intern_c(vm, alias)); if (vm->globals[gs_].type == MCS_T_UNDEF) vm->globals[gs_] = OBJ_VAL(c); }
    vm->gc_pause--;
}
void lib_set_static(mcs_vm_t* vm, mcs_class_t* c, const char* name, mcs_value_t v) { set_static(vm, c, name, v); }
void lib_define_native_class(mcs_vm_t* vm, const mcs_class_def_t* def) { define_native_class(vm, def); }

/* Every built-in global, created on first use (or all in mcs_new() when
 * MCS_LAZY_CLASSES=0). The table stays in flash. */
enum {
    LZ_OBJECT, LZ_TYPE, LZ_INT, LZ_INT64, LZ_UINT32, LZ_BYTE, LZ_SBYTE, LZ_INT16, LZ_UINT16,
    LZ_DOUBLE, LZ_SINGLE, LZ_DECIMAL, LZ_BOOL, LZ_CHAR, LZ_DELEGATE, LZ_ACTION, LZ_FUNC, LZ_KVP,
    LZ_CONSOLE, LZ_RT, LZ_CONVERT, LZ_MATH, LZ_MATHF, LZ_ENV, LZ_THREAD, LZ_GC, LZ_DEBUG,
    LZ_STOPWATCH, LZ_RANDOM
};
static const mcs_lib_entry_t core_entries[] = {
    { "Object", LZ_OBJECT, MCS_LIB_CORE }, { "Type", LZ_TYPE, MCS_LIB_CORE },
    { "Int32", LZ_INT, MCS_LIB_CORE }, { "Int64", LZ_INT64, MCS_LIB_CORE }, { "UInt32", LZ_UINT32, MCS_LIB_CORE },
    { "Byte", LZ_BYTE, MCS_LIB_CORE }, { "SByte", LZ_SBYTE, MCS_LIB_CORE },
    { "Int16", LZ_INT16, MCS_LIB_CORE }, { "UInt16", LZ_UINT16, MCS_LIB_CORE },
    { "Double", LZ_DOUBLE, MCS_LIB_CORE }, { "Single", LZ_SINGLE, MCS_LIB_CORE }, { "Decimal", LZ_DECIMAL, MCS_LIB_CORE },
    { "Boolean", LZ_BOOL, MCS_LIB_CORE }, { "Char", LZ_CHAR, MCS_LIB_CORE },
    { "Delegate", LZ_DELEGATE, MCS_LIB_CORE }, { "Action", LZ_ACTION, MCS_LIB_CORE }, { "Func", LZ_FUNC, MCS_LIB_CORE },
    { "KeyValuePair", LZ_KVP, MCS_LIB_CORE },
    { "Console", LZ_CONSOLE, MCS_LIB_CORE }, { "__rt", LZ_RT, MCS_LIB_CORE },
#if MCS_ENABLE_CONVERT
    { "Convert", LZ_CONVERT, MCS_LIB_CORE },
#endif
    { "Math", LZ_MATH, MCS_LIB_MATH }, { "MathF", LZ_MATHF, MCS_LIB_MATH },
    { "Environment", LZ_ENV, MCS_LIB_SYSTEM }, { "Thread", LZ_THREAD, MCS_LIB_SYSTEM },
#if MCS_ENABLE_DIAGNOSTICS
    { "GC", LZ_GC, MCS_LIB_SYSTEM }, { "Debug", LZ_DEBUG, MCS_LIB_SYSTEM },
    { "Stopwatch", LZ_STOPWATCH, MCS_LIB_SYSTEM },
#endif
#if MCS_ENABLE_RANDOM
    { "Random", LZ_RANDOM, MCS_LIB_SYSTEM },
#endif
    { NULL, 0, 0 }
};

static void core_make(mcs_vm_t* vm, int id) {
    mcs_class_t* c;
    switch (id) {
    case LZ_OBJECT:
        if (vm->cls_object) return;
        vm->cls_object = c = mcs_define_builtin_class(vm, "Object", NULL, CLS_BUILTIN);
        mcs_add_regs(vm, c, object_methods, false);
        mcs_add_regs(vm, c, object_statics, true);
        return;
    case LZ_TYPE:
        c = mcs_define_builtin_class(vm, "Type", MCS_CLS(vm, object), CLS_BUILTIN);
        vm->gc_pause++;
        mcs_class_add_field(vm, c, mcs_intern_c(vm, "Name"), mcs_null());
        mcs_class_add_field(vm, c, mcs_intern_c(vm, "FullName"), mcs_null());
        vm->gc_pause--;
        mcs_add_regs(vm, c, type_methods, false);
        mcs_add_regs(vm, c, type_statics, true);
        return;
    case LZ_INT:
        if (vm->cls_int) return;
        vm->cls_int = c = mcs_define_builtin_class(vm, "Int32", MCS_CLS(vm, object), CLS_BUILTIN);
        mcs_add_regs(vm, c, value_methods, false);
        mcs_add_regs(vm, c, int_statics, true);
        set_static(vm, c, "MaxValue", mcs_int(INT32_MAX));
        set_static(vm, c, "MinValue", mcs_int(INT32_MIN));
        return;
#if MCS_INT64
    case LZ_INT64:
        c = mcs_define_builtin_class(vm, "Int64", MCS_CLS(vm, int), CLS_BUILTIN);
        set_static(vm, c, "MaxValue", mcs_int(INT64_MAX));
        set_static(vm, c, "MinValue", mcs_int(INT64_MIN));
        return;
    case LZ_UINT32:
        c = mcs_define_builtin_class(vm, "UInt32", MCS_CLS(vm, int), CLS_BUILTIN);
        set_static(vm, c, "MaxValue", mcs_int(UINT32_MAX));
        set_static(vm, c, "MinValue", mcs_int(0));
        return;
#else
    case LZ_INT64: alias_global(vm, "Int64", MCS_CLS(vm, int)); return;
    case LZ_UINT32: alias_global(vm, "UInt32", MCS_CLS(vm, int)); return;
#endif
    case LZ_BYTE: small_int_class(vm, "Byte", byte_parse, 0, 255); return;
    case LZ_SBYTE: small_int_class(vm, "SByte", sbyte_parse, -128, 127); return;
    case LZ_INT16: small_int_class(vm, "Int16", short_parse, -32768, 32767); return;
    case LZ_UINT16: small_int_class(vm, "UInt16", ushort_parse, 0, 65535); return;
    case LZ_DOUBLE:
        if (vm->cls_float) return;
        vm->cls_float = c = mcs_define_builtin_class(vm, "Double", MCS_CLS(vm, object), CLS_BUILTIN);
        mcs_add_regs(vm, c, value_methods, false);
#if MCS_ENABLE_FLOAT
        mcs_add_regs(vm, c, dbl_statics, true);
#if MCS_FLOAT_DOUBLE
        set_static(vm, c, "MaxValue", mcs_float(1.7976931348623157e308));
        set_static(vm, c, "MinValue", mcs_float(-1.7976931348623157e308));
        set_static(vm, c, "Epsilon", mcs_float(4.9406564584124654e-324));
#else
        set_static(vm, c, "MaxValue", mcs_float(3.40282347e+38f));
        set_static(vm, c, "MinValue", mcs_float(-3.40282347e+38f));
        set_static(vm, c, "Epsilon", mcs_float(1.401298E-45f));
#endif
        set_static(vm, c, "NaN", mcs_float((mcs_float_t)NAN));
        set_static(vm, c, "PositiveInfinity", mcs_float((mcs_float_t)INFINITY));
        set_static(vm, c, "NegativeInfinity", mcs_float(-(mcs_float_t)INFINITY));
#endif
        return;
    case LZ_SINGLE: alias_global(vm, "Single", MCS_CLS(vm, float)); return;
    case LZ_DECIMAL: alias_global(vm, "Decimal", MCS_CLS(vm, float)); return;
    case LZ_BOOL:
        if (vm->cls_bool) return;
        vm->cls_bool = c = mcs_define_builtin_class(vm, "Boolean", MCS_CLS(vm, object), CLS_BUILTIN);
        mcs_add_regs(vm, c, value_methods, false);
        mcs_add_regs(vm, c, bool_statics, true);
        set_static(vm, c, "TrueString", lib_cstr(vm, "True"));
        set_static(vm, c, "FalseString", lib_cstr(vm, "False"));
        return;
    case LZ_CHAR:
        if (vm->cls_char) return;
        vm->cls_char = c = mcs_define_builtin_class(vm, "Char", MCS_CLS(vm, object), CLS_BUILTIN);
        mcs_add_regs(vm, c, value_methods, false);
        mcs_add_regs(vm, c, char_statics, true);
        set_static(vm, c, "MinValue", mcs_char(0));
        set_static(vm, c, "MaxValue", mcs_char(0xFFFF));
        return;
    case LZ_DELEGATE:
        if (vm->cls_delegate) return;
        vm->cls_delegate = c = mcs_define_builtin_class(vm, "Delegate", MCS_CLS(vm, object), CLS_BUILTIN);
        mcs_add_regs(vm, c, delegate_methods, false);
        return;
    case LZ_ACTION: alias_global(vm, "Action", MCS_CLS(vm, delegate)); return;
    case LZ_FUNC: alias_global(vm, "Func", MCS_CLS(vm, delegate)); return;
    case LZ_KVP:
        if (vm->cls_kvp) return;
        vm->cls_kvp = c = mcs_define_builtin_class(vm, "KeyValuePair", MCS_CLS(vm, object), CLS_BUILTIN);
        vm->gc_pause++;
        mcs_class_add_field(vm, c, vm->s_key, mcs_null());   /* slot 0 */
        mcs_class_add_field(vm, c, vm->s_value, mcs_null()); /* slot 1 */
        vm->gc_pause--;
        c->native_ctor = kvp_new;
        mcs_add_regs(vm, c, kvp_methods, false);
        return;
    case LZ_CONSOLE: mcs_add_regs(vm, module_for(vm, "Console"), console_fns, true); return;
    case LZ_RT: mcs_add_regs(vm, module_for(vm, "__rt"), rt_fns, true); return;
#if MCS_ENABLE_CONVERT
    case LZ_CONVERT:
        c = module_for(vm, "Convert");
        mcs_add_regs(vm, c, convert_fns, true);
#if MCS_ENABLE_BYTES
        mcs_add_regs(vm, c, mcs_lib_convert_bytes_fns, true);
#endif
        return;
#endif
    case LZ_MATH:
        c = module_for(vm, "Math");
        mcs_add_regs(vm, c, math_fns, true);
#if MCS_ENABLE_FLOAT
        set_static(vm, c, "PI", mcs_float((mcs_float_t)3.14159265358979323846));
        set_static(vm, c, "E", mcs_float((mcs_float_t)2.7182818284590452354));
        set_static(vm, c, "Tau", mcs_float((mcs_float_t)6.28318530717958647692));
#endif
        return;
    case LZ_MATHF: c = lib_global_class(vm, "Math"); if (c) alias_global(vm, "MathF", c); return;
    case LZ_ENV:
        c = module_for(vm, "Environment");
        mcs_add_regs(vm, c, env_fns, true);
        set_static(vm, c, "NewLine", lib_cstr(vm, "\n"));
        return;
    case LZ_THREAD: mcs_add_regs(vm, module_for(vm, "Thread"), thread_fns, true); return;
#if MCS_ENABLE_DIAGNOSTICS
    case LZ_GC: mcs_add_regs(vm, module_for(vm, "GC"), gc_fns, true); return;
    case LZ_DEBUG: mcs_add_regs(vm, module_for(vm, "Debug"), debug_fns, true); return;
    case LZ_STOPWATCH: define_native_class(vm, &stopwatch_def); return;
#endif
#if MCS_ENABLE_RANDOM
    case LZ_RANDOM: define_native_class(vm, &random_def); return;
#endif
    default: return;
    }
}

/* the areas of the standard library, each a flash table + maker */
static const struct { const mcs_lib_entry_t* tab; void (*make)(mcs_vm_t*, int); } lib_areas[] = {
    { core_entries, core_make },
    { mcs_lib_str_entries, mcs_lib_str_make },
    { mcs_lib_coll_entries, mcs_lib_coll_make },
    { mcs_lib_bytes_entries, mcs_lib_bytes_make },
};
#define LIB_AREAS ((int)(sizeof lib_areas / sizeof lib_areas[0]))

static bool name_is(const char* a, const char* b, size_t len) { return strncmp(a, b, len) == 0 && a[len] == 0; }
static uint8_t lib_mask(mcs_vm_t* vm) { return (uint8_t)(vm->cfg.stdlib | MCS_LIB_CORE); }

static const mcs_lib_entry_t* lib_find(mcs_vm_t* vm, const char* name, size_t len, int* area) {
    uint8_t mask = lib_mask(vm);
    for (int a = 0; a < LIB_AREAS; a++)
        for (const mcs_lib_entry_t* e = lib_areas[a].tab; e->name; e++)
            if ((e->mask & mask) && name_is(e->name, name, len)) { *area = a; return e; }
    return NULL;
}
static int exc_find(const char* name, size_t len) {
    if (len < 9 || memcmp(name + len - 9, "Exception", 9) != 0) return -1;
    for (int i = 0; i < EXC_TBL_N; i++) if (name_is(exc_tbl[i].name, name, len)) return i;
    return -1;
}

static const char* const core_names[MCS_CORE__COUNT] = {
    "Object", "String", "Int32", "Double", "Boolean", "Char", "Array", "List", "Dictionary", "KeyValuePair", "Delegate"
};
mcs_class_t* mcs_core_class(mcs_vm_t* vm, int which) {
    mcs_class_t** slot = &vm->cls_object + which;
    if (!*slot) {
        int area;
        const char* n = core_names[which];
        const mcs_lib_entry_t* e = lib_find(vm, n, strlen(n), &area);
        vm->gc_pause++;
        if (e) lib_areas[area].make(vm, e->id);
        vm->gc_pause--;
        if (!*slot) mcs_panic(vm, MCS_ERR_MEMORY, "built-in class missing");
    }
    return *slot;
}

bool mcs_lazy_known(mcs_vm_t* vm, const char* name, size_t len) {
    int area;
    if (lib_find(vm, name, len, &area) || exc_find(name, len) >= 0) return true;
    for (mcs_lazy_t* z = vm->lazy; z; z = z->next) if (name_is(z->name, name, len)) return true;
    return false;
}

bool mcs_lazy_resolve(mcs_vm_t* vm, const char* name, size_t len) {
    bool any = false;
    int area, x;
    vm->gc_pause++;
    const mcs_lib_entry_t* e = lib_find(vm, name, len, &area);
    if (e) { lib_areas[area].make(vm, e->id); any = true; }
    else if ((x = exc_find(name, len)) >= 0) { make_exception(vm, x); any = true; }
    /* registrations from modules / the host, in the order they were made */
    mcs_lazy_t** pp = &vm->lazy;
    while (*pp) {
        mcs_lazy_t* z = *pp;
        if (!name_is(z->name, name, len)) { pp = &z->next; continue; }
        *pp = z->next;   /* unlink first: applying may resolve other names */
        if (z->def) define_native_class(vm, z->def);
        else {
            mcs_class_t* c = module_for(vm, z->name);
            if (z->regs) mcs_add_regs(vm, c, z->regs, true);
            add_consts(vm, c, z->consts);
        }
        mcs_realloc(vm, z, sizeof(mcs_lazy_t) + strlen(z->name), 0);
        any = true;
    }
    vm->gc_pause--;
    return any;
}

void mcs_lazy_free(mcs_vm_t* vm) {
    while (vm->lazy) {
        mcs_lazy_t* z = vm->lazy;
        vm->lazy = z->next;
        mcs_realloc(vm, z, sizeof(mcs_lazy_t) + strlen(z->name), 0);
    }
}

void mcs_open_libs(mcs_vm_t* vm, uint8_t mask) {
    vm->cfg.stdlib = (uint8_t)(mask | MCS_LIB_CORE);
#if MCS_LAZY_CLASSES
    /* nothing to do: every class is created by its first use */
#else
    for (int a = 0; a < LIB_AREAS; a++)
        for (const mcs_lib_entry_t* e = lib_areas[a].tab; e->name; e++)
            if (e->mask & lib_mask(vm)) mcs_lazy_resolve(vm, e->name, strlen(e->name));
    for (int i = 0; i < EXC_TBL_N; i++) make_exception(vm, i);
#endif
}
