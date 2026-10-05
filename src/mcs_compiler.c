/* MicroCS compiler: AST -> stack bytecode. */
#include "mcs_front.h"
#if MCS_ENABLE_COMPILER
#include <stdio.h>
#include <stdlib.h>

enum { FK_MAIN, FK_FUNC, FK_METHOD, FK_STATIC, FK_CTOR, FK_LAMBDA };

typedef struct { const char* name; uint32_t len; int depth; bool captured; uint8_t pt, conv; } local_t;
typedef struct { uint8_t index, is_local; } upv_t;

typedef struct jlist { uint32_t* at; int n, cap; } jlist_t;

typedef struct loopctx {
    struct loopctx* prev;
    int depth;            /* scope depth outside the loop body */
    int try_depth;
    int64_t start;        /* continue target or -1 (forward) */
    bool is_switch;
    jlist_t breaks, conts;
} loop_t;

typedef struct tryctx { struct tryctx* prev; node_t* fin; } try_t;

typedef struct fcomp {
    struct fcomp* enc;
    mcs_function_t* fn;
    int kind;
    local_t locals[256];
    int nlocals, depth, max_locals;
    upv_t upv[256];
    int nupv;
    classdecl_t* cls;
    bool is_static;
    loop_t* loop;
    try_t* tries;
    int try_depth;
    typeref_t* ret;
    int exc_slot;         /* hidden exception local for `throw;` */
    int nrefs;            /* ref/out parameters: cell slot + working local, written back on return */
    uint8_t ref_cell[16], ref_local[16];
} fcomp_t;

typedef struct gvar { const char* name; uint32_t len; uint8_t pt, conv; struct gvar* next; } gvar_t;

typedef struct {
    front_ctx_t* ctx;
    mcs_vm_t* vm;
    gvar_t* gvars;        /* types of top-level variables */
    program_t* prog;
    fcomp_t* fc;
    jmp_buf jb;
    uint32_t line;
    mcs_string_t* src_name;
    classdecl_t* cur_enum;
    uint8_t arr_conv;     /* element conversion for array literals */
    bool force_global;    /* top-level var whose initializer declares pattern locals */
    bool outvar_global;   /* top-level statement: `out var x` declares a global */
    struct fcomp* fc_free; /* recycled function-compiler states (each is ~4-6 KB) */
} comp_t;

static uint8_t compile_expr(comp_t* c, node_t* n);
static void compile_stmt(comp_t* c, node_t* n);
static void compile_block_list(comp_t* c, node_t* list);
static void emit_closure(comp_t* c, funcdecl_t* f, int kind, classdecl_t* cls, const char* name, uint32_t nlen, member_t* ctor);

/* ------------------------------------------------------------ errors */
static void cerr(comp_t* c, uint32_t line, const char* fmt, ...) {
    char msg[200];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    mcs_front_error(c->ctx, line ? line : c->line, 1, "%s", msg);
    longjmp(c->jb, 1);
}

/* ------------------------------------------------------------ emitting */
static mcs_function_t* FN(comp_t* c) { return c->fc->fn; }
static void emit(comp_t* c, uint8_t b) { mcs_fn_emit(c->vm, FN(c), b, c->line); }
static void emit_u16(comp_t* c, uint32_t v) { if (v > 0xFFFF) cerr(c, 0, "function too large"); emit(c, (uint8_t)(v >> 8)); emit(c, (uint8_t)v); }
static void emit_op(comp_t* c, uint8_t op) { emit(c, op); }
static void emit_op8(comp_t* c, uint8_t op, uint8_t a) { emit(c, op); emit(c, a); }
static void emit_op16(comp_t* c, uint8_t op, uint32_t a) { emit(c, op); emit_u16(c, a); }
static uint32_t here(comp_t* c) { return FN(c)->code_len; }
static uint32_t emit_jump(comp_t* c, uint8_t op) { emit(c, op); emit(c, 0xff); emit(c, 0xff); return here(c) - 2; }
static void patch_at(comp_t* c, uint32_t at) {
    uint32_t off = here(c) - at - 2;
    if (off > 0xFFFF) cerr(c, 0, "jump too large");
    FN(c)->code[at] = (uint8_t)(off >> 8); FN(c)->code[at + 1] = (uint8_t)off;
}
static void emit_loop(comp_t* c, uint32_t start) {
    emit(c, OP_LOOP);
    uint32_t off = here(c) - start + 2;
    if (off > 0xFFFF) cerr(c, 0, "loop body too large");
    emit(c, (uint8_t)(off >> 8)); emit(c, (uint8_t)off);
}
static void jl_add(comp_t* c, jlist_t* j, uint32_t at) {
    if (j->n == j->cap) {
        int nc = j->cap ? j->cap * 2 : 8;
        uint32_t* na = (uint32_t*)arena_alloc(c->ctx->arena, sizeof(uint32_t) * (size_t)nc);
        if (j->n) memcpy(na, j->at, sizeof(uint32_t) * (size_t)j->n);
        j->at = na; j->cap = nc;
    }
    j->at[j->n++] = at;
}
static void jl_patch(comp_t* c, jlist_t* j) { for (int i = 0; i < j->n; i++) patch_at(c, j->at[i]); j->n = 0; }

static uint32_t konst(comp_t* c, mcs_value_t v) { return mcs_fn_add_const(c->vm, FN(c), v); }
static mcs_string_t* istr(comp_t* c, const char* s, uint32_t n) { return mcs_intern(c->vm, s, n); }
static uint32_t kstr(comp_t* c, const char* s, uint32_t n) { return konst(c, OBJ_VAL(istr(c, s, n))); }
static uint32_t kcstr(comp_t* c, const char* s) { return kstr(c, s, (uint32_t)strlen(s)); }

static void emit_value(comp_t* c, mcs_value_t v) {
    switch (v.type) {
    case MCS_T_NULL: emit_op(c, OP_NULL); return;
    case MCS_T_BOOL: emit_op(c, v.as.b ? OP_TRUE : OP_FALSE); return;
    case MCS_T_INT: if (v.as.i >= -128 && v.as.i <= 127) { emit_op8(c, OP_INT8, (uint8_t)(int8_t)v.as.i); return; } break;
    default: break;
    }
    emit_op16(c, OP_CONST, konst(c, v));
}

static uint8_t pt_of_value(mcs_value_t v) {
    switch (v.type) {
    case MCS_T_INT: return PT_INT; case MCS_T_FLOAT: return PT_FLOAT; case MCS_T_BOOL: return PT_BOOL;
    case MCS_T_CHAR: return PT_CHAR;
    case MCS_T_OBJ: return IS_STRING(v) ? PT_STRING : PT_ANY;
    default: return PT_ANY;
    }
}

static mcs_value_t default_for(typeref_t* t) {
    if (!t || t->rank) return mcs_null();
    switch (t->prim) {
    case PT_INT: return mcs_int(0);
#if MCS_ENABLE_FLOAT
    case PT_FLOAT: return mcs_float(0);
#endif
    case PT_BOOL: return mcs_bool(false);
    case PT_CHAR: return mcs_char(0);
    default: return mcs_null();
    }
}

/* conversion to apply when storing a value of type `src` into `dst_conv` */
static void emit_store_conv(comp_t* c, uint8_t conv, uint8_t src) {
    switch (conv) {
    case CV_FLOAT: if (src != PT_FLOAT) emit_op8(c, OP_CONV, CV_FLOAT); break;
    case CV_INT: if (src != PT_INT) emit_op8(c, OP_CONV, CV_INT); break;
    case CV_CHAR: if (src != PT_CHAR) emit_op8(c, OP_CONV, CV_CHAR); break;
    case CV_BYTE: case CV_SBYTE: case CV_SHORT: case CV_USHORT: emit_op8(c, OP_CONV, conv); break;
    default: break;
    }
}
static uint8_t conv_of(typeref_t* t) {
    if (!t || t->rank || t->is_var) return 0xff;
    if (t->conv == CV_UINT) return CV_INT;
    if (t->conv == CV_BOOL) return 0xff;
    return t->conv;
}
static uint8_t pt_of_type(typeref_t* t) { if (!t || t->rank || t->is_var) return PT_ANY; return t->prim; }

/* ------------------------------------------------------------ scopes */
static bool name_eq(const char* a, uint32_t al, const char* b, uint32_t bl) { return al == bl && memcmp(a, b, al) == 0; }

static int declare_local(comp_t* c, const char* name, uint32_t len, uint8_t pt, uint8_t conv) {
    fcomp_t* f = c->fc;
    if (f->nlocals >= 255) cerr(c, 0, "too many local variables in function");
    local_t* l = &f->locals[f->nlocals];
    l->name = name; l->len = len; l->depth = f->depth; l->captured = false; l->pt = pt; l->conv = conv;
    f->nlocals++;
    if (f->nlocals > f->max_locals) f->max_locals = f->nlocals;
    return f->nlocals - 1;
}
static void begin_scope(comp_t* c) { c->fc->depth++; }
static void end_scope(comp_t* c) {
    fcomp_t* f = c->fc;
    f->depth--;
    while (f->nlocals > 0 && f->locals[f->nlocals - 1].depth > f->depth) {
        emit_op(c, f->locals[f->nlocals - 1].captured ? OP_CLOSE_UPVAL : OP_POP);
        f->nlocals--;
    }
}
/* emit pops for locals deeper than `depth` without forgetting them */
static void pop_to_depth(comp_t* c, int depth) {
    fcomp_t* f = c->fc;
    for (int i = f->nlocals - 1; i >= 0 && f->locals[i].depth > depth; i--)
        emit_op(c, f->locals[i].captured ? OP_CLOSE_UPVAL : OP_POP);
}

static int resolve_local(fcomp_t* f, const char* name, uint32_t len) {
    for (int i = f->nlocals - 1; i >= 0; i--)
        if (name_eq(f->locals[i].name, f->locals[i].len, name, len)) return i;
    return -1;
}
static int add_upvalue(comp_t* c, fcomp_t* f, uint8_t index, bool is_local) {
    for (int i = 0; i < f->nupv; i++) if (f->upv[i].index == index && f->upv[i].is_local == is_local) return i;
    if (f->nupv >= 255) cerr(c, 0, "too many captured variables");
    f->upv[f->nupv].index = index; f->upv[f->nupv].is_local = is_local;
    return f->nupv++;
}
static int resolve_upvalue(comp_t* c, fcomp_t* f, const char* name, uint32_t len, uint8_t* pt, uint8_t* conv) {
    if (!f->enc) return -1;
    int l = resolve_local(f->enc, name, len);
    if (l >= 0) {
        f->enc->locals[l].captured = true;
        *pt = f->enc->locals[l].pt; *conv = f->enc->locals[l].conv;
        return add_upvalue(c, f, (uint8_t)l, true);
    }
    int u = resolve_upvalue(c, f->enc, name, len, pt, conv);
    if (u >= 0) return add_upvalue(c, f, (uint8_t)u, false);
    return -1;
}

