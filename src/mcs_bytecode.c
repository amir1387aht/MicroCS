/* MicroCS - bytecode buffers, disassembler and portable image format.
 *
 * Image layout (all integers little endian):
 *   "MCSB" u8 version u8 flags u16 reserved
 *   u32 nglobals { str }            names referenced by GET/SET_GLOBAL
 *   function                         the <main> function (recursive)
 * function:
 *   str name, str source, u8 arity, u8 min_arity, u8 upvalue_count, u8 flags,
 *   u16 max_slots, u8 has_ptypes [arity bytes],
 *   u32 nconst { u8 tag, payload }, u32 code_len code, u32 nlines {u32 pc,u32 line}
 * str: u16 len (0xFFFF = null) + bytes.
 * Global operands in the code are indices into the image name table and
 * are re-linked to VM global slots when the image is loaded.            */
#include "mcs_internal.h"
#include <stdio.h>

#define IMG_VERSION 2      /* v2 adds superinstructions; v1 images still load */
enum { K_NULL, K_FALSE, K_TRUE, K_INT, K_FLOAT, K_CHAR, K_STRING, K_FUNC };

/* ------------------------------------------------------------ emitting */
void mcs_fn_emit(mcs_vm_t* vm, mcs_function_t* fn, uint8_t byte, uint32_t line) {
    if (fn->code_len == fn->code_cap) {
        uint32_t nc = fn->code_cap < 16 ? 16 : fn->code_cap * 2;
        fn->code = MCS_GROW(vm, uint8_t, fn->code, fn->code_cap, nc);
        fn->code_cap = nc;
    }
#if MCS_ENABLE_LINES
    if (fn->line_count == 0 || fn->lines[fn->line_count - 1].line != line) {
        if (fn->line_count && fn->lines[fn->line_count - 1].pc == fn->code_len) {
            fn->lines[fn->line_count - 1].line = line;
        } else {
            if (fn->line_count == fn->line_cap) {
                uint32_t nc = fn->line_cap < 8 ? 8 : fn->line_cap * 2;
                fn->lines = MCS_GROW(vm, mcs_line_t, fn->lines, fn->line_cap, nc);
                fn->line_cap = nc;
            }
            fn->lines[fn->line_count].pc = fn->code_len;
            fn->lines[fn->line_count].line = line;
            fn->line_count++;
        }
    }
#else
    MCS_UNUSED(line);
#endif
    fn->code[fn->code_len++] = byte;
}

uint32_t mcs_fn_add_const(mcs_vm_t* vm, mcs_function_t* fn, mcs_value_t v) {
    for (uint32_t i = 0; i < fn->const_count; i++) {
        mcs_value_t k = fn->consts[i];
        if (k.type == v.type && mcs_values_same(k, v)) {
#if MCS_ENABLE_FLOAT
            /* keep 0.0 and -0.0 apart */
            if (v.type == MCS_T_FLOAT && memcmp(&k.as.f, &v.as.f, sizeof v.as.f) != 0) continue;
#endif
            return i;
        }
    }
    if (fn->const_count == fn->const_cap) {
        uint32_t nc = fn->const_cap < 8 ? 8 : fn->const_cap * 2;
        fn->consts = MCS_GROW(vm, mcs_value_t, fn->consts, fn->const_cap, nc);
        fn->const_cap = nc;
    }
    fn->consts[fn->const_count] = v;
    return fn->const_count++;
}

/* length of the instruction at code[pc] including operands */
static uint32_t insn_len(const mcs_function_t* fn, uint32_t pc) {
    uint8_t op = fn->code[pc];
    if (op >= OP__COUNT) return 0;
    uint32_t n = 1u + mcs_op_len[op];
    if (op == OP_CLOSURE && pc + 2 < fn->code_len) {
        uint16_t k = (uint16_t)((fn->code[pc + 1] << 8) | fn->code[pc + 2]);
        if (k < fn->const_count && IS_KIND(fn->consts[k], MCS_O_FUNCTION))
            n += 2u * AS_FUNCTION(fn->consts[k])->upvalue_count;
    }
    return n;
}

/* --------------------------------------------------------- disassembler */
#if MCS_ENABLE_DISASM
static void out(mcs_vm_t* vm, const char* fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
    if (n > 0) mcs_write(vm, buf, (size_t)n);
}

