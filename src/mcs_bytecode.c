/* MicroCS - bytecode buffers, disassembler and portable image format.
 *
 * Image layout v3 (compact; vN = unsigned LEB128 varint, zN = zig-zag varint):
 *   "MCSB" u8 version u8 flags u16 reserved
 *   vN nglobals { str }             names referenced by GET/SET_GLOBAL
 *   function                         the <main> function (recursive)
 * function:
 *   str name, str source, u8 arity, u8 min_arity, u8 upvalue_count, u8 flags,
 *   vN max_slots, u8 has_ptypes [arity bytes],
 *   vN nconst { u8 tag, payload }, vN code_len code,
 *   vN nlines { vN pc delta, zN line delta }
 * constants: K_INT zN, K_CHAR vN, K_FLOAT 8-byte double, K_FLOAT32 4-byte
 *   float (exactly representable values), K_STRING str, K_FUNC function.
 * str: vN v; 0 = null, odd = the (v>>1)-th string already in the image
 *   (names, sources and constants are stored once), even = (v>>1)-1 bytes.
 * v1/v2 images (fixed-width u16/u32/u64 fields, no string sharing) still load.
 * Global operands in the code are indices into the image name table and
 * are re-linked to VM global slots when the image is loaded.            */
#include "mcs_internal.h"
#include <stdio.h>