/* ------------------------------------------------------------ declarations lookup */
static classdecl_t* find_class(comp_t* c, const char* name, uint32_t len) {
    for (classdecl_t* d = c->prog->classes; d; d = d->next) if (name_eq(d->name, d->len, name, len)) return d;
    return NULL;
}
static member_t* find_member(classdecl_t* cls, const char* name, uint32_t len, classdecl_t** owner) {
    for (classdecl_t* k = cls; k; k = k->base_decl) {
        for (member_t* m = k->members; m; m = m->next) {
            if (m->kind == M_CTOR || m->kind == M_INDEXER || m->kind == M_OPERATOR) continue;
            const char* mn = m->kind == M_METHOD ? m->fn->name : m->name;
            uint32_t ml = m->kind == M_METHOD ? m->fn->len : m->len;
            if (name_eq(mn, ml, name, len)) { if (owner) *owner = k; return m; }
        }
    }
    return NULL;
}
static bool class_has_native_base(classdecl_t* cls) {
    for (classdecl_t* k = cls; k; k = k->base_decl) if (!k->base_decl && k->base) return true;
    return false;
}
static bool is_toplevel_name(comp_t* c, const char* name, uint32_t len) {
    if (find_class(c, name, len)) return true;
    for (node_t* s = c->prog->stmts; s; s = s->next) {
        if ((s->kind == N_LOCAL_FUNC || s->kind == N_VAR) && name_eq(s->name, s->len, name, len)) return true;
        if (s->kind == N_BLOCK && s->flag == 1) for (node_t* v = s->a; v; v = v->next) if (v->kind == N_VAR && name_eq(v->name, v->len, name, len)) return true;
    }
    mcs_string_t* str = mcs_table_find_string(&c->vm->strings, name, len, mcs_hash_bytes(name, len));
    if (str) { mcs_value_t slot; if (mcs_table_get_s(&c->vm->global_index, str, &slot)) return true; }
    return false;
}
static uint32_t global_slot(comp_t* c, const char* name, uint32_t len) {
    uint32_t s = mcs_global_slot(c->vm, istr(c, name, len));
    if (s > 0xFFFF) cerr(c, 0, "too many globals");
    return s;
}

/* ------------------------------------------------------------ constant folding */
static bool fold(comp_t* c, node_t* n, mcs_value_t* out);

static bool enum_value(comp_t* c, classdecl_t* e, const char* name, uint32_t len, mcs_value_t* out) {
    MCS_UNUSED(c);
    for (enumval_t* v = e->enums; v; v = v->next)
        if (name_eq(v->name, v->len, name, len)) { *out = mcs_int(v->computed); return true; }
    return false;
}

static bool const_member(comp_t* c, classdecl_t* cls, const char* name, uint32_t len, mcs_value_t* out) {
    if (!cls) return false;
    if (cls->kind == C_ENUM) return enum_value(c, cls, name, len, out);
    member_t* m = find_member(cls, name, len, NULL);
    if (m && m->kind == M_CONST && m->init) {
        classdecl_t* save = c->cur_enum; c->cur_enum = NULL;
        bool ok = fold(c, m->init, out);
        c->cur_enum = save;
#if MCS_ENABLE_FLOAT
        if (ok && m->type && m->type->prim == PT_FLOAT && out->type == MCS_T_INT) *out = mcs_float((mcs_float_t)out->as.i);
#endif
        return ok;
    }
    return false;
}

static bool name_is_variable(comp_t* c, const char* name, uint32_t len) {
    for (fcomp_t* f = c->fc; f; f = f->enc) if (resolve_local(f, name, len) >= 0) return true;
    return false;
}

static bool fold(comp_t* c, node_t* n, mcs_value_t* out) {
    switch (n->kind) {
    case N_INT: *out = mcs_int(n->ival); return true;
#if MCS_ENABLE_FLOAT
    case N_FLOAT: *out = mcs_float(n->fval); return true;
#endif
    case N_CHAR: *out = mcs_char((uint32_t)n->ival); return true;
    case N_BOOL: *out = mcs_bool(n->ival != 0); return true;
    case N_STR: *out = OBJ_VAL(istr(c, n->name, n->len)); return true;
    case N_NAME:
        if (name_is_variable(c, n->name, n->len)) return false;
        if (c->cur_enum && enum_value(c, c->cur_enum, n->name, n->len, out)) return true;
        if (c->fc && c->fc->cls && const_member(c, c->fc->cls, n->name, n->len, out)) return true;
        return false;
    case N_MEMBER:
        if (n->a->kind == N_NAME && !name_is_variable(c, n->a->name, n->a->len)) {
            classdecl_t* d = find_class(c, n->a->name, n->a->len);
            if (d && const_member(c, d, n->name, n->len, out)) return true;
        }
        return false;
    case N_CAST: {
        mcs_value_t v;
        if (!fold(c, n->a, &v)) return false;
        uint8_t cv = conv_of(n->type);
        if (n->type->prim == PT_ANY) { classdecl_t* d = find_class(c, n->type->name, n->type->len); if (d && d->kind == C_ENUM) { *out = v; return true; } return false; }
        if (cv == CV_INT && (v.type == MCS_T_INT || v.type == MCS_T_CHAR)) { *out = mcs_int(v.as.i); return true; }
#if MCS_ENABLE_FLOAT
        if (cv == CV_INT && v.type == MCS_T_FLOAT) { *out = mcs_int((mcs_int_t)v.as.f); return true; }
        if (cv == CV_FLOAT && v.type == MCS_T_INT) { *out = mcs_float((mcs_float_t)v.as.i); return true; }
        if (cv == CV_FLOAT && v.type == MCS_T_FLOAT) { *out = v; return true; }
#endif
        if (cv == CV_CHAR && (v.type == MCS_T_INT || v.type == MCS_T_CHAR)) { *out = mcs_char((uint32_t)v.as.i); return true; }
        return false;
    }
    case N_UNARY: {
        mcs_value_t v;
        if (!fold(c, n->a, &v)) return false;
        if (n->op == TK_MINUS && v.type == MCS_T_INT) { *out = mcs_int((mcs_int_t)(0 - (mcs_uint_t)v.as.i)); return true; }
#if MCS_ENABLE_FLOAT
        if (n->op == TK_MINUS && v.type == MCS_T_FLOAT) { *out = mcs_float(-v.as.f); return true; }
#endif
        if (n->op == TK_TILDE && v.type == MCS_T_INT) { *out = mcs_int(~v.as.i); return true; }
        if (n->op == TK_BANG && v.type == MCS_T_BOOL) { *out = mcs_bool(!v.as.b); return true; }
        return false;
    }
    case N_BINARY: {
        mcs_value_t a, b;
        if (!fold(c, n->a, &a) || !fold(c, n->b, &b)) return false;
        if ((a.type == MCS_T_INT || a.type == MCS_T_CHAR) && (b.type == MCS_T_INT || b.type == MCS_T_CHAR) &&
            !(n->op == TK_PLUS && (a.type == MCS_T_CHAR) && b.type == MCS_T_CHAR && 0)) {
            mcs_uint_t x = (mcs_uint_t)a.as.i, y = (mcs_uint_t)b.as.i;
            mcs_int_t r;
            switch (n->op) {
            case TK_PLUS: r = (mcs_int_t)(x + y); break;
            case TK_MINUS: r = (mcs_int_t)(x - y); break;
            case TK_STAR: r = (mcs_int_t)(x * y); break;
            case TK_SLASH: if (b.as.i == 0) return false; r = a.as.i / b.as.i; break;
            case TK_PERCENT: if (b.as.i == 0) return false; r = a.as.i % b.as.i; break;
            case TK_AMP: r = (mcs_int_t)(x & y); break;
            case TK_PIPE: r = (mcs_int_t)(x | y); break;
            case TK_CARET: r = (mcs_int_t)(x ^ y); break;
            case TK_SHL: r = (mcs_int_t)(x << (y & (sizeof(mcs_int_t) * 8 - 1))); break;
            case 250: r = a.as.i >> (y & (sizeof(mcs_int_t) * 8 - 1)); break;
            default: return false;
            }
            *out = mcs_int(r);
            return true;
        }
#if MCS_ENABLE_FLOAT
        if ((a.type == MCS_T_FLOAT || a.type == MCS_T_INT) && (b.type == MCS_T_FLOAT || b.type == MCS_T_INT)) {
            mcs_float_t x = a.type == MCS_T_FLOAT ? a.as.f : (mcs_float_t)a.as.i;
            mcs_float_t y = b.type == MCS_T_FLOAT ? b.as.f : (mcs_float_t)b.as.i;
            switch (n->op) {
            case TK_PLUS: *out = mcs_float(x + y); return true;
            case TK_MINUS: *out = mcs_float(x - y); return true;
            case TK_STAR: *out = mcs_float(x * y); return true;
            case TK_SLASH: *out = mcs_float(x / y); return true;
            default: return false;
            }
        }
#endif
        return false;
    }
    default: return false;
    }
}

/* ------------------------------------------------------------ name resolution */
enum { R_LOCAL, R_UPVAL, R_THIS_MEMBER, R_STATIC_MEMBER, R_GLOBAL };
typedef struct { int kind; int index; uint8_t pt, conv; classdecl_t* owner; member_t* m; } res_t;

static bool in_instance_ctx(comp_t* c) {
    for (fcomp_t* f = c->fc; f; f = f->enc) {
        if (f->kind == FK_METHOD || f->kind == FK_CTOR) return true;
        if (f->kind != FK_LAMBDA) return false;
    }
    return false;
}

static res_t resolve(comp_t* c, const char* name, uint32_t len) {
    res_t r; memset(&r, 0, sizeof r);
    r.pt = PT_ANY; r.conv = 0xff;
    int l = resolve_local(c->fc, name, len);
    if (l >= 0) { r.kind = R_LOCAL; r.index = l; r.pt = c->fc->locals[l].pt; r.conv = c->fc->locals[l].conv; return r; }
    int u = resolve_upvalue(c, c->fc, name, len, &r.pt, &r.conv);
    if (u >= 0) { r.kind = R_UPVAL; r.index = u; return r; }
    classdecl_t* cls = c->fc->cls;
    if (cls) {
        classdecl_t* owner = NULL;
        member_t* m = find_member(cls, name, len, &owner);
        if (m) {
            r.m = m; r.owner = owner;
            r.kind = (m->mods & MOD_STATIC) ? R_STATIC_MEMBER : R_THIS_MEMBER;
            if (m->kind == M_FIELD || m->kind == M_PROP || m->kind == M_CONST) { r.pt = pt_of_type(m->type); r.conv = conv_of(m->type); }
            if (m->kind == M_METHOD) r.pt = pt_of_type(m->fn->ret);
            if (r.kind == R_THIS_MEMBER && !in_instance_ctx(c))
                cerr(c, 0, "an object reference is required to access instance member '%.*s'", (int)len, name);
            return r;
        }
        if (class_has_native_base(cls) && in_instance_ctx(c) && !is_toplevel_name(c, name, len)) {
            r.kind = R_THIS_MEMBER;
            return r;
        }
    }
    r.kind = R_GLOBAL;
    r.index = (int)global_slot(c, name, len);
    for (gvar_t* g = c->gvars; g; g = g->next)
        if (g->len == len && !memcmp(g->name, name, len)) { r.pt = g->pt; r.conv = g->conv; break; }
    return r;
}

static void emit_this(comp_t* c) {
    res_t r; memset(&r, 0, sizeof r);
    int l = resolve_local(c->fc, "this", 4);
    if (l >= 0) { emit_op8(c, OP_GET_LOCAL, (uint8_t)l); return; }
    uint8_t pt, conv;
    int u = resolve_upvalue(c, c->fc, "this", 4, &pt, &conv);
    if (u >= 0) { emit_op8(c, OP_GET_UPVAL, (uint8_t)u); return; }
    cerr(c, 0, "'this' is not available in a static context");
}

static void emit_class_ref(comp_t* c, classdecl_t* d) { emit_op16(c, OP_GET_GLOBAL, global_slot(c, d->name, d->len)); }

/* ------------------------------------------------------------ assignment targets */
enum { T_LOCAL, T_UPVAL, T_GLOBAL, T_FIELD, T_INDEX };
typedef struct { int kind; int index; uint32_t name_k; uint8_t conv, pt; } target_t;