static void const_repr(mcs_vm_t* vm, mcs_value_t v, char* dst, size_t cap) {
    if (IS_STRING(v)) { snprintf(dst, cap, "\"%.40s\"%s", AS_CSTR(v), AS_STRING(v)->len > 40 ? "..." : ""); return; }
    if (IS_KIND(v, MCS_O_FUNCTION)) { snprintf(dst, cap, "<fn %s>", AS_FUNCTION(v)->name ? AS_FUNCTION(v)->name->chars : "?"); return; }
    if (v.type == MCS_T_INT) { mcs_format_int(dst, v.as.i); return; }
#if MCS_ENABLE_FLOAT
    if (v.type == MCS_T_FLOAT) { mcs_format_float(dst, v.as.f); return; }
#endif
    if (v.type == MCS_T_CHAR) { snprintf(dst, cap, "'\\u%04x'", (unsigned)v.as.i); return; }
    snprintf(dst, cap, "%s", mcs_type_name(vm, v));
}

void mcs_disassemble(mcs_vm_t* vm, mcs_function_t* fn, int depth) {
    out(vm, "%*s== %s  (arity %d, min %d, upvals %d, slots %d, %u bytes, %u consts) ==\n", depth * 2, "",
        fn->name ? fn->name->chars : "?", fn->arity, fn->min_arity, fn->upvalue_count, fn->max_slots,
        (unsigned)fn->code_len, (unsigned)fn->const_count);
    uint32_t pc = 0, last_line = 0;
    char rep[96];
    while (pc < fn->code_len) {
        uint8_t op = fn->code[pc];
        uint32_t len = insn_len(fn, pc);
        if (!len || pc + len > fn->code_len) { out(vm, "%*s%04u  <bad opcode %u>\n", depth * 2, "", (unsigned)pc, op); break; }
        const uint8_t* a = fn->code + pc + 1;
        uint32_t line = mcs_line_of(fn, pc);
        if (line != last_line) out(vm, "%*s%04u %4u  %-20s", depth * 2, "", (unsigned)pc, (unsigned)line, mcs_op_name[op]);
        else out(vm, "%*s%04u    |  %-20s", depth * 2, "", (unsigned)pc, mcs_op_name[op]);
        last_line = line;
        uint16_t u16 = (mcs_op_len[op] >= 2) ? (uint16_t)((a[0] << 8) | a[1]) : 0;
        switch (op) {
        case OP_CONST: case OP_GET_FIELD: case OP_SET_FIELD: case OP_IS: case OP_AS: case OP_CAST: case OP_IMPLEMENTS:
            const_repr(vm, fn->consts[u16], rep, sizeof rep); out(vm, "%5u  %s", u16, rep); break;
        case OP_GET_GLOBAL: case OP_SET_GLOBAL: case OP_SET_GLOBAL_POP:
        {
            uint16_t g = u16;
#if MCS_ENABLE_XIP
            if (fn->gmap) g = fn->gmap[u16];
#endif
            out(vm, "%5u  %s", u16, g < vm->global_count ? vm->global_names[g]->chars : "?"); break;
        }
        case OP_INVOKE: case OP_SUPER_INVOKE:
            out(vm, "%5u  %s argc=%u", u16, AS_CSTR(fn->consts[u16]), a[2]); break;
        case OP_CLASS: case OP_METHOD: case OP_STATIC: case OP_GETTER: case OP_SETTER:
            out(vm, "%5u  %s flags=%u", u16, AS_CSTR(fn->consts[u16]), a[2]); break;
        case OP_FIELD: {
            uint16_t k = (uint16_t)((a[2] << 8) | a[3]);
            if (k == 0xFFFF) strcpy(rep, "null"); else const_repr(vm, fn->consts[k], rep, sizeof rep);
            out(vm, "%5u  %s = %s", u16, AS_CSTR(fn->consts[u16]), rep); break;
        }
        case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_JUMP_IF_FALSE_KEEP:
        case OP_JUMP_IF_TRUE_KEEP: case OP_JUMP_IF_NULL_KEEP: case OP_JUMP_IF_NOT_NULL_KEEP: case OP_TRY:
        case OP_JF_EQ: case OP_JF_NE: case OP_JF_LT: case OP_JF_LE: case OP_JF_GT: case OP_JF_GE:
            out(vm, "-> %04u", (unsigned)(pc + 3 + u16)); break;
        case OP_LOOP: out(vm, "-> %04u", (unsigned)(pc + 3 - u16)); break;
        case OP_ARGC_JUMP: out(vm, "argc>=%u -> %04u", a[0], (unsigned)(pc + 4 + ((a[1] << 8) | a[2]))); break;
        case OP_FOR_ITER: out(vm, "slot %u, end -> %04u", a[0], (unsigned)(pc + 4 + ((a[1] << 8) | a[2]))); break;
        case OP_INC_LOCAL: out(vm, "slot %u += %d", a[0], (int8_t)a[1]); break;
        case OP_INT8: out(vm, "%d", (int8_t)a[0]); break;
        case OP_CLOSURE: {
            mcs_function_t* f = AS_FUNCTION(fn->consts[u16]);
            out(vm, "%5u  %s", u16, f->name ? f->name->chars : "?");
            for (uint32_t i = 0; i < f->upvalue_count; i++) out(vm, " %s%u", a[2 + i * 2] ? "L" : "U", a[3 + i * 2]);
            break;
        }
        case OP_ARRAY: out(vm, "count %u", u16); break;
        default:
            if (mcs_op_len[op] == 1) out(vm, "%u", a[0]);
            break;
        }
        out(vm, "\n");
        pc += len;
    }
    for (uint32_t i = 0; i < fn->const_count; i++)
        if (IS_KIND(fn->consts[i], MCS_O_FUNCTION)) mcs_disassemble(vm, AS_FUNCTION(fn->consts[i]), depth + 1);
}
#endif