#define IMG_VERSION 3      /* v2 added superinstructions, v3 the compact encoding */
enum { K_NULL, K_FALSE, K_TRUE, K_INT, K_FLOAT, K_CHAR, K_STRING, K_FUNC, K_FLOAT32 };

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
uint32_t mcs_insn_len(const mcs_function_t* fn, uint32_t pc) {
    uint8_t op = fn->code[pc];
    if (op >= OP_RT_COUNT) return 0;
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

#if MCS_ENABLE_SUPEROPS
static const char* const bx_sym[BX__COUNT] = { "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>", ">>>" };
static const char* const jx_sym[JX__COUNT] = { "==", "!=", "<", "<=", ">", ">=" };
#endif
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
        uint32_t len = mcs_insn_len(fn, pc);
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
#if MCS_ENABLE_SUPEROPS
        case OP_BIN_LL: case OP_BIN_LK: case OP_BIN_SL: case OP_BIN_SK: case OP_BIN_LLS: case OP_BIN_LKS: {
            const char* o = a[0] < BX__COUNT ? bx_sym[a[0]] : "?";
            char lhs[16], rhs[64];
            if (op == OP_BIN_SL || op == OP_BIN_SK) strcpy(lhs, "top"); else snprintf(lhs, sizeof lhs, "L%u", a[1]);
            if (op == OP_BIN_LL || op == OP_BIN_LLS) snprintf(rhs, sizeof rhs, "L%u", a[2]);
            else if (op == OP_BIN_SL) snprintf(rhs, sizeof rhs, "L%u", a[1]);
            else {
                uint16_t k = op == OP_BIN_SK ? (uint16_t)((a[1] << 8) | a[2]) : (uint16_t)((a[2] << 8) | a[3]);
                const_repr(vm, fn->consts[k], rhs, sizeof rhs);
            }
            if (op == OP_BIN_LLS) out(vm, "L%u = %s %s %s", a[3], lhs, o, rhs);
            else if (op == OP_BIN_LKS) out(vm, "L%u = %s %s %s", a[4], lhs, o, rhs);
            else out(vm, "%s %s %s", lhs, o, rhs);
            break;
        }
        case OP_JF_LK: case OP_JF_SL: case OP_JF_SK: case OP_JB_LK: {
            const char* c = a[0] < JX__COUNT ? jx_sym[a[0]] : "?";
            char lhs[16], rhs[64];
            uint32_t len = mcs_op_len[op] + 1;
            uint16_t o = (uint16_t)((a[len - 3] << 8) | a[len - 2]);
            if (op == OP_JF_SL || op == OP_JF_SK) strcpy(lhs, "pop"); else snprintf(lhs, sizeof lhs, "L%u", a[1]);
            if (op == OP_JF_SL) snprintf(rhs, sizeof rhs, "L%u", a[1]);
            else {
                uint16_t k = op == OP_JF_SK ? (uint16_t)((a[1] << 8) | a[2]) : (uint16_t)((a[2] << 8) | a[3]);
                const_repr(vm, fn->consts[k], rhs, sizeof rhs);
            }
            if (op == OP_JB_LK) out(vm, "if %s %s %s -> %04u", lhs, c, rhs, (unsigned)(pc + len - o));
            else out(vm, "unless %s %s %s -> %04u", lhs, c, rhs, (unsigned)(pc + len + o));
            break;
        }
        case OP_BIN_LIS: out(vm, "L%u = L%u %s %d", a[3], a[1], a[0] < BX__COUNT ? bx_sym[a[0]] : "?", (int8_t)a[2]); break;
        case OP_ACC: out(vm, "L%u %s= pop", a[1], a[0] < BX__COUNT ? bx_sym[a[0]] : "?"); break;
        case OP_SETF_L: {
            uint16_t k = (uint16_t)((a[1] << 8) | a[2]);
            const_repr(vm, fn->consts[k], rep, sizeof rep); out(vm, "L%u.%s = pop", a[0], rep); break;
        }
        case OP_GET_INDEX_LL: out(vm, "L%u[L%u]", a[0], a[1]); break;
        case OP_SET_INDEX_LL: out(vm, "L%u[L%u] = pop", a[0], a[1]); break;
        case OP_ACC_ADD: out(vm, "L%u += pop", a[0]); break;
        case OP_ACC_SUB: out(vm, "L%u -= pop", a[0]); break;
        case OP_GET_FIELD_L: {
            uint16_t k = (uint16_t)((a[1] << 8) | a[2]);
            const_repr(vm, fn->consts[k], rep, sizeof rep); out(vm, "L%u.%s", a[0], rep); break;
        }
#endif
        default:
#if MCS_ENABLE_SUPEROPS
            if (OP_IS_LI(op)) { out(vm, "L%u %s %d", a[0], bx_sym[op - OP_LI_ADD], (int8_t)a[1]); break; }
            if (OP_IS_SI(op)) { out(vm, "top %s %d", bx_sym[op - OP_SI_ADD], (int8_t)a[0]); break; }
            if (OP_IS_JFLI(op)) { out(vm, "unless L%u %s %d -> %04u", a[0], jx_sym[op - OP_JFLI_EQ], (int8_t)a[1], (unsigned)(pc + 5 + ((a[2] << 8) | a[3]))); break; }
            if (OP_IS_JBLI(op)) { out(vm, "if L%u %s %d -> %04u", a[0], jx_sym[op - OP_JBLI_EQ], (int8_t)a[1], (unsigned)(pc + 5 - ((a[2] << 8) | a[3]))); break; }
            if (OP_IS_JFSI(op)) { out(vm, "unless pop %s %d -> %04u", jx_sym[op - OP_JFSI_EQ], (int8_t)a[0], (unsigned)(pc + 4 + ((a[1] << 8) | a[2]))); break; }
            if (OP_IS_JFLL(op)) { out(vm, "unless L%u %s L%u -> %04u", a[0], jx_sym[op - OP_JFLL_EQ], a[1], (unsigned)(pc + 5 + ((a[2] << 8) | a[3]))); break; }
            if (OP_IS_JBLL(op)) { out(vm, "if L%u %s L%u -> %04u", a[0], jx_sym[op - OP_JBLL_EQ], a[1], (unsigned)(pc + 5 - ((a[2] << 8) | a[3]))); break; }
#endif
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
    mcs_string_t** strs;   /* strings already written (shared by reference) */
    uint32_t scount, scap;
    bool strip;
} saver_t;