static void prepare_target(comp_t* c, node_t* n, target_t* t) {
    t->conv = 0xff; t->pt = PT_ANY;
    if (n->kind == N_NAME) {
        res_t r = resolve(c, n->name, n->len);
        t->conv = r.conv; t->pt = r.pt;
        switch (r.kind) {
        case R_LOCAL: t->kind = T_LOCAL; t->index = r.index; return;
        case R_UPVAL: t->kind = T_UPVAL; t->index = r.index; return;
        case R_GLOBAL: t->kind = T_GLOBAL; t->index = r.index; return;
        case R_THIS_MEMBER: emit_this(c); t->kind = T_FIELD; t->name_k = kstr(c, n->name, n->len); return;
        case R_STATIC_MEMBER: emit_class_ref(c, r.owner); t->kind = T_FIELD; t->name_k = kstr(c, n->name, n->len); return;
        }
    }
    if (n->kind == N_MEMBER) {
        if (n->flag) cerr(c, n->line, "null-conditional member cannot be assigned");
        if (n->a->kind == N_BASE || n->a->kind == N_THIS) {
            emit_this(c);
            if (c->fc->cls) { member_t* m = find_member(c->fc->cls, n->name, n->len, NULL); if (m && (m->kind == M_FIELD || m->kind == M_PROP)) { t->conv = conv_of(m->type); t->pt = pt_of_type(m->type); } }
        } else compile_expr(c, n->a);
        t->kind = T_FIELD; t->name_k = kstr(c, n->name, n->len);
        return;
    }
    if (n->kind == N_INDEX) {
        compile_expr(c, n->a);
        compile_expr(c, n->b);
        t->kind = T_INDEX;
        return;
    }
    cerr(c, n->line, "invalid assignment target");
}
static void target_get(comp_t* c, target_t* t) {
    switch (t->kind) {
    case T_LOCAL: emit_op8(c, OP_GET_LOCAL, (uint8_t)t->index); break;
    case T_UPVAL: emit_op8(c, OP_GET_UPVAL, (uint8_t)t->index); break;
    case T_GLOBAL: emit_op16(c, OP_GET_GLOBAL, (uint32_t)t->index); break;
    case T_FIELD: emit_op(c, OP_DUP); emit_op16(c, OP_GET_FIELD, t->name_k); break;
    case T_INDEX: emit_op(c, OP_DUP2); emit_op(c, OP_GET_INDEX); break;
    }
}
static void target_set(comp_t* c, target_t* t) {
    switch (t->kind) {
    case T_LOCAL: emit_op8(c, OP_SET_LOCAL, (uint8_t)t->index); break;
    case T_UPVAL: emit_op8(c, OP_SET_UPVAL, (uint8_t)t->index); break;
    case T_GLOBAL: emit_op16(c, OP_SET_GLOBAL, (uint32_t)t->index); break;
    case T_FIELD: emit_op16(c, OP_SET_FIELD, t->name_k); break;
    case T_INDEX: emit_op(c, OP_SET_INDEX); break;
    }
}
static int target_depth(target_t* t) { return t->kind == T_FIELD ? 1 : t->kind == T_INDEX ? 2 : 0; }

static uint8_t binop_code(int tk) {
    switch (tk) {
    case TK_PLUS: return OP_ADD; case TK_MINUS: return OP_SUB; case TK_STAR: return OP_MUL;
    case TK_SLASH: return OP_DIV; case TK_PERCENT: return OP_MOD; case TK_AMP: return OP_BAND;
    case TK_PIPE: return OP_BOR; case TK_CARET: return OP_BXOR; case TK_SHL: return OP_SHL;
    case 250: return OP_SHR; case 251: return OP_USHR;
    case TK_EQ: return OP_EQ; case TK_NE: return OP_NE; case TK_LT: return OP_LT; case TK_LE: return OP_LE;
    case TK_GT: return OP_GT; case TK_GE: return OP_GE;
    default: return OP_ADD;
    }
}

static uint8_t arith_pt(int op, uint8_t a, uint8_t b) {
    if (op == TK_EQ || op == TK_NE || op == TK_LT || op == TK_LE || op == TK_GT || op == TK_GE) return PT_BOOL;
    if (op == TK_PLUS && (a == PT_STRING || b == PT_STRING)) return PT_STRING;
    bool ai = a == PT_INT || a == PT_CHAR, bi = b == PT_INT || b == PT_CHAR;
    if (ai && bi) return PT_INT;
    if ((a == PT_FLOAT && (bi || b == PT_FLOAT)) || (b == PT_FLOAT && ai)) return PT_FLOAT;
    if ((op == TK_AMP || op == TK_PIPE || op == TK_CARET) && a == PT_BOOL && b == PT_BOOL) return PT_BOOL;
    return PT_ANY;
}

static uint8_t compile_assign(comp_t* c, node_t* n, bool want) {
    target_t t;
    prepare_target(c, n->a, &t);
    uint8_t pt;
    if (n->op == TK_ASSIGN) {
        pt = compile_expr(c, n->b);
        emit_store_conv(c, t.conv, pt);
        target_set(c, &t);
    } else if (n->op == TK_QQ) {
        target_get(c, &t);
        uint32_t j = emit_jump(c, OP_JUMP_IF_NOT_NULL_KEEP);
        emit_op(c, OP_POP);
        pt = compile_expr(c, n->b);
        emit_store_conv(c, t.conv, pt);
        target_set(c, &t);
        int d = target_depth(&t);
        if (d) {
            uint32_t j2 = emit_jump(c, OP_JUMP);
            patch_at(c, j);
            for (int i = 0; i < d; i++) { emit_op(c, OP_SWAP); emit_op(c, OP_POP); }
            patch_at(c, j2);
        } else patch_at(c, j);
        pt = PT_ANY;
    } else {
        /* compound: fast path for local += small int constant as a statement */
        mcs_value_t k;
        if (!want && t.kind == T_LOCAL && (n->op == TK_PLUS || n->op == TK_MINUS) && (t.conv == 0xff || t.conv == CV_INT || t.conv == CV_FLOAT) &&
            t.pt != PT_STRING && fold(c, n->b, &k) && k.type == MCS_T_INT && k.as.i >= -127 && k.as.i <= 127) {
            int d = (int)(n->op == TK_PLUS ? k.as.i : -k.as.i);
            emit(c, OP_INC_LOCAL); emit(c, (uint8_t)t.index); emit(c, (uint8_t)(int8_t)d);
            return t.pt;
        }
        target_get(c, &t);
        uint8_t bp = compile_expr(c, n->b);
        emit_op(c, binop_code(n->op));
        pt = arith_pt(n->op, t.pt, bp);
        emit_store_conv(c, t.conv, pt);
        target_set(c, &t);
        if (t.pt != PT_ANY) pt = t.pt;
    }
    if (!want) emit_op(c, OP_POP);
    return pt;
}

static uint8_t compile_incdec(comp_t* c, node_t* n, bool prefix, bool want) {
    int delta = n->op == TK_INC ? 1 : -1;
    target_t t;
    prepare_target(c, n->a, &t);
    if (!want && t.kind == T_LOCAL && (t.conv == 0xff || t.conv == CV_INT || t.conv == CV_FLOAT || t.conv == CV_CHAR)) {
        emit(c, OP_INC_LOCAL); emit(c, (uint8_t)t.index); emit(c, (uint8_t)(int8_t)delta);
        return t.pt;
    }
    target_get(c, &t);
    bool keep_old = want && !prefix;
    if (keep_old) {
        emit_op(c, OP_DUP);
        int d = target_depth(&t);
        if (d) emit_op8(c, OP_ROT, (uint8_t)(d + 1));
    }
    emit_op8(c, OP_INT8, 1);
    emit_op(c, delta > 0 ? OP_ADD : OP_SUB);
    if (t.pt == PT_CHAR) emit_op8(c, OP_CONV, CV_CHAR);
    else emit_store_conv(c, t.conv, t.pt);
    target_set(c, &t);
    if (keep_old || !want) emit_op(c, OP_POP);
    return t.pt;
}

/* ------------------------------------------------------------ calls */
static void emit_cell_read(comp_t* c, int slot) {
    emit_op8(c, OP_GET_LOCAL, (uint8_t)slot); emit_op8(c, OP_INT8, 0); emit_op(c, OP_GET_INDEX);
}
static int compile_args(comp_t* c, node_t* args) {
    int n = 0;
    for (node_t* a = args; a; a = a->next) {
        if (a->kind == N_ARG_OUT) {
            /* out/ref argument: pass a fresh 1-element cell; the caller copies cell[0] back after the call */
            if (a->ival < 0) cerr(c, a->line, "out/ref arguments are not supported in this position (use them in a statement, declaration or condition)");
            if (a->flag == 2) compile_expr(c, a->a); else emit_op(c, OP_NULL);
            emit_op16(c, OP_ARRAY, 1);
            emit_op8(c, OP_SET_LOCAL, (uint8_t)a->ival);
        } else compile_expr(c, a);
        n++;
    }
    if (n > 255) cerr(c, 0, "too many arguments");
    return n;
}
static void emit_out_writeback(comp_t* c, node_t* args) {
    for (node_t* a = args; a; a = a->next) {
        if (a->kind != N_ARG_OUT || (!a->name && !a->a)) continue;
        node_t tmp; memset(&tmp, 0, sizeof tmp);
        node_t* tn = a->a;
        if (!tn) { tmp.kind = N_NAME; tmp.name = a->name; tmp.len = a->len; tmp.line = a->line; tn = &tmp; }
        target_t t;
        prepare_target(c, tn, &t);
        emit_cell_read(c, (int)a->ival);
        target_set(c, &t);
        emit_op(c, OP_POP);
    }
}
static void emit_invoke(comp_t* c, uint8_t op, const char* name, uint32_t len, int argc) {
    emit(c, op); emit_u16(c, kstr(c, name, len)); emit(c, (uint8_t)argc);
}

static uint8_t compile_call(comp_t* c, node_t* n) {
    node_t* f = n->a;
    if (f->kind == N_NAME) {
        res_t r = resolve(c, f->name, f->len);
        switch (r.kind) {
        case R_LOCAL: emit_op8(c, OP_GET_LOCAL, (uint8_t)r.index); break;
        case R_UPVAL: emit_op8(c, OP_GET_UPVAL, (uint8_t)r.index); break;
        case R_GLOBAL: emit_op16(c, OP_GET_GLOBAL, (uint32_t)r.index); break;
        case R_THIS_MEMBER: case R_STATIC_MEMBER: {
            if (r.kind == R_THIS_MEMBER) emit_this(c); else emit_class_ref(c, r.owner);
            int argc = compile_args(c, n->b);
            emit_invoke(c, OP_INVOKE, f->name, f->len, argc);
            return r.m && r.m->kind == M_METHOD ? pt_of_type(r.m->fn->ret) : PT_ANY;
        }
        }
        int argc = compile_args(c, n->b);
        emit_op8(c, OP_CALL, (uint8_t)argc);
        return PT_ANY;
    }
    if (f->kind == N_MEMBER) {
        if (f->a->kind == N_BASE) {
            classdecl_t* cls = c->fc->cls;
            if (!cls || !cls->base) cerr(c, f->line, "'base' used in a class without a base class");
            emit_this(c);
            int argc = compile_args(c, n->b);
            emit_op16(c, OP_GET_GLOBAL, global_slot(c, cls->base, cls->base_len));
            emit_invoke(c, OP_SUPER_INVOKE, f->name, f->len, argc);
            return PT_ANY;
        }
        uint8_t pt = PT_ANY;
        if (f->a->kind == N_THIS) {
            emit_this(c);
            if (c->fc->cls) { member_t* m = find_member(c->fc->cls, f->name, f->len, NULL); if (m && m->kind == M_METHOD) pt = pt_of_type(m->fn->ret); }
        } else if (f->a->kind == N_NAME && !name_is_variable(c, f->a->name, f->a->len) && find_class(c, f->a->name, f->a->len)) {
            classdecl_t* d = find_class(c, f->a->name, f->a->len);
            emit_class_ref(c, d);
            member_t* m = find_member(d, f->name, f->len, NULL);
            if (m && m->kind == M_METHOD) pt = pt_of_type(m->fn->ret);
        } else compile_expr(c, f->a);
        uint32_t j = 0;
        if (f->flag) j = emit_jump(c, OP_JUMP_IF_NULL_KEEP);
        int argc = compile_args(c, n->b);
        emit_invoke(c, OP_INVOKE, f->name, f->len, argc);
        if (f->flag) patch_at(c, j);
        return pt;
    }
    compile_expr(c, f);
    int argc = compile_args(c, n->b);
    emit_op8(c, OP_CALL, (uint8_t)argc);
    return PT_ANY;
}