/* ------------------------------------------------------------ image save */
#if MCS_ENABLE_COMPILER && MCS_ENABLE_BYTECODE_SAVE
typedef struct {
    mcs_vm_t* vm;
    mcs_buf_t b;
    uint32_t* gmap;     /* vm slot -> image index + 1 */
    uint32_t gmap_cap;
    mcs_string_t** gnames;
    uint32_t gcount, gcap;
    bool strip;
} saver_t;

static void w8(saver_t* s, uint8_t v) { mcs_buf_putc(&s->b, (char)v); }
static void w16(saver_t* s, uint16_t v) { w8(s, (uint8_t)v); w8(s, (uint8_t)(v >> 8)); }
static void w32(saver_t* s, uint32_t v) { w16(s, (uint16_t)v); w16(s, (uint16_t)(v >> 16)); }
static void w64(saver_t* s, uint64_t v) { w32(s, (uint32_t)v); w32(s, (uint32_t)(v >> 32)); }
static void wstr(saver_t* s, mcs_string_t* str) {
    if (!str) { w16(s, 0xFFFF); return; }
    uint16_t n = str->len > 0xFFFE ? 0xFFFE : (uint16_t)str->len;
    w16(s, n); mcs_buf_putn(&s->b, str->chars, n);
}

static uint16_t map_global(saver_t* s, uint16_t slot) {
    if (s->gmap[slot]) return (uint16_t)(s->gmap[slot] - 1);
    if (s->gcount == s->gcap) {
        uint32_t nc = s->gcap < 16 ? 16 : s->gcap * 2;
        s->gnames = MCS_GROW(s->vm, mcs_string_t*, s->gnames, s->gcap, nc);
        s->gcap = nc;
    }
    s->gnames[s->gcount] = s->vm->global_names[slot];
    s->gmap[slot] = ++s->gcount;
    return (uint16_t)(s->gcount - 1);
}

/* first pass: register all globals so the table can be written up front */
static void scan_globals(saver_t* s, mcs_function_t* fn) {
    for (uint32_t pc = 0; pc < fn->code_len;) {
        uint8_t op = fn->code[pc];
        uint32_t len = insn_len(fn, pc);
        if (!len) break;
        if (op == OP_GET_GLOBAL || op == OP_SET_GLOBAL || op == OP_SET_GLOBAL_POP) map_global(s, (uint16_t)((fn->code[pc + 1] << 8) | fn->code[pc + 2]));
        pc += len;
    }
    for (uint32_t i = 0; i < fn->const_count; i++)
        if (IS_KIND(fn->consts[i], MCS_O_FUNCTION)) scan_globals(s, AS_FUNCTION(fn->consts[i]));
}