static void w8(saver_t* s, uint8_t v) { mcs_buf_putc(&s->b, (char)v); }
static void w16(saver_t* s, uint16_t v) { w8(s, (uint8_t)v); w8(s, (uint8_t)(v >> 8)); }
#if MCS_ENABLE_FLOAT
static void w32(saver_t* s, uint32_t v) { w16(s, (uint16_t)v); w16(s, (uint16_t)(v >> 16)); }
#endif
static void wv(saver_t* s, uint64_t v) {
    while (v >= 0x80) { w8(s, (uint8_t)(v | 0x80)); v >>= 7; }
    w8(s, (uint8_t)v);
}
static void wz(saver_t* s, int64_t v) { wv(s, ((uint64_t)v << 1) ^ (uint64_t)(v >> 63)); }
static void wstr(saver_t* s, mcs_string_t* str) {
    if (!str) { wv(s, 0); return; }
    for (uint32_t i = 0; i < s->scount; i++)   /* strings are interned: compare pointers */
        if (s->strs[i] == str) { wv(s, ((uint64_t)i << 1) | 1); return; }
    uint32_t n = str->len > 0xFFFE ? 0xFFFE : str->len;
    wv(s, (uint64_t)(n + 1) << 1); mcs_buf_putn(&s->b, str->chars, n);
    if (s->scount == s->scap) {
        uint32_t nc = s->scap < 32 ? 32 : s->scap * 2;
        s->strs = MCS_GROW(s->vm, mcs_string_t*, s->strs, s->scap, nc);
        s->scap = nc;
    }
    s->strs[s->scount++] = str;
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
        uint32_t len = mcs_insn_len(fn, pc);
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
    wv(s, fn->max_slots);
    w8(s, fn->param_types ? 1 : 0);
    if (fn->param_types) for (int i = 0; i < fn->arity; i++) w8(s, fn->param_types[i]);
    wv(s, fn->const_count);
    for (uint32_t i = 0; i < fn->const_count; i++) {
        mcs_value_t v = fn->consts[i];
        switch (v.type) {
        case MCS_T_NULL: w8(s, K_NULL); break;
        case MCS_T_BOOL: w8(s, v.as.b ? K_TRUE : K_FALSE); break;
        case MCS_T_INT: w8(s, K_INT); wz(s, (int64_t)v.as.i); break;
        case MCS_T_CHAR: w8(s, K_CHAR); wv(s, (uint32_t)v.as.i); break;
#if MCS_ENABLE_FLOAT
        case MCS_T_FLOAT: {
            double d = (double)v.as.f; float f = (float)d;
            if ((double)f == d) { uint32_t u; memcpy(&u, &f, 4); w8(s, K_FLOAT32); w32(s, u); }
            else { uint64_t u; memcpy(&u, &d, 8); w8(s, K_FLOAT); w32(s, (uint32_t)u); w32(s, (uint32_t)(u >> 32)); }
            break;
        }
#endif
        default:
            if (IS_STRING(v)) { w8(s, K_STRING); wstr(s, AS_STRING(v)); }
            else if (IS_KIND(v, MCS_O_FUNCTION)) { w8(s, K_FUNC); write_fn(s, AS_FUNCTION(v)); }
            else w8(s, K_NULL);
        }
    }
    wv(s, fn->code_len);
    for (uint32_t pc = 0; pc < fn->code_len;) {
        uint8_t op = fn->code[pc];
        uint32_t len = mcs_insn_len(fn, pc);
        if (!len) len = 1;
        if (op == OP_GET_GLOBAL || op == OP_SET_GLOBAL || op == OP_SET_GLOBAL_POP) {
            uint16_t g = map_global(s, (uint16_t)((fn->code[pc + 1] << 8) | fn->code[pc + 2]));
            w8(s, op); w8(s, (uint8_t)(g >> 8)); w8(s, (uint8_t)g);
        } else {
            mcs_buf_putn(&s->b, (const char*)fn->code + pc, len);
        }
        pc += len;
    }
    if (s->strip) { wv(s, 0); return; }
    wv(s, fn->line_count);
    uint32_t lpc = 0, lline = 0;
    for (uint32_t i = 0; i < fn->line_count; i++) {
        wv(s, fn->lines[i].pc - lpc); wz(s, (int64_t)fn->lines[i].line - (int64_t)lline);
        lpc = fn->lines[i].pc; lline = fn->lines[i].line;
    }
}

mcs_result_t mcs_compile_image(mcs_vm_t* vm, const char* name, const char* src, bool strip_lines, uint8_t** out_img, size_t* out_len) {
    return mcs_compile_image_ex(vm, name, src, strip_lines ? MCS_IMAGE_STRIP : 0, out_img, out_len);
}