static const char* canon_type_name(typeref_t* t, uint32_t* len) {
    if (t->rank) { *len = 5; return "Array"; }
    const char* s = NULL;
    switch (t->prim) {
    case PT_INT: s = "int"; break; case PT_FLOAT: s = "double"; break; case PT_STRING: s = "string"; break;
    case PT_BOOL: s = "bool"; break; case PT_CHAR: s = "char"; break; case PT_OBJECT: s = "object"; break;
    default: *len = t->len; return t->name;
    }
    *len = (uint32_t)strlen(s);
    return s;
}

static void compile_initializers(comp_t* c, node_t* list) {
    for (node_t* it = list; it; it = it->next) {
        c->line = it->line;
        emit_op(c, OP_DUP);
        if (it->kind == N_INIT_FIELD) {
            compile_expr(c, it->a);
            emit_op16(c, OP_SET_FIELD, kstr(c, it->name, it->len));
        } else if (it->kind == N_INIT_INDEX) {
            compile_expr(c, it->a); compile_expr(c, it->b);
            emit_op(c, OP_SET_INDEX);
        } else {
            int argc = 0;
            if (it->a && it->a->kind != N_INIT_ADD) {
                /* single expression or arg list from {a, b} */
                for (node_t* a = it->a; a; a = a->next) { compile_expr(c, a); argc++; }
            }
            emit_invoke(c, OP_INVOKE, "Add", 3, argc);
        }
        emit_op(c, OP_POP);
    }
}

static uint8_t compile_new(comp_t* c, node_t* n) {
    typeref_t* t = n->type;
    if (t->prim != PT_ANY && t->prim != PT_STRING && t->prim != PT_OBJECT) { emit_value(c, default_for(t)); return t->prim; }
    const char* name = t->name; uint32_t len = t->len;
    if (t->prim == PT_STRING) { name = "String"; len = 6; }
    if (t->prim == PT_OBJECT) { name = "Object"; len = 6; }
    classdecl_t* d = find_class(c, name, len);
    if (d && (d->kind == C_INTERFACE)) cerr(c, n->line, "cannot create an instance of interface '%.*s'", (int)len, name);
    emit_op16(c, OP_GET_GLOBAL, global_slot(c, name, len));
    int argc = compile_args(c, n->a);
    emit_op8(c, OP_CALL, (uint8_t)argc);
    emit_out_writeback(c, n->a);
    if (n->c) compile_initializers(c, n->c);
    return t->prim == PT_STRING ? PT_STRING : PT_ANY;
}

static uint8_t array_kind(typeref_t* t, bool jagged) {
    if (jagged) return CV_NULL;
    switch (t->prim) {
    case PT_INT: return CV_INT; case PT_FLOAT: return CV_FLOAT; case PT_BOOL: return CV_BOOL; case PT_CHAR: return CV_CHAR;
    default: return CV_NULL;
    }
}

static void compile_array_lit(comp_t* c, node_t* n) {
    int count = 0;
    uint8_t conv = c->arr_conv;
    for (node_t* e = n->a; e; e = e->next) {
        c->arr_conv = 0xff;
        uint8_t pt = compile_expr(c, e);
        if (conv != 0xff) emit_store_conv(c, conv, pt);
        count++;
    }
    c->arr_conv = conv;
    emit_op16(c, OP_ARRAY, (uint32_t)count);
}

/* ------------------------------------------------------------ expressions */
static void compile_pattern_test(comp_t* c, node_t* pat) {
    /* subject is on stack top (kept); pushes bool */
    emit_op(c, OP_DUP);
    if (pat->kind == N_IS && !pat->a) { compile_expr(c, pat); return; }
    if (pat->kind == N_BINARY && pat->b == NULL) {
        compile_expr(c, pat->a);
        emit_op(c, binop_code(pat->op));
    } else {
        compile_expr(c, pat);
        emit_op(c, OP_EQ);
    }
}

static uint8_t compile_switch_expr(comp_t* c, node_t* n) {
    compile_expr(c, n->a);
    jlist_t ends = {0};
    for (node_t* arm = n->b; arm; arm = arm->next) {
        c->line = arm->line;
        jlist_t hits = {0};
        bool always = false;
        uint32_t miss = 0;
        for (node_t* p = arm->a; p; p = p->next) {
            if (p->kind == N_EMPTY) { always = true; break; }
            compile_pattern_test(c, p);
            jl_add(c, &hits, emit_jump(c, OP_JUMP_IF_TRUE));
        }
        if (!always) miss = emit_jump(c, OP_JUMP);
        jl_patch(c, &hits);
        uint32_t guard = 0;
        if (arm->c) { compile_expr(c, arm->c); guard = emit_jump(c, OP_JUMP_IF_FALSE); }
        emit_op(c, OP_POP);
        compile_expr(c, arm->b);
        jl_add(c, &ends, emit_jump(c, OP_JUMP));
        if (arm->c) patch_at(c, guard);
        if (!always) patch_at(c, miss);
        if (always && !arm->c) break;
    }
    /* no arm matched */
    emit_op(c, OP_POP);
    emit_op16(c, OP_GET_GLOBAL, global_slot(c, "InvalidOperationException", 25));
    emit_op16(c, OP_CONST, kcstr(c, "Non-exhaustive switch expression"));
    emit_op8(c, OP_CALL, 1);
    emit_op(c, OP_THROW);
    jl_patch(c, &ends);
    return PT_ANY;
}

static uint8_t compile_interp(comp_t* c, node_t* n) {
    int parts = 0;
    for (node_t* p = n->a; p; p = p->next) {
        if (p->kind == N_STR) { if (p->len == 0) continue; emit_op16(c, OP_CONST, kstr(c, p->name, p->len)); }
        else {
            uint8_t pt = compile_expr(c, p->a);
            if (p->name || p->ival) {
                char spec[96];
                int k = 0;
                if (p->ival) k = snprintf(spec, sizeof spec, ",%d", (int)p->ival);
                if (p->name) k += snprintf(spec + k, sizeof spec - (size_t)k, ":%.*s", (int)p->len, p->name);
                emit_op16(c, OP_CONST, kstr(c, spec, (uint32_t)k));
                emit_op(c, OP_TOSTR_FMT);
            } else if (pt != PT_STRING) emit_op(c, OP_TOSTR);
        }
        parts++;
        if (parts == 255) { emit_op8(c, OP_CONCAT, 255); parts = 1; }
    }
    if (parts == 0) emit_op16(c, OP_CONST, kstr(c, "", 0));
    else if (parts > 1) emit_op8(c, OP_CONCAT, (uint8_t)parts);
    return PT_STRING;
}