static void write_fn(saver_t* s, mcs_function_t* fn) {
    wstr(s, fn->name); wstr(s, s->strip ? NULL : fn->source);
    w8(s, fn->arity); w8(s, fn->min_arity); w8(s, fn->upvalue_count); w8(s, fn->flags);
    w16(s, fn->max_slots);
    w8(s, fn->param_types ? 1 : 0);
    if (fn->param_types) for (int i = 0; i < fn->arity; i++) w8(s, fn->param_types[i]);
    w32(s, fn->const_count);
    for (uint32_t i = 0; i < fn->const_count; i++) {
        mcs_value_t v = fn->consts[i];
        switch (v.type) {
        case MCS_T_NULL: w8(s, K_NULL); break;
        case MCS_T_BOOL: w8(s, v.as.b ? K_TRUE : K_FALSE); break;
        case MCS_T_INT: w8(s, K_INT); w64(s, (uint64_t)(int64_t)v.as.i); break;
        case MCS_T_CHAR: w8(s, K_CHAR); w32(s, (uint32_t)v.as.i); break;
#if MCS_ENABLE_FLOAT
        case MCS_T_FLOAT: { double d = (double)v.as.f; uint64_t u; memcpy(&u, &d, 8); w8(s, K_FLOAT); w64(s, u); break; }
#endif
        default:
            if (IS_STRING(v)) { w8(s, K_STRING); wstr(s, AS_STRING(v)); }
            else if (IS_KIND(v, MCS_O_FUNCTION)) { w8(s, K_FUNC); write_fn(s, AS_FUNCTION(v)); }
            else w8(s, K_NULL);
        }
    }
    w32(s, fn->code_len);
    for (uint32_t pc = 0; pc < fn->code_len;) {
        uint8_t op = fn->code[pc];
        uint32_t len = insn_len(fn, pc);
        if (!len) len = 1;
        if (op == OP_GET_GLOBAL || op == OP_SET_GLOBAL || op == OP_SET_GLOBAL_POP) {
            uint16_t g = map_global(s, (uint16_t)((fn->code[pc + 1] << 8) | fn->code[pc + 2]));
            w8(s, op); w8(s, (uint8_t)(g >> 8)); w8(s, (uint8_t)g);
        } else {
            mcs_buf_putn(&s->b, (const char*)fn->code + pc, len);
        }
        pc += len;
    }
    if (s->strip) { w32(s, 0); return; }
    w32(s, fn->line_count);
    for (uint32_t i = 0; i < fn->line_count; i++) { w32(s, fn->lines[i].pc); w32(s, fn->lines[i].line); }
}

mcs_result_t mcs_compile_image(mcs_vm_t* vm, const char* name, const char* src, bool strip_lines, uint8_t** out_img, size_t* out_len) {
    *out_img = NULL; *out_len = 0;
    vm->error[0] = 0;
    mcs_function_t* fn = mcs_compile(vm, name, src);
    if (!fn) return MCS_ERR_COMPILE;
    vm->gc_pause++;
    saver_t s; memset(&s, 0, sizeof s);
    s.vm = vm; s.strip = strip_lines;
    mcs_buf_init(&s.b, vm);
    s.gmap_cap = vm->global_count;
    s.gmap = MCS_ALLOC(vm, uint32_t, s.gmap_cap ? s.gmap_cap : 1);
    memset(s.gmap, 0, sizeof(uint32_t) * (s.gmap_cap ? s.gmap_cap : 1));
    scan_globals(&s, fn);
    mcs_buf_puts(&s.b, "MCSB");
    w8(&s, IMG_VERSION);
    w8(&s, (uint8_t)((MCS_INT64 ? 1 : 0) | (MCS_ENABLE_FLOAT ? 2 : 0) | (MCS_FLOAT_DOUBLE ? 4 : 0)));
    w16(&s, 0);
    w32(&s, s.gcount);
    for (uint32_t i = 0; i < s.gcount; i++) wstr(&s, s.gnames[i]);
    write_fn(&s, fn);
    MCS_FREE(vm, uint32_t, s.gmap, s.gmap_cap ? s.gmap_cap : 1);
    if (s.gnames) MCS_FREE(vm, mcs_string_t*, s.gnames, s.gcap);
    /* hand the buffer to the caller as a plain allocation of exact size */
    /* 8-byte size header in front so mcs_free_image can release exactly */
    uint8_t* raw = (uint8_t*)mcs_realloc(vm, NULL, 0, s.b.len + 8);
    uint64_t sz = s.b.len;
    memcpy(raw, &sz, 8);
    memcpy(raw + 8, s.b.data, s.b.len);
    *out_img = raw + 8; *out_len = s.b.len;
    mcs_buf_free(&s.b);
    vm->gc_pause--;
    return MCS_OK;
}