mcs_result_t mcs_compile_image_ex(mcs_vm_t* vm, const char* name, const char* src, unsigned flags, uint8_t** out_img, size_t* out_len) {
    bool strip_lines = (flags & MCS_IMAGE_STRIP) != 0;
    *out_img = NULL; *out_len = 0;
    vm->error[0] = 0;
    mcs_function_t* fn = mcs_compile(vm, name, src);
    if (!fn) return MCS_ERR_COMPILE;
    vm->gc_pause++;
#if MCS_ENABLE_OPTIMIZER
    if (!(flags & MCS_IMAGE_NO_OPT)) mcs_optimize(vm, fn, 0);
#endif
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
    wv(&s, s.gcount);
    for (uint32_t i = 0; i < s.gcount; i++) wstr(&s, s.gnames[i]);
    write_fn(&s, fn);
    MCS_FREE(vm, uint32_t, s.gmap, s.gmap_cap ? s.gmap_cap : 1);
    if (s.gnames) MCS_FREE(vm, mcs_string_t*, s.gnames, s.gcap);
    if (s.strs) MCS_FREE(vm, mcs_string_t*, s.strs, s.scap);
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
    bool v3;               /* compact encoding */
    mcs_string_t** strs;   /* v3: strings read so far */
    uint32_t scount, scap;
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
static uint64_t rv(loader_t* l) {
    uint64_t v = 0;
    for (unsigned sh = 0; sh < 64; sh += 7) {
        uint8_t c = r8(l);
        v |= (uint64_t)(c & 0x7F) << sh;
        if (!(c & 0x80)) return v;
    }
    l->bad = true; return 0;
}
static int64_t rz(loader_t* l) { uint64_t v = rv(l); return (int64_t)(v >> 1) ^ -(int64_t)(v & 1); }
/* counts and sizes: varint in v3, u32 before */
static uint32_t rn(loader_t* l) {
    if (!l->v3) return r32(l);
    uint64_t v = rv(l);
    if (v > 0xFFFFFFFFu) { l->bad = true; return 0; }
    return (uint32_t)v;
}
static mcs_string_t* rstr(loader_t* l) {
    uint32_t n;
    if (l->v3) {
        uint64_t v = rv(l);
        if (!v || l->bad) return NULL;
        if (v & 1) {
            if ((v >> 1) >= l->scount) { l->bad = true; return NULL; }
            return l->strs[v >> 1];
        }
        if ((v >> 1) - 1 > 0xFFFE) { l->bad = true; return NULL; }
        n = (uint32_t)(v >> 1) - 1;
    } else {
        n = r16(l);
        if (n == 0xFFFF || l->bad) return NULL;
    }
    if (!need(l, n)) return NULL;
    mcs_string_t* s = mcs_intern(l->vm, (const char*)l->p, n);
    l->p += n;
    if (l->v3) {
        if (l->scount == l->scap) {
            uint32_t nc = l->scap < 32 ? 32 : l->scap * 2;
            l->strs = MCS_GROW(l->vm, mcs_string_t*, l->strs, l->scap, nc);
            l->scap = nc;
        }
        l->strs[l->scount++] = s;
    }
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
    for (uint32_t pc = 0; pc < clen; pc += mcs_insn_len(fn, pc)) starts[pc >> 3] |= (uint8_t)(1u << (pc & 7));
#define IS_START(t) ((t) < clen && (starts[(t) >> 3] & (1u << ((t) & 7))))
    bool ok = true;
    for (uint32_t pc = 0; pc < clen && ok;) {
        uint8_t op = fn->code[pc];
        uint32_t len = mcs_insn_len(fn, pc);
        const uint8_t* a = fn->code + pc + 1;
        uint32_t u16 = (op < OP_RT_COUNT && mcs_op_len[op] >= 2) ? (uint32_t)((a[0] << 8) | a[1]) : 0;
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
#if MCS_ENABLE_SUPEROPS
        /* fused forms: a[0] = sub-op, then slots / constants / offset */
        case OP_BIN_LL: ok = a[0] < BX__COUNT && a[1] < fn->max_slots && a[2] < fn->max_slots; break;
        case OP_BIN_LK: ok = a[0] < BX__COUNT && a[1] < fn->max_slots; break;
        case OP_BIN_SL: ok = a[0] < BX__COUNT && a[1] < fn->max_slots; break;
        case OP_BIN_SK: ok = a[0] < BX__COUNT; break;
        case OP_BIN_LLS: ok = a[0] < BX__COUNT && a[1] < fn->max_slots && a[2] < fn->max_slots && a[3] < fn->max_slots; break;
        case OP_BIN_LKS: ok = a[0] < BX__COUNT && a[1] < fn->max_slots && a[4] < fn->max_slots; break;
        case OP_BIN_LIS: ok = a[0] < BX__COUNT && a[1] < fn->max_slots && a[3] < fn->max_slots; break;
        case OP_ACC: ok = a[0] < BX__COUNT && a[1] < fn->max_slots; break;
        case OP_JF_LK: ok = a[0] < JX__COUNT && a[1] < fn->max_slots &&
                            IS_START(pc + len + (uint32_t)((a[4] << 8) | a[5])); break;
        case OP_JF_SL: ok = a[0] < JX__COUNT && a[1] < fn->max_slots &&
                            IS_START(pc + len + (uint32_t)((a[2] << 8) | a[3])); break;
        case OP_JF_SK: ok = a[0] < JX__COUNT && IS_START(pc + len + (uint32_t)((a[3] << 8) | a[4])); break;
        case OP_JB_LK: { uint32_t o = (uint32_t)((a[4] << 8) | a[5]);
            ok = a[0] < JX__COUNT && a[1] < fn->max_slots && o <= pc + len && IS_START(pc + len - o); break; }
        case OP_GET_FIELD_L: case OP_SETF_L: case OP_RETURN_LOCAL: case OP_ACC_ADD: case OP_ACC_SUB: ok = a[0] < fn->max_slots; break;
        case OP_GET_INDEX_LL: case OP_SET_INDEX_LL: ok = a[0] < fn->max_slots && a[1] < fn->max_slots; break;
#endif
        case OP_CLOSURE: {
            mcs_function_t* f = AS_FUNCTION(fn->consts[u16]);
            for (uint32_t i = 0; i < f->upvalue_count && ok; i++) {
                uint8_t is_local = a[2 + i * 2], idx = a[3 + i * 2];
                ok = is_local ? idx < fn->max_slots : idx < fn->upvalue_count;
            }
            break;
        }
        default:
#if MCS_ENABLE_SUPEROPS
            if (OP_IS_LI(op)) ok = a[0] < fn->max_slots;
            else if (OP_IS_JFLI(op)) ok = a[0] < fn->max_slots && IS_START(pc + len + (uint32_t)((a[2] << 8) | a[3]));
            else if (OP_IS_JFSI(op)) ok = IS_START(pc + len + (uint32_t)((a[1] << 8) | a[2]));
            else if (OP_IS_JFLL(op)) ok = a[0] < fn->max_slots && a[1] < fn->max_slots && IS_START(pc + len + (uint32_t)((a[2] << 8) | a[3]));
            else if (OP_IS_JBLI(op) || OP_IS_JBLL(op)) {
                uint32_t o = (uint32_t)((a[2] << 8) | a[3]);
                ok = a[0] < fn->max_slots && (OP_IS_JBLI(op) || a[1] < fn->max_slots) && o <= pc + len && IS_START(pc + len - o);
            }
#endif
            break;
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
    fn->max_slots = (uint16_t)(l->v3 ? rn(l) : r16(l));
    if (r8(l)) {
        fn->param_types = MCS_ALLOC(vm, uint8_t, fn->arity ? fn->arity : 1);
        for (int i = 0; i < fn->arity; i++) fn->param_types[i] = r8(l);
    }
    uint32_t nk = rn(l);
    if (l->bad || nk > 65536) { l->bad = true; return NULL; }
    if (nk && need(l, nk)) {   /* exact size: images never add constants later */
        fn->consts = MCS_ALLOC(vm, mcs_value_t, nk);
        fn->const_cap = nk;
    }
    for (uint32_t i = 0; i < nk && !l->bad; i++) {
        mcs_value_t v = mcs_null();
        switch (r8(l)) {
        case K_NULL: break;
        case K_FALSE: v = mcs_bool(false); break;
        case K_TRUE: v = mcs_bool(true); break;
        case K_INT: v = mcs_int((mcs_int_t)(l->v3 ? rz(l) : (int64_t)r64(l))); break;
        case K_CHAR: v = mcs_char(rn(l)); break;
        case K_FLOAT32:
        case K_FLOAT: {
            uint8_t tag = l->p[-1];
            uint64_t u = tag == K_FLOAT ? r64(l) : r32(l);
#if MCS_ENABLE_FLOAT
            double d;
            if (tag == K_FLOAT) memcpy(&d, &u, 8);
            else { uint32_t u32 = (uint32_t)u; float f; memcpy(&f, &u32, 4); d = f; }
            v = mcs_float((mcs_float_t)d);
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
    uint32_t clen = rn(l);
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
#if !MCS_ENABLE_SUPEROPS
        if (op >= OP_FIRST_SUPEROP && op < OP__COUNT) {
            snprintf(vm->error, sizeof vm->error, "image uses superinstructions; rebuild with MCS_ENABLE_SUPEROPS=1 or compile with -O0");
            l->bad = true; return NULL;
        }
#endif
        uint32_t len = mcs_insn_len(fn, pc);
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
            if (op == OP_FIELD) {   /* second operand: default-value constant or 0xFFFF */
                uint16_t d = (uint16_t)((fn->code[pc + 3] << 8) | fn->code[pc + 4]);
                if (d != 0xFFFF && d >= fn->const_count) { l->bad = true; return NULL; }
            }
        } else if (op >= OP_FIRST_SUPEROP) {
#if MCS_ENABLE_SUPEROPS
            /* constant operand position (0 = none) of the fused forms */
            uint32_t ko = 0;
            switch (op) {
            case OP_BIN_LK: case OP_BIN_LKS: case OP_JF_LK: case OP_JB_LK: ko = 3; break;
            case OP_BIN_SK: case OP_JF_SK: case OP_GET_FIELD_L: case OP_SETF_L: ko = 2; break;
            default: break;
            }
            if (ko) {
                uint16_t k = (uint16_t)((fn->code[pc + ko] << 8) | fn->code[pc + ko + 1]);
                if (k >= fn->const_count) { l->bad = true; return NULL; }
                if ((op == OP_GET_FIELD_L || op == OP_SETF_L) && !IS_STRING(fn->consts[k])) { l->bad = true; return NULL; }
            }
#endif
        }
        pc += len;
    }
    if (!validate_code(vm, fn)) { l->bad = true; return NULL; }
    uint32_t nl = rn(l);
    if (nl && need(l, l->v3 ? (size_t)nl * 2 : (size_t)nl * 8)) {
#if MCS_ENABLE_LINES
        fn->lines = MCS_ALLOC(vm, mcs_line_t, nl);
        fn->line_cap = fn->line_count = nl;
#endif
        uint32_t pc = 0, line = 0;
        for (uint32_t i = 0; i < nl && !l->bad; i++) {
            if (l->v3) { pc += (uint32_t)rv(l); line = (uint32_t)((int64_t)line + rz(l)); }
            else { pc = r32(l); line = r32(l); }
#if MCS_ENABLE_LINES
            fn->lines[i].pc = pc; fn->lines[i].line = line;
#endif
        }
#if !MCS_ENABLE_LINES
        MCS_UNUSED(pc); MCS_UNUSED(line);
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
    if (img[4] < 1 || img[4] > IMG_VERSION) {
        snprintf(vm->error, sizeof vm->error, "bytecode version %u not supported (expected %u)", img[4], IMG_VERSION);
        return NULL;
    }
    uint8_t flags = img[5];
#if !MCS_INT64
    if (flags & 1) { /* 64-bit image on 32-bit VM: allowed, constants are truncated */ }
#endif
    MCS_UNUSED(flags);
    loader_t l; memset(&l, 0, sizeof l);
    l.vm = vm; l.p = img + 8; l.end = img + len; l.xip = xip; l.v3 = img[4] >= 3;
    vm->gc_pause++;
    l.gcount = rn(&l);
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
    if (l.strs) MCS_FREE(vm, mcs_string_t*, l.strs, l.scap);
    vm->gc_pause--;
    if (!fn || l.bad) {
        if (!vm->error[0]) snprintf(vm->error, sizeof vm->error, "corrupt bytecode image");
        return NULL;
    }
    return fn;
}
mcs_function_t* mcs_load_image(mcs_vm_t* vm, const uint8_t* img, size_t len) { return mcs_load_image_ex(vm, img, len, false); }
#endif