static uint8_t compile_expr(comp_t* c, node_t* n) {
    uint32_t saved_line = c->line;
    c->line = n->line;
    uint8_t pt = PT_ANY;
    mcs_value_t k;
    switch (n->kind) {
    case N_INT: case N_FLOAT: case N_CHAR: case N_BOOL: case N_STR:
        fold(c, n, &k); emit_value(c, k); pt = pt_of_value(k); break;
    case N_NULL: emit_op(c, OP_NULL); break;
    case N_INTERP: pt = compile_interp(c, n); break;
    case N_THIS: emit_this(c); break;
    case N_BASE: emit_this(c); break;
    case N_NAME: {
        if (fold(c, n, &k)) { emit_value(c, k); pt = pt_of_value(k); break; }
        res_t r = resolve(c, n->name, n->len);
        pt = r.pt;
        switch (r.kind) {
        case R_LOCAL: emit_op8(c, OP_GET_LOCAL, (uint8_t)r.index); break;
        case R_UPVAL: emit_op8(c, OP_GET_UPVAL, (uint8_t)r.index); break;
        case R_GLOBAL: emit_op16(c, OP_GET_GLOBAL, (uint32_t)r.index); break;
        case R_THIS_MEMBER: emit_this(c); emit_op16(c, OP_GET_FIELD, kstr(c, n->name, n->len)); break;
        case R_STATIC_MEMBER: emit_class_ref(c, r.owner); emit_op16(c, OP_GET_FIELD, kstr(c, n->name, n->len)); break;
        }
        break;
    }
    case N_MEMBER: {
        if (fold(c, n, &k)) { emit_value(c, k); pt = pt_of_value(k); break; }
        if (n->a->kind == N_BASE || n->a->kind == N_THIS) {
            emit_this(c);
            if (c->fc->cls) { member_t* m = find_member(c->fc->cls, n->name, n->len, NULL); if (m && (m->kind == M_FIELD || m->kind == M_PROP)) pt = pt_of_type(m->type); }
            emit_op16(c, OP_GET_FIELD, kstr(c, n->name, n->len));
            break;
        }
        compile_expr(c, n->a);
        uint32_t j = 0;
        if (n->flag) j = emit_jump(c, OP_JUMP_IF_NULL_KEEP);
        emit_op16(c, OP_GET_FIELD, kstr(c, n->name, n->len));
        if (n->flag) patch_at(c, j);
        if (name_eq(n->name, n->len, "Length", 6) || name_eq(n->name, n->len, "Count", 5)) pt = PT_INT;
        break;
    }
    case N_INDEX: {
        compile_expr(c, n->a);
        uint32_t j = 0;
        if (n->flag) j = emit_jump(c, OP_JUMP_IF_NULL_KEEP);
        compile_expr(c, n->b);
        emit_op(c, OP_GET_INDEX);
        if (n->flag) patch_at(c, j);
        break;
    }
    case N_CALL: pt = compile_call(c, n); emit_out_writeback(c, n->b); break;
    case N_NEW: pt = compile_new(c, n); break;
    case N_NEW_ARRAY:
        compile_expr(c, n->a);
        emit_op8(c, OP_NEW_ARRAY, array_kind(n->type, n->flag != 0));
        break;
    case N_ARRAY_LIT: compile_array_lit(c, n); break;
    case N_UNARY:
        if (fold(c, n, &k)) { emit_value(c, k); pt = pt_of_value(k); break; }
        pt = compile_expr(c, n->a);
        emit_op(c, n->op == TK_MINUS ? OP_NEG : n->op == TK_BANG ? OP_NOT : OP_BNOT);
        if (n->op == TK_BANG) pt = PT_BOOL;
        else if (pt == PT_CHAR) pt = PT_INT;
        break;
    case N_BINARY: {
        if (fold(c, n, &k)) { emit_value(c, k); pt = pt_of_value(k); break; }
        uint8_t a = compile_expr(c, n->a);
        uint8_t b = compile_expr(c, n->b);
        emit_op(c, binop_code(n->op));
        pt = arith_pt(n->op, a, b);
        break;
    }
    case N_AND: case N_OR: {
        compile_expr(c, n->a);
        uint32_t j = emit_jump(c, n->kind == N_AND ? OP_JUMP_IF_FALSE_KEEP : OP_JUMP_IF_TRUE_KEEP);
        emit_op(c, OP_POP);
        compile_expr(c, n->b);
        patch_at(c, j);
        pt = PT_BOOL;
        break;
    }
    case N_COALESCE: {
        compile_expr(c, n->a);
        uint32_t j = emit_jump(c, OP_JUMP_IF_NOT_NULL_KEEP);
        emit_op(c, OP_POP);
        pt = compile_expr(c, n->b);
        patch_at(c, j);
        break;
    }
    case N_ASSIGN: pt = compile_assign(c, n, true); break;
    case N_PREINC: pt = compile_incdec(c, n, true, true); break;
    case N_POSTINC: pt = compile_incdec(c, n, false, true); break;
    case N_COND: {
        compile_expr(c, n->a);
        uint32_t jf = emit_jump(c, OP_JUMP_IF_FALSE);
        uint8_t a = compile_expr(c, n->b);
        uint32_t je = emit_jump(c, OP_JUMP);
        patch_at(c, jf);
        uint8_t b = compile_expr(c, n->c);
        patch_at(c, je);
        pt = a == b ? a : PT_ANY;
        break;
    }
    case N_CAST: {
        if (fold(c, n, &k)) { emit_value(c, k); pt = pt_of_value(k); break; }
        uint8_t src = compile_expr(c, n->a);
        typeref_t* t = n->type;
        if (t->rank == 0 && t->prim != PT_ANY && t->prim != PT_OBJECT && t->prim != PT_STRING && t->prim != PT_VOID) {
            uint8_t cv = t->conv == CV_UINT ? CV_INT : t->conv;
            if (cv != 0xff && cv != CV_BOOL && !(cv == CV_INT && src == PT_INT) && !(cv == CV_FLOAT && src == PT_FLOAT))
                emit_op8(c, OP_CONV, cv);
            pt = t->prim;
        } else if (t->prim == PT_ANY && t->rank == 0) {
            classdecl_t* d = find_class(c, t->name, t->len);
            if (d && d->kind == C_ENUM) { emit_op8(c, OP_CONV, CV_INT); pt = PT_INT; }
            else if (!(t->len == 1 || (d && d->kind == C_INTERFACE))) emit_op16(c, OP_CAST, kstr(c, t->name, t->len)); /* T (generic) skipped */
        } else if (t->prim == PT_STRING) { emit_op16(c, OP_CAST, kcstr(c, "string")); pt = PT_STRING; }
        break;
    }
    case N_IS: {
        if (n->a) compile_expr(c, n->a); /* NULL: subject already on the stack (switch arm) */
        if (n->op == 1) { emit_op(c, OP_NULL); emit_op(c, OP_EQ); }
        else if (n->op == 2) { compile_expr(c, n->b); emit_op(c, OP_EQ); }
        else {
            if (n->name) {
                int slot = resolve_local(c->fc, n->name, n->len);
                if (slot < 0) cerr(c, n->line, "internal: pattern variable not declared");
                c->fc->locals[slot].pt = pt_of_type(n->type);
                emit_op8(c, OP_SET_LOCAL, (uint8_t)slot);
            }
            uint32_t len; const char* nm = canon_type_name(n->type, &len);
            emit_op16(c, OP_IS, kstr(c, nm, len));
        }
        if (n->flag) emit_op(c, OP_NOT);
        pt = PT_BOOL;
        break;
    }
    case N_AS: {
        compile_expr(c, n->a);
        uint32_t len; const char* nm = canon_type_name(n->type, &len);
        emit_op16(c, OP_AS, kstr(c, nm, len));
        break;
    }
    case N_LAMBDA:
        emit_closure(c, n->fn, FK_LAMBDA, c->fc->cls, "lambda", 6, NULL);
        break;
    case N_SWITCH_EXPR: pt = compile_switch_expr(c, n); break;
    case N_TYPEOF: { uint32_t len; const char* nm = canon_type_name(n->type, &len); emit_op16(c, OP_CONST, kstr(c, nm, len)); pt = PT_STRING; break; }
    case N_DEFAULT: emit_value(c, default_for(n->type)); pt = n->type ? n->type->prim : PT_ANY; break;
    case N_THROW:
        if (!n->a) cerr(c, n->line, "rethrow is only allowed as a statement");
        compile_expr(c, n->a); emit_op(c, OP_THROW);
        break;
    default:
        cerr(c, n->line, "unsupported expression");
    }
    c->line = saved_line;
    return pt;
}

/* ------------------------------------------------------------ statements */
static void predeclare(comp_t* c, node_t* n, bool follow) {
    for (; n; n = follow ? n->next : NULL) {
        if (n->kind == N_LAMBDA) continue;
        if (n->kind == N_IS && n->name) {
            int l = resolve_local(c->fc, n->name, n->len);
            if (l < 0 || c->fc->locals[l].depth != c->fc->depth) {
                if (c->fc->kind == FK_MAIN && c->fc->depth == 0) {
                    /* top-level: pattern variables live in a scope of their own */
                }
                emit_op(c, OP_NULL);
                declare_local(c, n->name, n->len, pt_of_type(n->type), 0xff);
            }
        }
        if (n->kind == N_ARG_OUT) {
            emit_op(c, OP_NULL);
            n->ival = declare_local(c, "$cell", 5, PT_ANY, 0xff);
            if (n->name) {
                uint8_t pt = (n->type && !n->type->is_var) ? pt_of_type(n->type) : PT_ANY;
                if (c->outvar_global) {
                    gvar_t* g = (gvar_t*)arena_alloc(c->ctx->arena, sizeof(gvar_t));
                    g->name = n->name; g->len = n->len; g->pt = pt; g->conv = 0xff; g->next = c->gvars; c->gvars = g;
                    (void)global_slot(c, n->name, n->len);
                } else {
                    int l = resolve_local(c->fc, n->name, n->len);
                    if (l < 0 || c->fc->locals[l].depth != c->fc->depth) {
                        emit_op(c, OP_NULL);
                        declare_local(c, n->name, n->len, pt, 0xff);
                    }
                }
            }
        }
        if (n->kind >= N_EXPR_STMT && n->kind != N_ARM) continue; /* don't descend into statements */
        predeclare(c, n->a, true); predeclare(c, n->b, true); predeclare(c, n->c, true); predeclare(c, n->d, true);
    }
}
static bool has_pattern(node_t* n, bool follow) {
    for (; n; n = follow ? n->next : NULL) {
        if (n->kind == N_LAMBDA) continue;
        if ((n->kind == N_IS && n->name) || n->kind == N_ARG_OUT) return true;
        if (n->kind >= N_EXPR_STMT && n->kind != N_ARM) continue;
        if (has_pattern(n->a, true) || has_pattern(n->b, true) || has_pattern(n->c, true) || has_pattern(n->d, true)) return true;
    }
    return false;
}

static void compile_expr_stmt(comp_t* c, node_t* e) {
    switch (e->kind) {
    case N_ASSIGN: compile_assign(c, e, false); return;
    case N_PREINC: compile_incdec(c, e, true, false); return;
    case N_POSTINC: compile_incdec(c, e, false, false); return;
    default: compile_expr(c, e); emit_op(c, OP_POP); return;
    }
}

static bool is_global_scope(comp_t* c) { return c->fc->kind == FK_MAIN && c->fc->depth == 0; }

static void compile_var(comp_t* c, node_t* v) {
    typeref_t* t = v->type;
    uint8_t conv = conv_of(t);
    uint8_t pt = pt_of_type(t);
    uint8_t save_ac = c->arr_conv;
    c->arr_conv = (t && t->rank == 1 && t->conv != 0xff && t->conv != CV_BOOL) ? (t->conv == CV_UINT ? CV_INT : t->conv) : 0xff;
    if (v->a) {
        uint8_t ipt = compile_expr(c, v->a);
        if (t && t->is_var) pt = ipt;
        else emit_store_conv(c, conv, ipt);
    } else emit_value(c, default_for(t));
    c->arr_conv = save_ac;
    if (is_global_scope(c) || c->force_global) {
        gvar_t* g = (gvar_t*)arena_alloc(c->ctx->arena, sizeof(gvar_t));
        g->name = v->name; g->len = v->len; g->pt = pt; g->conv = conv; g->next = c->gvars; c->gvars = g;
        emit_op16(c, OP_SET_GLOBAL, global_slot(c, v->name, v->len));
        emit_op(c, OP_POP);
    } else {
        int l = resolve_local(c->fc, v->name, v->len);
        if (l >= 0 && c->fc->locals[l].depth == c->fc->depth)
            cerr(c, v->line, "a local variable named '%.*s' is already defined in this scope", (int)v->len, v->name);
        declare_local(c, v->name, v->len, pt, t && t->is_var ? 0xff : conv);
    }
}

static loop_t* push_loop(comp_t* c, loop_t* L, int64_t start, bool is_switch) {
    memset(L, 0, sizeof *L);
    L->prev = c->fc->loop; L->depth = c->fc->depth; L->try_depth = c->fc->try_depth;
    L->start = start; L->is_switch = is_switch;
    c->fc->loop = L;
    return L;
}
static void pop_loop(comp_t* c, loop_t* L) { jl_patch(c, &L->breaks); c->fc->loop = L->prev; }

/* inline finally blocks of try statements being exited (innermost first) */
static void unwind_tries(comp_t* c, int to_depth) {
    fcomp_t* f = c->fc;
    try_t* saved = f->tries; int saved_d = f->try_depth;
    while (f->try_depth > to_depth) {
        try_t* t = f->tries;
        emit_op(c, OP_END_TRY);
        f->tries = t->prev; f->try_depth--;
        if (t->fin) compile_stmt(c, t->fin);
    }
    f->tries = saved; f->try_depth = saved_d;
}
static bool tries_have_finally(comp_t* c, int to_depth) {
    int d = c->fc->try_depth;
    for (try_t* t = c->fc->tries; t && d > to_depth; t = t->prev, d--) if (t->fin) return true;
    return false;
}

static void emit_ref_writeback(comp_t* c) {
    fcomp_t* f = c->fc;
    for (int i = 0; i < f->nrefs; i++) {
        emit_op8(c, OP_GET_LOCAL, f->ref_cell[i]); emit_op8(c, OP_INT8, 0);
        emit_op8(c, OP_GET_LOCAL, f->ref_local[i]); emit_op(c, OP_SET_INDEX); emit_op(c, OP_POP);
    }
}
static void emit_return_value(comp_t* c) {
    emit_ref_writeback(c);
    if (c->fc->kind == FK_CTOR) { emit_op8(c, OP_GET_LOCAL, 0); emit_op(c, OP_RETURN); }
    else emit_op(c, OP_RETURN_NULL);
}

static void compile_return(comp_t* c, node_t* n) {
    fcomp_t* f = c->fc;
    if (f->kind == FK_MAIN && n->a) cerr(c, n->line, "cannot return a value from top-level code");
    if (n->a && has_pattern(n->a, false)) predeclare(c, n->a, false);
    if (n->a) {
        uint8_t pt = compile_expr(c, n->a);
        if (f->ret) emit_store_conv(c, conv_of(f->ret), pt);
    }
    if (tries_have_finally(c, 0)) {
        int slot = -1;
        if (n->a) slot = declare_local(c, "$ret", 4, PT_ANY, 0xff);
        unwind_tries(c, 0);
        if (n->a) { emit_op8(c, OP_GET_LOCAL, (uint8_t)slot); emit_ref_writeback(c); emit_op(c, OP_RETURN); f->nlocals--; }
        else emit_return_value(c);
        return;
    }
    if (n->a) { emit_ref_writeback(c); emit_op(c, OP_RETURN); }
    else emit_return_value(c);
}