void mcs_free_image(mcs_vm_t* vm, uint8_t* image) {
    if (!image) return;
    uint64_t sz; memcpy(&sz, image - 8, 8);
    mcs_realloc(vm, image - 8, (size_t)sz + 8, 0);
}
#endif

/* ------------------------------------------------------------ image load */
#if MCS_ENABLE_BYTECODE_LOAD
typedef struct {
    mcs_vm_t* vm;
    const uint8_t* p;
    const uint8_t* end;
    uint16_t* gslots;
    uint32_t gcount;
    bool bad;
    bool xip;
#if MCS_ENABLE_XIP
    mcs_string_t* gmap_obj;
#endif
    int depth;
} loader_t;

static bool need(loader_t* l, size_t n) {
    if (l->bad || (size_t)(l->end - l->p) < n) { l->bad = true; return false; }
    return true;
}
static uint8_t r8(loader_t* l) { if (!need(l, 1)) return 0; return *l->p++; }
static uint16_t r16(loader_t* l) { if (!need(l, 2)) return 0; uint16_t v = (uint16_t)(l->p[0] | (l->p[1] << 8)); l->p += 2; return v; }
static uint32_t r32(loader_t* l) { uint32_t lo = r16(l); return lo | ((uint32_t)r16(l) << 16); }
static uint64_t r64(loader_t* l) { uint64_t lo = r32(l); return lo | ((uint64_t)r32(l) << 32); }
static mcs_string_t* rstr(loader_t* l) {
    uint16_t n = r16(l);
    if (n == 0xFFFF || l->bad) return NULL;
    if (!need(l, n)) return NULL;
    mcs_string_t* s = mcs_intern(l->vm, (const char*)l->p, n);
    l->p += n;
    return s;
}

/* Second validation pass: operand ranges that the interpreter trusts.
   Checks local/upvalue indices and that every branch lands on an
   instruction boundary inside the function. Stack balance is NOT verified;
   images must still come from a trusted compiler (see docs/SECURITY.md). */
static bool validate_code(mcs_vm_t* vm, mcs_function_t* fn) {
    uint32_t clen = fn->code_len;
    if (!clen) return true;
    uint32_t nbytes = (clen + 8) / 8;
    uint8_t* starts = MCS_ALLOC(vm, uint8_t, nbytes);
    if (!starts) return false;
    memset(starts, 0, nbytes);
    for (uint32_t pc = 0; pc < clen; pc += insn_len(fn, pc)) starts[pc >> 3] |= (uint8_t)(1u << (pc & 7));
#define IS_START(t) ((t) < clen && (starts[(t) >> 3] & (1u << ((t) & 7))))
    bool ok = true;
    for (uint32_t pc = 0; pc < clen && ok;) {
        uint8_t op = fn->code[pc];
        uint32_t len = insn_len(fn, pc);
        const uint8_t* a = fn->code + pc + 1;
        uint32_t u16 = (mcs_op_len[op] >= 2) ? (uint32_t)((a[0] << 8) | a[1]) : 0;
        switch (op) {
        case OP_GET_LOCAL: case OP_SET_LOCAL: case OP_SET_LOCAL_POP: case OP_INC_LOCAL:
            ok = a[0] < fn->max_slots; break;
        case OP_FOR_ITER:
            ok = (uint32_t)a[0] + 1 < fn->max_slots && IS_START(pc + 4 + (uint32_t)((a[1] << 8) | a[2])); break;
        case OP_GET_UPVAL: case OP_SET_UPVAL:
            ok = a[0] < fn->upvalue_count; break;
        case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_JUMP_IF_FALSE_KEEP:
        case OP_JUMP_IF_TRUE_KEEP: case OP_JUMP_IF_NULL_KEEP: case OP_JUMP_IF_NOT_NULL_KEEP: case OP_TRY:
        case OP_JF_EQ: case OP_JF_NE: case OP_JF_LT: case OP_JF_LE: case OP_JF_GT: case OP_JF_GE:
            ok = IS_START(pc + 3 + u16); break;
        case OP_LOOP:
            ok = u16 <= pc + 3 && IS_START(pc + 3 - u16); break;
        case OP_ARGC_JUMP:
            ok = IS_START(pc + 4 + (uint32_t)((a[1] << 8) | a[2])); break;
        case OP_CLOSURE: {
            mcs_function_t* f = AS_FUNCTION(fn->consts[u16]);
            for (uint32_t i = 0; i < f->upvalue_count && ok; i++) {
                uint8_t is_local = a[2 + i * 2], idx = a[3 + i * 2];
                ok = is_local ? idx < fn->max_slots : idx < fn->upvalue_count;
            }
            break;
        }
        default: break;
        }
        pc += len;
    }
#undef IS_START
    MCS_FREE(vm, uint8_t, starts, nbytes);
    return ok;
}