static void compile_break(comp_t* c, node_t* n, bool is_continue) {
    loop_t* L = c->fc->loop;
    if (is_continue) while (L && L->is_switch) L = L->prev;
    if (!L) cerr(c, n->line, is_continue ? "'continue' outside of a loop" : "'break' outside of a loop or switch");
    unwind_tries(c, L->try_depth);
    pop_to_depth(c, L->depth);
    if (is_continue) {
        if (L->start >= 0) emit_loop(c, (uint32_t)L->start);
        else jl_add(c, &L->conts, emit_jump(c, OP_JUMP));
    } else jl_add(c, &L->breaks, emit_jump(c, OP_JUMP));
}

static void compile_foreach(comp_t* c, node_t* n) {
    begin_scope(c);
    compile_expr(c, n->a);
    int coll = declare_local(c, "$coll", 5, PT_ANY, 0xff);
    emit_op8(c, OP_INT8, 0);
    declare_local(c, "$idx", 4, PT_INT, 0xff);
    uint32_t start = here(c);
    emit(c, OP_FOR_ITER); emit(c, (uint8_t)coll); emit(c, 0xff); emit(c, 0xff);
    uint32_t exit_at = here(c) - 2;
    loop_t L; push_loop(c, &L, start, false);
    begin_scope(c);
    uint8_t conv = conv_of(n->type);
    if (conv != 0xff) emit_store_conv(c, conv, PT_ANY);
    declare_local(c, n->name, n->len, pt_of_type(n->type), conv);
    compile_stmt(c, n->b);
    end_scope(c);
    emit_loop(c, start);
    patch_at(c, exit_at);
    pop_loop(c, &L);
    end_scope(c);
}

static void compile_try(comp_t* c, node_t* n) {
    fcomp_t* f = c->fc;
    node_t* fin = n->c;
    try_t T = { f->tries, fin };
    uint32_t h = emit_jump(c, OP_TRY);
    f->tries = &T; f->try_depth++;
    compile_stmt(c, n->a);
    f->tries = T.prev; f->try_depth--;
    emit_op(c, OP_END_TRY);
    if (fin) compile_stmt(c, fin);
    uint32_t to_end = emit_jump(c, OP_JUMP);
    patch_at(c, h);

    begin_scope(c);
    int exc = declare_local(c, "$exc", 4, PT_ANY, 0xff);
    int saved_exc = f->exc_slot;
    f->exc_slot = exc;
    if (!n->b) {
        /* try/finally: run finally then rethrow */
        compile_stmt(c, fin);
        emit_op8(c, OP_GET_LOCAL, (uint8_t)exc); emit_op(c, OP_THROW);
        f->exc_slot = saved_exc;
        f->nlocals--; f->depth--;
        patch_at(c, to_end);
        return;
    }
    uint32_t h2 = 0;
    try_t T2 = { f->tries, fin };
    if (fin) { h2 = emit_jump(c, OP_TRY); f->tries = &T2; f->try_depth++; }
    jlist_t done = {0};
    for (node_t* k = n->b; k; k = k->next) {
        c->line = k->line;
        uint32_t next = 0; bool cond = false;
        if (k->type) {
            uint32_t len; const char* nm = canon_type_name(k->type, &len);
            if (!(name_eq(nm, len, "Exception", 9) || name_eq(nm, len, "object", 6))) {
                emit_op8(c, OP_GET_LOCAL, (uint8_t)exc);
                emit_op16(c, OP_IS, kstr(c, nm, len));
                next = emit_jump(c, OP_JUMP_IF_FALSE); cond = true;
            }
        }
        begin_scope(c);
        if (k->name) { emit_op8(c, OP_GET_LOCAL, (uint8_t)exc); declare_local(c, k->name, k->len, PT_ANY, 0xff); }
        uint32_t wnext = 0; bool wc = false;
        if (k->c) { compile_expr(c, k->c); wnext = emit_jump(c, OP_JUMP_IF_FALSE); wc = true; }
        compile_stmt(c, k->a);
        if (wc) {
            /* guard false: leave scope and continue with next clause */
            end_scope(c);
            jl_add(c, &done, emit_jump(c, OP_JUMP));
            patch_at(c, wnext);
            if (k->name) emit_op(c, OP_POP);
        } else {
            end_scope(c);
            jl_add(c, &done, emit_jump(c, OP_JUMP));
        }
        if (cond) patch_at(c, next);
        if (!cond && !wc) break;
    }
    /* no clause matched: rethrow */
    emit_op8(c, OP_GET_LOCAL, (uint8_t)exc); emit_op(c, OP_THROW);
    jl_patch(c, &done);
    if (fin) {
        f->tries = T2.prev; f->try_depth--;
        emit_op(c, OP_END_TRY);
        compile_stmt(c, fin);
    }
    f->exc_slot = saved_exc;
    end_scope(c);
    uint32_t to_end2 = emit_jump(c, OP_JUMP);
    if (fin) {
        /* exception escaping a catch block: run finally, rethrow */
        patch_at(c, h2);
        begin_scope(c);
        declare_local(c, "$exc", 4, PT_ANY, 0xff);
        int e2 = declare_local(c, "$exc2", 5, PT_ANY, 0xff);
        compile_stmt(c, fin);
        emit_op8(c, OP_GET_LOCAL, (uint8_t)e2); emit_op(c, OP_THROW);
        f->nlocals -= 2; f->depth--;
    }
    patch_at(c, to_end);
    patch_at(c, to_end2);
}

static void compile_switch(comp_t* c, node_t* n) {
    begin_scope(c);
    compile_expr(c, n->a);
    int sw = declare_local(c, "$sw", 3, PT_ANY, 0xff);
    /* pattern variables of case labels (`case Circle k when k.R > 1:`) live in the switch scope */
    for (node_t* s = n->b; s; s = s->next)
        for (node_t* lab = s->a; lab; lab = lab->next) { predeclare(c, lab->a, true); predeclare(c, lab->c, false); }
    int nsec = 0;
    for (node_t* s = n->b; s; s = s->next) nsec++;
    jlist_t* hits = (jlist_t*)arena_alloc(c->ctx->arena, sizeof(jlist_t) * (size_t)(nsec + 1));
    int i = 0, def = -1;
    for (node_t* s = n->b; s; s = s->next, i++) {
        if (s->flag) def = i;
        for (node_t* lab = s->a; lab; lab = lab->next) {
            /* lab = N_ARM: a = pattern list (constants compare with ==), c = optional `when` guard */
            c->line = lab->line;
            jlist_t phit = {0};
            bool always = false;
            emit_op8(c, OP_GET_LOCAL, (uint8_t)sw);
            for (node_t* p = lab->a; p; p = p->next) {
                if (p->kind == N_EMPTY) { always = true; break; }
                compile_pattern_test(c, p);
                jl_add(c, &phit, emit_jump(c, OP_JUMP_IF_TRUE));
            }
            uint32_t miss = 0, skip = 0;
            if (!always) {
                emit_op(c, OP_POP);
                miss = emit_jump(c, OP_JUMP);
            }
            jl_patch(c, &phit);
            emit_op(c, OP_POP);
            if (lab->c) { compile_expr(c, lab->c); skip = emit_jump(c, OP_JUMP_IF_FALSE); }
            jl_add(c, &hits[i], emit_jump(c, OP_JUMP));
            if (!always) patch_at(c, miss);
            if (lab->c) patch_at(c, skip);
        }
    }
    loop_t L; push_loop(c, &L, -1, true);
    if (def >= 0) jl_add(c, &hits[def], emit_jump(c, OP_JUMP));
    else jl_add(c, &L.breaks, emit_jump(c, OP_JUMP));
    i = 0;
    for (node_t* s = n->b; s; s = s->next, i++) {
        jl_patch(c, &hits[i]);
        begin_scope(c);
        compile_block_list(c, s->b);
        end_scope(c);
    }
    pop_loop(c, &L);
    end_scope(c);
}

static void compile_block_list(comp_t* c, node_t* list) {
    for (node_t* s = list; s; s = s->next) compile_stmt(c, s);
}

static void compile_stmt(comp_t* c, node_t* n) {
    c->line = n->line;
    fcomp_t* f = c->fc;
    switch (n->kind) {
    case N_EMPTY: return;
    case N_EXPR_STMT:
        if (has_pattern(n->a, false)) { if (is_global_scope(c)) { begin_scope(c); c->outvar_global = true; predeclare(c, n->a, false); c->outvar_global = false; compile_expr_stmt(c, n->a); end_scope(c); return; } predeclare(c, n->a, false); }
        compile_expr_stmt(c, n->a);
        return;
    case N_VAR:
        if (has_pattern(n->a, false)) {
            if (is_global_scope(c)) {
                begin_scope(c); c->outvar_global = true; predeclare(c, n->a, false); c->outvar_global = false;
                c->force_global = true; compile_var(c, n); c->force_global = false;
                end_scope(c);
                return;
            }
            predeclare(c, n->a, false);
        }
        compile_var(c, n);
        return;
    case N_BLOCK:
        if (n->flag) { compile_block_list(c, n->a); return; }
        begin_scope(c); compile_block_list(c, n->a); end_scope(c);
        return;
    case N_IF: {
        bool scoped = has_pattern(n->a, false);
        if (scoped) { if (is_global_scope(c)) { begin_scope(c); c->outvar_global = true; } else scoped = false; predeclare(c, n->a, false); c->outvar_global = false; }
        compile_expr(c, n->a);
        uint32_t jf = emit_jump(c, OP_JUMP_IF_FALSE);
        compile_stmt(c, n->b);
        if (n->c) {
            uint32_t je = emit_jump(c, OP_JUMP);
            patch_at(c, jf);
            compile_stmt(c, n->c);
            patch_at(c, je);
        } else patch_at(c, jf);
        if (scoped) end_scope(c);
        return;
    }
    case N_WHILE: {
        begin_scope(c);
        predeclare(c, n->a, false);
        uint32_t start = here(c);
        loop_t L; push_loop(c, &L, start, false);
        compile_expr(c, n->a);
        uint32_t jf = emit_jump(c, OP_JUMP_IF_FALSE);
        compile_stmt(c, n->b);
        emit_loop(c, start);
        patch_at(c, jf);
        pop_loop(c, &L);
        end_scope(c);
        return;
    }
    case N_DO: {
        uint32_t start = here(c);
        loop_t L; push_loop(c, &L, -1, false);
        compile_stmt(c, n->b);
        jl_patch(c, &L.conts);
        compile_expr(c, n->a);
        uint32_t jf = emit_jump(c, OP_JUMP_IF_FALSE);
        emit_loop(c, start);
        patch_at(c, jf);
        pop_loop(c, &L);
        return;
    }
    case N_FOR: {
        begin_scope(c);
        if (n->a) compile_stmt(c, n->a);
        uint32_t start = here(c);
        uint32_t jf = 0;
        loop_t L; push_loop(c, &L, -1, false);
        if (n->b) { compile_expr(c, n->b); jf = emit_jump(c, OP_JUMP_IF_FALSE); }
        compile_stmt(c, n->d);
        jl_patch(c, &L.conts);
        if (n->c) compile_stmt(c, n->c);
        emit_loop(c, start);
        if (n->b) patch_at(c, jf);
        pop_loop(c, &L);
        end_scope(c);
        return;
    }
    case N_FOREACH: compile_foreach(c, n); return;
    case N_BREAK: compile_break(c, n, false); return;
    case N_CONTINUE: compile_break(c, n, true); return;
    case N_RETURN: compile_return(c, n); return;
    case N_THROW:
        if (n->a) compile_expr(c, n->a);
        else {
            if (f->exc_slot < 0) cerr(c, n->line, "'throw;' is only valid inside a catch block");
            emit_op8(c, OP_GET_LOCAL, (uint8_t)f->exc_slot);
        }
        emit_op(c, OP_THROW);
        return;
    case N_TRY: compile_try(c, n); return;
    case N_SWITCH: compile_switch(c, n); return;
    case N_LOCAL_FUNC:
        if (is_global_scope(c)) return; /* hoisted */
        declare_local(c, n->name, n->len, PT_ANY, 0xff);
        emit_closure(c, n->fn, FK_LAMBDA, f->cls, n->name, n->len, NULL);
        return;
    default:
        /* expression used as statement (shouldn't happen) */
        compile_expr(c, n); emit_op(c, OP_POP);
    }
}

/* ------------------------------------------------------------ functions */
static void emit_ctor_prologue(comp_t* c, member_t* m, classdecl_t* cls, bool needs_init) {
    if (m && m->has_ctor_call && m->ctor_call == 2) {
        emit_op8(c, OP_GET_LOCAL, 0);
        int argc = compile_args(c, m->ctor_args);
        emit_class_ref(c, cls);
        emit_invoke(c, OP_SUPER_INVOKE, ".ctor", 5, argc);
        emit_op(c, OP_POP);
        return;
    }
    if (needs_init) {
        emit_op8(c, OP_GET_LOCAL, 0);
        emit_class_ref(c, cls);
        emit_invoke(c, OP_SUPER_INVOKE, "$init", 5, 0);
        emit_op(c, OP_POP);
    }
    if (cls->base) {
        emit_op8(c, OP_GET_LOCAL, 0);
        int argc = (m && m->has_ctor_call) ? compile_args(c, m->ctor_args) : 0;
        emit_op16(c, OP_GET_GLOBAL, global_slot(c, cls->base, cls->base_len));
        emit_invoke(c, OP_SUPER_INVOKE, ".ctor", 5, argc);
        emit_op(c, OP_POP);
    }
}

static bool class_needs_init(classdecl_t* d);

static void emit_closure(comp_t* c, funcdecl_t* f, int kind, classdecl_t* cls, const char* name, uint32_t nlen, member_t* ctor) {
    fcomp_t* prev = c->fc;
    fcomp_t* fc = c->fc_free;
    if (fc) { c->fc_free = fc->enc; memset(fc, 0, sizeof *fc); }
    else fc = (fcomp_t*)arena_alloc(c->ctx->arena, sizeof(fcomp_t));
    fc->enc = (kind == FK_LAMBDA || kind == FK_FUNC) ? prev : NULL;
    fc->kind = kind; fc->cls = cls; fc->exc_slot = -1; fc->ret = f->ret;
    fc->is_static = kind == FK_STATIC || kind == FK_FUNC || (kind == FK_LAMBDA && prev && prev->is_static);
    mcs_function_t* fn = mcs_new_function(c->vm);
    fn->name = istr(c, name, nlen);
    fn->source = c->src_name;
    fc->fn = fn;
    uint32_t saved_line = c->line;
    c->line = f->line;
    c->fc = fc;
    fc->depth = 1;
    if (kind == FK_METHOD || kind == FK_CTOR) declare_local(c, "this", 4, PT_ANY, 0xff);
    else declare_local(c, "", 0, PT_ANY, 0xff);
    if (f->nparams) fn->param_types = (uint8_t*)mcs_realloc(c->vm, NULL, 0, f->nparams);
    int i = 0; int min = -1;
    for (param_t* p = f->params; p; p = p->next, i++) {
        if (p->ref_kind) declare_local(c, "$ref", 4, PT_ANY, 0xff);
        else declare_local(c, p->name, p->len, pt_of_type(p->type), conv_of(p->type));
        fn->param_types[i] = (p->is_params || p->ref_kind) ? PT_ANY : pt_of_type(p->type);
        if (fn->param_types[i] == PT_OBJECT) fn->param_types[i] = PT_ANY;
        if ((p->def || p->is_params) && min < 0) min = i;
        if (p->is_params) fn->flags |= FN_HAS_PARAMS;
    }
    fn->arity = f->nparams;
    fn->min_arity = (uint8_t)(min < 0 ? f->nparams : min);
    if (kind == FK_CTOR) fn->flags |= FN_IS_CTOR;
    if (kind == FK_STATIC) fn->flags |= FN_IS_STATIC;
    /* default arguments */
    i = 0;
    for (param_t* p = f->params; p; p = p->next, i++) {
        if (p->def) {
            emit(c, OP_ARGC_JUMP); emit(c, (uint8_t)(i + 1)); emit(c, 0xff); emit(c, 0xff);
            uint32_t at = here(c) - 2;
            uint8_t pt = compile_expr(c, p->def);
            emit_store_conv(c, conv_of(p->type), pt);
            emit_op8(c, OP_SET_LOCAL, (uint8_t)(i + 1));
            emit_op(c, OP_POP);
            patch_at(c, at);
        }
        if (conv_of(p->type) == CV_FLOAT) {
            emit_op8(c, OP_GET_LOCAL, (uint8_t)(i + 1)); emit_op8(c, OP_CONV, CV_FLOAT);
            emit_op8(c, OP_SET_LOCAL, (uint8_t)(i + 1)); emit_op(c, OP_POP);
        }
    }
    /* ref/out parameters: work on a local copy, written back to the caller's cell on every return */
    i = 0;
    for (param_t* p = f->params; p; p = p->next, i++) {
        if (!p->ref_kind) continue;
        if (fc->nrefs >= 16) cerr(c, f->line, "too many ref/out parameters");
        if (p->ref_kind == 2) emit_cell_read(c, i + 1);
        else if (p->type) emit_value(c, default_for(p->type));
        else emit_op(c, OP_NULL);
        fc->ref_cell[fc->nrefs] = (uint8_t)(i + 1);
        fc->ref_local[fc->nrefs] = (uint8_t)declare_local(c, p->name, p->len, pt_of_type(p->type), conv_of(p->type));
        fc->nrefs++;
    }
    if (kind == FK_CTOR) emit_ctor_prologue(c, ctor, cls, class_needs_init(cls));
    if (f->body) {
        if (f->expr_body) {
            if (has_pattern(f->body, false)) predeclare(c, f->body, false);
            uint8_t pt = compile_expr(c, f->body);
            if (kind == FK_CTOR) { emit_op(c, OP_POP); emit_op8(c, OP_GET_LOCAL, 0); }
            else if (f->ret) emit_store_conv(c, conv_of(f->ret), pt);
            emit_ref_writeback(c);
            emit_op(c, OP_RETURN);
        } else {
            compile_stmt(c, f->body);
            emit_return_value(c);
        }
    } else emit_return_value(c);
    fn->upvalue_count = (uint8_t)fc->nupv;
    fn->max_slots = (uint16_t)fc->max_locals;
    c->fc = prev;
    c->line = saved_line;
    emit_op16(c, OP_CLOSURE, konst(c, OBJ_VAL(fn)));
    for (int u = 0; u < fc->nupv; u++) { emit(c, fc->upv[u].is_local); emit(c, fc->upv[u].index); }
    fc->enc = c->fc_free; c->fc_free = fc; /* finished: reuse for the next function */
}

/* ------------------------------------------------------------ classes */
static bool member_is_data(member_t* m) { return m->kind == M_FIELD || (m->kind == M_PROP && m->auto_prop); }

static bool class_needs_init(classdecl_t* d) {
    for (member_t* m = d->members; m; m = m->next) {
        if (!member_is_data(m) || (m->mods & MOD_STATIC) || !m->init) continue;
        return true; /* refined in emit: constant initializers become defaults */
    }
    return false;
}

static bool const_init(comp_t* c, member_t* m, mcs_value_t* out) {
    if (!m->init) { *out = default_for(m->type); return true; }
    if (m->init->kind == N_ARRAY_LIT || m->init->kind == N_NEW || m->init->kind == N_NEW_ARRAY) return false;
    if (!fold(c, m->init, out)) return false;
#if MCS_ENABLE_FLOAT
    if (m->type && m->type->prim == PT_FLOAT && !m->type->rank && out->type == MCS_T_INT) *out = mcs_float((mcs_float_t)out->as.i);
#endif
    if (m->type && m->type->prim == PT_INT && !m->type->rank && out->type == MCS_T_CHAR) *out = mcs_int(out->as.i);
    return true;
}

static void emit_def(comp_t* c, uint8_t op, const char* name, uint32_t len, uint8_t flags) {
    emit(c, op); emit_u16(c, kstr(c, name, len)); emit(c, flags);
}

static bool first_decl(member_t* list, member_t* m, const char* name, uint32_t len) {
    for (member_t* x = list; x && x != m; x = x->next) {
        if (x->kind == M_METHOD && name_eq(x->fn->name, x->fn->len, name, len) && ((x->mods & MOD_STATIC) == (m->mods & MOD_STATIC)) && x->fn->body) return false;
        if (x->kind == M_CTOR && m->kind == M_CTOR && !(x->mods & MOD_STATIC)) return false;
        if (x->kind == M_OPERATOR && m->kind == M_OPERATOR && name_eq(x->name, x->len, name, len)) return false;
    }
    return true;
}

static node_t* synth(comp_t* c, int kind, uint32_t line) {
    node_t* n = (node_t*)arena_alloc(c->ctx->arena, sizeof(node_t));
    n->kind = (uint8_t)kind; n->line = line;
    return n;
}