static mcs_function_t* read_fn(loader_t* l) {
    mcs_vm_t* vm = l->vm;
    if (++l->depth > 64) { l->bad = true; return NULL; }
    mcs_function_t* fn = mcs_new_function(vm);
    fn->name = rstr(l); fn->source = rstr(l);
    fn->arity = r8(l); fn->min_arity = r8(l); fn->upvalue_count = r8(l); fn->flags = (uint8_t)(r8(l) & ~FN_XIP);
    fn->max_slots = r16(l);
    if (r8(l)) {
        fn->param_types = MCS_ALLOC(vm, uint8_t, fn->arity ? fn->arity : 1);
        for (int i = 0; i < fn->arity; i++) fn->param_types[i] = r8(l);
    }
    uint32_t nk = r32(l);
    if (l->bad || nk > 65536) { l->bad = true; return NULL; }
    for (uint32_t i = 0; i < nk && !l->bad; i++) {
        mcs_value_t v = mcs_null();
        switch (r8(l)) {
        case K_NULL: break;
        case K_FALSE: v = mcs_bool(false); break;
        case K_TRUE: v = mcs_bool(true); break;
        case K_INT: v = mcs_int((mcs_int_t)(int64_t)r64(l)); break;
        case K_CHAR: v = mcs_char(r32(l)); break;
        case K_FLOAT: {
            uint64_t u = r64(l);
#if MCS_ENABLE_FLOAT
            double d; memcpy(&d, &u, 8); v = mcs_float((mcs_float_t)d);
#else
            MCS_UNUSED(u);
            snprintf(vm->error, sizeof vm->error, "bytecode uses floating point but MCS_ENABLE_FLOAT=0");
            l->bad = true;
#endif
            break;
        }
        case K_STRING: { mcs_string_t* s = rstr(l); if (!s) { l->bad = true; break; } v = OBJ_VAL(s); break; }
        case K_FUNC: { mcs_function_t* f = read_fn(l); if (!f) { l->bad = true; break; } v = OBJ_VAL(f); break; }
        default: l->bad = true; break;
        }
        /* append without dedupe to keep indices stable */
        if (fn->const_count == fn->const_cap) {
            uint32_t nc = fn->const_cap < 8 ? 8 : fn->const_cap * 2;
            fn->consts = MCS_GROW(vm, mcs_value_t, fn->consts, fn->const_cap, nc);
            fn->const_cap = nc;
        }
        fn->consts[fn->const_count++] = v;
    }
    if (l->bad) return NULL;
    uint32_t clen = r32(l);
    if (!need(l, clen)) return NULL;
#if MCS_ENABLE_XIP
    if (l->xip && clen) {   /* execute in place: the image buffer outlives the VM's use of it */
        fn->code = (uint8_t*)(uintptr_t)l->p;
        fn->code_cap = 0; fn->code_len = clen;
        fn->flags |= FN_XIP;
        fn->gmap = (const uint16_t*)(const void*)l->gmap_obj->chars;
        fn->gmap_obj = l->gmap_obj;
    } else
#endif
    {
        fn->code = MCS_ALLOC(vm, uint8_t, clen ? clen : 1);
        fn->code_cap = clen ? clen : 1; fn->code_len = clen;
        memcpy(fn->code, l->p, clen);
    }
    l->p += clen;
    /* validate & relink */
    for (uint32_t pc = 0; pc < clen;) {
        uint8_t op = fn->code[pc];
        uint32_t len = insn_len(fn, pc);
        if (!len || pc + len > clen) { l->bad = true; return NULL; }
        if (op == OP_GET_GLOBAL || op == OP_SET_GLOBAL || op == OP_SET_GLOBAL_POP) {
            uint16_t g = (uint16_t)((fn->code[pc + 1] << 8) | fn->code[pc + 2]);
            if (g >= l->gcount) { l->bad = true; return NULL; }
            if (!(fn->flags & FN_XIP)) {   /* XIP code keeps image indices; fn->gmap maps them */
                uint16_t s = l->gslots[g];
                fn->code[pc + 1] = (uint8_t)(s >> 8); fn->code[pc + 2] = (uint8_t)s;
            }
        } else if (op == OP_CONST || op == OP_CLOSURE || op == OP_GET_FIELD || op == OP_SET_FIELD || op == OP_INVOKE ||
                   op == OP_SUPER_INVOKE || op == OP_CLASS || op == OP_METHOD || op == OP_STATIC || op == OP_GETTER ||
                   op == OP_SETTER || op == OP_IS || op == OP_AS || op == OP_CAST || op == OP_IMPLEMENTS || op == OP_FIELD) {
            uint16_t k = (uint16_t)((fn->code[pc + 1] << 8) | fn->code[pc + 2]);
            if (k >= fn->const_count) { l->bad = true; return NULL; }
            if (op == OP_CLOSURE && !IS_KIND(fn->consts[k], MCS_O_FUNCTION)) { l->bad = true; return NULL; }
            if (op != OP_CONST && op != OP_CLOSURE && !IS_STRING(fn->consts[k])) { l->bad = true; return NULL; }
        }
        pc += len;
    }
    if (!validate_code(vm, fn)) { l->bad = true; return NULL; }
    uint32_t nl = r32(l);
    if (nl && need(l, (size_t)nl * 8)) {
#if MCS_ENABLE_LINES
        fn->lines = MCS_ALLOC(vm, mcs_line_t, nl);
        fn->line_cap = fn->line_count = nl;
        for (uint32_t i = 0; i < nl; i++) { fn->lines[i].pc = r32(l); fn->lines[i].line = r32(l); }
#else
        l->p += (size_t)nl * 8;
#endif
    }
    l->depth--;
    return l->bad ? NULL : fn;
}