static void emit_class(comp_t* c, classdecl_t* d) {
    c->line = d->line;
    if (d->kind == C_INTERFACE) {
        emit(c, OP_CLASS); emit_u16(c, kstr(c, d->name, d->len)); emit(c, 2);
        emit_op16(c, OP_SET_GLOBAL, global_slot(c, d->name, d->len)); emit_op(c, OP_POP);
        return;
    }
    emit(c, OP_CLASS); emit_u16(c, kstr(c, d->name, d->len)); emit(c, d->is_static || d->kind == C_ENUM ? 1 : 0);
    if (d->kind == C_ENUM) {
        for (enumval_t* v = d->enums; v; v = v->next) {
            emit_value(c, mcs_int(v->computed));
            emit_def(c, OP_STATIC, v->name, v->len, 0);
        }
        emit_op16(c, OP_SET_GLOBAL, global_slot(c, d->name, d->len)); emit_op(c, OP_POP);
        return;
    }
    if (d->base) { emit_op16(c, OP_GET_GLOBAL, global_slot(c, d->base, d->base_len)); emit_op(c, OP_INHERIT); }
    for (namelist_t* b = d->bases; b; b = b->next) {
        if (name_eq(b->name, b->len, d->base ? d->base : "", d->base ? d->base_len : 0)) continue;
        emit_op16(c, OP_IMPLEMENTS, kstr(c, b->name, b->len));
    }
    fcomp_t* mainfc = c->fc;
    classdecl_t* save_cls = mainfc->cls;
    mainfc->cls = d;
    bool needs_init = false;
    node_t *init_h = NULL, *init_t = NULL;
    bool has_ctor = false;
    for (member_t* m = d->members; m; m = m->next) {
        c->line = m->line;
        switch (m->kind) {
        case M_FIELD: case M_PROP: case M_CONST:
            if (member_is_data(m) || m->kind == M_CONST) {
                mcs_value_t v;
                bool isconst = const_init(c, m, &v);
                if (m->mods & MOD_STATIC) {
                    emit_value(c, isconst ? v : default_for(m->type));
                    emit_def(c, OP_STATIC, m->name, m->len, 0);
                } else {
                    emit(c, OP_FIELD); emit_u16(c, kstr(c, m->name, m->len));
                    if (!isconst) v = default_for(m->type);
                    emit_u16(c, v.type == MCS_T_NULL ? 0xFFFF : konst(c, v));
                    if (!isconst) {
                        needs_init = true;
                        node_t* st = synth(c, N_EXPR_STMT, m->line);
                        node_t* as = synth(c, N_ASSIGN, m->line); as->op = TK_ASSIGN;
                        node_t* mem = synth(c, N_MEMBER, m->line); mem->a = synth(c, N_THIS, m->line);
                        mem->name = m->name; mem->len = m->len;
                        as->a = mem; as->b = m->init; st->a = as;
                        if (!init_h) init_h = st; else init_t->next = st;
                        init_t = st;
                    }
                }
            }
            if (m->kind == M_PROP && !m->auto_prop) {
                bool st = (m->mods & MOD_STATIC) != 0;
                if (m->getter) {
                    m->getter->ret = m->type;
                    emit_closure(c, m->getter, st ? FK_STATIC : FK_METHOD, d, m->name, m->len, NULL);
                    emit_def(c, OP_GETTER, m->name, m->len, 0);
                }
                if (m->setter) {
                    emit_closure(c, m->setter, st ? FK_STATIC : FK_METHOD, d, m->name, m->len, NULL);
                    emit_def(c, OP_SETTER, m->name, m->len, 0);
                }
            }
            break;
        case M_METHOD:
            if (!m->fn->body) break; /* abstract / interface */
            emit_closure(c, m->fn, (m->mods & MOD_STATIC) ? FK_STATIC : FK_METHOD, d, m->fn->name, m->fn->len, NULL);
            emit_def(c, (m->mods & MOD_STATIC) ? OP_STATIC : OP_METHOD, m->fn->name, m->fn->len,
                     first_decl(d->members, m, m->fn->name, m->fn->len) ? 1 : 0);
            break;
        case M_OPERATOR:
            emit_closure(c, m->fn, FK_STATIC, d, m->name, m->len, NULL);
            emit_def(c, OP_STATIC, m->name, m->len, first_decl(d->members, m, m->name, m->len) ? 1 : 0);
            break;
        case M_INDEXER:
            if (m->getter) { emit_closure(c, m->getter, FK_METHOD, d, "get_Item", 8, NULL); emit_def(c, OP_METHOD, "get_Item", 8, 1); }
            if (m->setter) { emit_closure(c, m->setter, FK_METHOD, d, "set_Item", 8, NULL); emit_def(c, OP_METHOD, "set_Item", 8, 1); }
            break;
        case M_CTOR:
            if (m->mods & MOD_STATIC) {
                emit_closure(c, m->fn, FK_STATIC, d, "$cctor", 6, NULL);
                emit_def(c, OP_STATIC, "$cctor", 6, 1);
            } else {
                has_ctor = true;
            }
            break;
        }
    }
    /* field initializer method */
    if (needs_init) {
        funcdecl_t* f = (funcdecl_t*)arena_alloc(c->ctx->arena, sizeof(funcdecl_t));
        node_t* body = synth(c, N_BLOCK, d->line); body->a = init_h;
        f->body = body; f->line = d->line;
        emit_closure(c, f, FK_METHOD, d, "$init", 5, NULL);
        emit_def(c, OP_METHOD, "$init", 5, 1);
    }
    /* constructors (after $init so the prologue can reference it) */
    for (member_t* m = d->members; m; m = m->next) {
        if (m->kind != M_CTOR || (m->mods & MOD_STATIC)) continue;
        c->line = m->line;
        emit_closure(c, m->fn, FK_CTOR, d, ".ctor", 5, m);
        emit_def(c, OP_METHOD, ".ctor", 5, first_decl(d->members, m, ".ctor", 5) ? 1 : 0);
    }
    if (!has_ctor && (needs_init || d->base_decl)) {
        funcdecl_t* f = (funcdecl_t*)arena_alloc(c->ctx->arena, sizeof(funcdecl_t));
        f->body = synth(c, N_BLOCK, d->line); f->line = d->line;
        emit_closure(c, f, FK_CTOR, d, ".ctor", 5, NULL);
        emit_def(c, OP_METHOD, ".ctor", 5, 1);
    }
    mainfc->cls = save_cls;
    emit_op16(c, OP_SET_GLOBAL, global_slot(c, d->name, d->len));
    emit_op(c, OP_POP);
}

static void emit_static_inits(comp_t* c, classdecl_t* d) {
    fcomp_t* mainfc = c->fc;
    classdecl_t* save = mainfc->cls;
    mainfc->cls = d;
    for (member_t* m = d->members; m; m = m->next) {
        if (!(member_is_data(m) && (m->mods & MOD_STATIC) && m->init)) continue;
        mcs_value_t v;
        if (const_init(c, m, &v)) continue;
        c->line = m->line;
        emit_class_ref(c, d);
        uint8_t save_ac = c->arr_conv;
        c->arr_conv = (m->type && m->type->rank == 1 && m->type->conv != 0xff && m->type->conv != CV_BOOL) ? m->type->conv : 0xff;
        uint8_t pt = compile_expr(c, m->init);
        c->arr_conv = save_ac;
        emit_store_conv(c, conv_of(m->type), pt);
        emit_op16(c, OP_SET_FIELD, kstr(c, m->name, m->len));
        emit_op(c, OP_POP);
    }
    for (member_t* m = d->members; m; m = m->next) {
        if (m->kind == M_CTOR && (m->mods & MOD_STATIC)) {
            emit_class_ref(c, d);
            emit_invoke(c, OP_INVOKE, "$cctor", 6, 0);
            emit_op(c, OP_POP);
        }
    }
    mainfc->cls = save;
}

static bool looks_like_interface(const char* n, uint32_t len) {
    return len >= 2 && n[0] == 'I' && n[1] >= 'A' && n[1] <= 'Z';
}

static void resolve_classes(comp_t* c) {
    for (classdecl_t* d = c->prog->classes; d; d = d->next) {
        if (d->kind == C_ENUM || d->kind == C_INTERFACE || !d->bases) continue;
        namelist_t* b = d->bases;
        classdecl_t* bd = find_class(c, b->name, b->len);
        if (bd && (bd->kind == C_CLASS || bd->kind == C_STRUCT)) { d->base_decl = bd; d->base = b->name; d->base_len = b->len; }
        else if (!bd && !looks_like_interface(b->name, b->len)) { d->base = b->name; d->base_len = b->len; }
        if (bd == d) cerr(c, d->line, "class '%.*s' cannot inherit from itself", (int)d->len, d->name);
    }
    /* enum values */
    for (classdecl_t* d = c->prog->classes; d; d = d->next) {
        if (d->kind != C_ENUM) continue;
        mcs_int_t next = 0;
        c->cur_enum = d;
        for (enumval_t* v = d->enums; v; v = v->next) {
            if (v->value) {
                mcs_value_t k;
                if (!fold(c, v->value, &k) || (k.type != MCS_T_INT && k.type != MCS_T_CHAR)) cerr(c, d->line, "enum value must be a constant integer");
                next = k.as.i;
            }
            v->computed = next;
            next++;
        }
        c->cur_enum = NULL;
    }
}

static void topo_visit(comp_t* c, classdecl_t* d, classdecl_t** out, int* n) {
    if (d->state == 2) return;
    if (d->state == 1) cerr(c, d->line, "circular base class dependency involving '%.*s'", (int)d->len, d->name);
    d->state = 1;
    if (d->base_decl) topo_visit(c, d->base_decl, out, n);
    d->state = 2;
    out[(*n)++] = d;
}

mcs_function_t* mcs_compile(mcs_vm_t* vm, const char* name, const char* src) {
    arena_t arena = { vm, NULL };
    front_ctx_t ctx = { vm, &arena, name, 0 };
    program_t prog; memset(&prog, 0, sizeof prog);
    vm->gc_pause++;
    mcs_function_t* volatile result = NULL;
    /* an out-of-memory panic during compilation must not leak the AST arena:
     * catch it here, free the arena, then pass the panic on */
    jmp_buf pjb; jmp_buf* volatile prev_panic = vm->panic;
    int pcode;
    vm->panic = &pjb;
    if ((pcode = setjmp(pjb)) != 0) {
        vm->panic = prev_panic;
        arena_free(&arena);
        vm->gc_pause--;
        if (prev_panic) longjmp(*prev_panic, pcode);
        mcs_report_error(vm, "fatal: %s\n", vm->error);
        abort();
    }
    if (!mcs_parse(&ctx, src, &prog)) goto done;
    {
        comp_t* c = (comp_t*)arena_alloc(&arena, sizeof(comp_t));
        c->ctx = &ctx; c->vm = vm; c->prog = &prog; c->arr_conv = 0xff; c->gvars = NULL;
        c->src_name = mcs_intern_c(vm, name);
        if (setjmp(c->jb)) goto done;
        fcomp_t* mainfc = (fcomp_t*)arena_alloc(&arena, sizeof(fcomp_t));
        mainfc->kind = FK_MAIN; mainfc->exc_slot = -1;
        mainfc->fn = mcs_new_function(vm);
        mainfc->fn->name = mcs_intern_c(vm, "<main>");
        mainfc->fn->source = c->src_name;
        c->fc = mainfc;
        c->line = 1;
        declare_local(c, "", 0, PT_ANY, 0xff);
        resolve_classes(c);
        int ncls = 0;
        for (classdecl_t* d = prog.classes; d; d = d->next) ncls++;
        classdecl_t** order = (classdecl_t**)arena_alloc(&arena, sizeof(classdecl_t*) * (size_t)(ncls + 1));
        int n = 0;
        for (classdecl_t* d = prog.classes; d; d = d->next) topo_visit(c, d, order, &n);
        for (int i = 0; i < n; i++) emit_class(c, order[i]);
        /* hoisted top-level functions */
        for (node_t* s = prog.stmts; s; s = s->next) {
            if (s->kind != N_LOCAL_FUNC) continue;
            c->line = s->line;
            emit_closure(c, s->fn, FK_FUNC, NULL, s->name, s->len, NULL);
            emit_op16(c, OP_SET_GLOBAL, global_slot(c, s->name, s->len));
            emit_op(c, OP_POP);
        }
        for (int i = 0; i < n; i++) emit_static_inits(c, order[i]);
        bool has_code = false;
        for (node_t* s = prog.stmts; s; s = s->next) {
            if (s->kind != N_LOCAL_FUNC && s->kind != N_EMPTY) has_code = true;
            compile_stmt(c, s);
        }
        if (!has_code) {
            for (int i = 0; i < n; i++) {
                member_t* m = find_member(order[i], "Main", 4, NULL);
                if (m && m->kind == M_METHOD && (m->mods & MOD_STATIC) && m->fn->body) {
                    emit_class_ref(c, order[i]);
                    if (m->fn->nparams) { emit_op8(c, OP_INT8, 0); emit_op8(c, OP_NEW_ARRAY, CV_NULL); }
                    emit_invoke(c, OP_INVOKE, "Main", 4, m->fn->nparams ? 1 : 0);
                    emit_op(c, OP_POP);
                    break;
                }
            }
        }
        emit_op(c, OP_RETURN_NULL);
        mainfc->fn->max_slots = (uint16_t)mainfc->max_locals;
        result = mainfc->fn;
    }
done:
    vm->panic = prev_panic;
    arena_free(&arena);
    vm->gc_pause--;
    return result;
}
#endif