mcs_function_t* mcs_load_image_ex(mcs_vm_t* vm, const uint8_t* img, size_t len, bool xip) {
    if (!img || len < 12 || memcmp(img, "MCSB", 4) != 0) {
        snprintf(vm->error, sizeof vm->error, "not a MicroCS bytecode image");
        return NULL;
    }
    if (img[4] != IMG_VERSION && img[4] != 1) {
        snprintf(vm->error, sizeof vm->error, "bytecode version %u not supported (expected %u)", img[4], IMG_VERSION);
        return NULL;
    }
    uint8_t flags = img[5];
#if !MCS_INT64
    if (flags & 1) { /* 64-bit image on 32-bit VM: allowed, constants are truncated */ }
#endif
    MCS_UNUSED(flags);
    loader_t l; memset(&l, 0, sizeof l);
    l.vm = vm; l.p = img + 8; l.end = img + len; l.xip = xip;
    vm->gc_pause++;
    l.gcount = r32(&l);
    if (l.gcount > 65535) l.bad = true;
    if (!l.bad && l.gcount) {
        l.gslots = MCS_ALLOC(vm, uint16_t, l.gcount);
        for (uint32_t i = 0; i < l.gcount && !l.bad; i++) {
            mcs_string_t* s = rstr(&l);
            if (!s) { l.bad = true; break; }
            l.gslots[i] = (uint16_t)mcs_global_slot(vm, s);
        }
    }
#if MCS_ENABLE_XIP
    if (!l.bad && xip) {   /* global map as a GC-owned, non-interned string blob */
        size_t nb = (size_t)(l.gcount ? l.gcount : 1) * sizeof(uint16_t);
        mcs_string_t* m = (mcs_string_t*)mcs_alloc_obj(vm, sizeof(mcs_string_t) + nb, MCS_O_STRING);
        m->len = (uint32_t)nb; m->hash = 0;
        memset(m->chars, 0, nb + 1);
        if (l.gcount) memcpy(m->chars, l.gslots, nb);
        l.gmap_obj = m;
    }
#endif
    mcs_function_t* fn = l.bad ? NULL : read_fn(&l);
    if (l.gslots) MCS_FREE(vm, uint16_t, l.gslots, l.gcount);
    vm->gc_pause--;
    if (!fn || l.bad) {
        if (!vm->error[0]) snprintf(vm->error, sizeof vm->error, "corrupt bytecode image");
        return NULL;
    }
    return fn;
}
mcs_function_t* mcs_load_image(mcs_vm_t* vm, const uint8_t* img, size_t len) { return mcs_load_image_ex(vm, img, len, false); }
#endif
