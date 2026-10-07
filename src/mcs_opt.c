/* MicroCS - bytecode optimizer (peephole + loop rotation).
 *
 * Runs on a finished function, after the single-pass compiler. It is what
 * makes precompiled images faster than compiling on the device: the host
 * tool (`mcs -c`, mcs_compile_image) always runs it, on-device source
 * compilation only when MCS_OPTIMIZE_SOURCE=1 (it costs compile time/RAM).
 *
 *  - fuses stack sequences into superinstructions (layout in mcs_internal.h;
 *    I = INT8 immediate, K = constant, L = local):
 *      GET_LOCAL a; INT8 i; <binop>                 -> LI_<op> a i
 *      GET_LOCAL a; GET_LOCAL b|CONST k; <binop>    -> BIN_LL / BIN_LK
 *        ... followed by SET_LOCAL_POP d            -> BIN_LLS / BIN_LKS / BIN_LIS
 *      INT8 i; <binop> -> SI_<op>    GET_LOCAL b|CONST k; <binop> -> BIN_SL / BIN_SK
 *      GET_LOCAL a; INT8 i|GET_LOCAL b|CONST k; JF_<c> -> JFLI_<c> / JFLL_<c> / JF_LK
 *      INT8 i|GET_LOCAL b|CONST k; JF_<c>               -> JFSI_<c> / JF_SL / JF_SK
 *      GET_LOCAL d; <one push>; <binop>; SET_LOCAL_POP d -> <one push>; ACC_<op> d
 *      GET_LOCAL s; <one push>; SET_FIELD k; POP        -> <one push>; SETF_L s k
 *      GET_LOCAL s; GET_FIELD k -> GET_FIELD_L      GET_LOCAL s; RETURN -> RETURN_LOCAL
 *  - threads JUMPs that land on a return,
 *  - rotates loops whose head is a fused compare-and-branch: the back edge
 *    re-tests the condition (JBLI/JBLL/JB_LK "jump back if true") instead of
 *    LOOP + test.
 * Nothing is fused across a branch target, so control flow is unchanged.
 * The VM executes the new opcodes only when built with MCS_ENABLE_SUPEROPS. */
#include "mcs_internal.h"

#if MCS_ENABLE_OPTIMIZER

typedef struct {
    uint32_t pc;           /* original pc (first fused instruction) */
    uint32_t npc;          /* new pc */
    int32_t tgt;           /* branch target: original pc while decoding, then an index; -1 = none */
    const uint8_t* raw;    /* unchanged instruction bytes (NULL when rebuilt) */
    uint16_t len;          /* encoded length incl. opcode */
    uint8_t lead;          /* some branch lands here */
    uint8_t b[8];          /* rebuilt instruction (len <= 8) */
} oi_t;

#define BY(x) ((x)->raw ? (x)->raw : (x)->b)
#define OPC(x) (BY(x)[0])

static int binop_index(uint8_t op) {
    switch (op) {
    case OP_ADD: return BX_ADD; case OP_SUB: return BX_SUB; case OP_MUL: return BX_MUL;
    case OP_DIV: return BX_DIV; case OP_MOD: return BX_MOD; case OP_BAND: return BX_BAND;
    case OP_BOR: return BX_BOR; case OP_BXOR: return BX_BXOR; case OP_SHL: return BX_SHL;
    case OP_SHR: return BX_SHR; case OP_USHR: return BX_USHR;
    default: return -1;
    }
}
static int jcmp_index(uint8_t op) {
    switch (op) {
    case OP_JF_EQ: return JX_EQ; case OP_JF_NE: return JX_NE; case OP_JF_LT: return JX_LT;
    case OP_JF_LE: return JX_LE; case OP_JF_GT: return JX_GT; case OP_JF_GE: return JX_GE;
    default: return -1;
    }
}

static uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* branch target of the original instruction at code[pc], or -1 */
static int32_t branch_target(const mcs_function_t* fn, uint32_t pc, uint32_t len) {
    const uint8_t* a = fn->code + pc + 1;
    switch (fn->code[pc]) {
    case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_JUMP_IF_FALSE_KEEP:
    case OP_JUMP_IF_TRUE_KEEP: case OP_JUMP_IF_NULL_KEEP: case OP_JUMP_IF_NOT_NULL_KEEP: case OP_TRY:
    case OP_JF_EQ: case OP_JF_NE: case OP_JF_LT: case OP_JF_LE: case OP_JF_GT: case OP_JF_GE:
        return (int32_t)(pc + len + rd16(a));
    case OP_LOOP: return (int32_t)(pc + len) - (int32_t)rd16(a);
    case OP_ARGC_JUMP: case OP_FOR_ITER: return (int32_t)(pc + len + rd16(a + 1));
    default: return -1;
    }
}

static bool is_back(uint8_t op) { return op == OP_LOOP || op == OP_JB_LK || OP_IS_JBLI(op) || OP_IS_JBLL(op); }
/* position of the u16 branch offset inside an instruction */
static int off_pos(uint8_t op) {
    switch (op) {
    case OP_ARGC_JUMP: case OP_FOR_ITER: return 2;
    case OP_JF_SK: return 4;
    case OP_JF_LK: case OP_JB_LK: return 5;
    case OP_JF_SL: return 3;
    default:
        if (OP_IS_JFLI(op) || OP_IS_JBLI(op) || OP_IS_JFLL(op) || OP_IS_JBLL(op)) return 3;
        if (OP_IS_JFSI(op)) return 2;
        return 1;
    }
}

static void put(oi_t* o, int32_t tgt, int len, const uint8_t* b) {
    o->raw = NULL; o->len = (uint16_t)len; o->tgt = tgt;
    memcpy(o->b, b, (size_t)len);
}

/* INT8 v usable as the immediate of operation bx (division needs v > 0) */
static bool imm_ok(int bx, int8_t v) { return (bx != BX_DIV && bx != BX_MOD) || v > 0; }
/* pure instruction pushing exactly one value that cannot write a local */
static bool one_push(uint8_t op) {
    return op == OP_GET_LOCAL || op == OP_INT8 || op == OP_CONST || OP_IS_LI(op) || op == OP_BIN_LL || op == OP_BIN_LK;
}

/* one fusion pass over in[0..n); returns the new count (in[] rewritten in place
 * through tmp). captured[s]: local s is captured by a closure of this function */
static uint32_t fuse_pass(mcs_vm_t* vm, mcs_function_t* fn, oi_t* in, uint32_t n, oi_t* o, int32_t* map,
                          const uint8_t* captured, bool* changed) {
    uint32_t m = 0;
    for (uint32_t i = 0; i < n;) {
        oi_t cur = in[i];
        const uint8_t* p0 = BY(&in[i]);
        uint32_t used = 1;
        uint8_t op1 = i + 1 < n ? OPC(&in[i + 1]) : 0, op2 = i + 2 < n ? OPC(&in[i + 2]) : 0;
        uint8_t op3 = i + 3 < n ? OPC(&in[i + 3]) : 0;
        const uint8_t* p1 = i + 1 < n ? BY(&in[i + 1]) : NULL;
        bool f1 = i + 1 < n && !in[i + 1].lead, f2 = f1 && i + 2 < n && !in[i + 2].lead, f3 = f2 && i + 3 < n && !in[i + 3].lead;
        int bi, ji;
        uint8_t e[8];
        /* GET_LOCAL d; <push>; <binop>; SET_LOCAL_POP d -> <push>; ACC d (a local read
         * by GET_LOCAL / INT8 / CONST is fused into BIN_LLS / BIN_LIS / BIN_LKS instead).
         * Reading slots[d] after <push> is only equivalent when nothing can write d
         * meanwhile: <push> is pure and d is not captured by a closure. */
        if (p0[0] == OP_GET_LOCAL && f3 && op3 == OP_SET_LOCAL_POP && BY(&in[i + 3])[1] == p0[1] && !captured[p0[1]] &&
            one_push(op1) && op1 != OP_GET_LOCAL && op1 != OP_INT8 && op1 != OP_CONST && (bi = binop_index(op2)) >= 0) {
            map[i] = map[i + 1] = (int32_t)m;
            cur = in[i + 1]; cur.lead = in[i].lead; cur.pc = in[i].pc;
            o[m++] = cur;
            if (bi == BX_ADD) { e[0] = OP_ACC_ADD; e[1] = p0[1]; put(&cur, -1, 2, e); }
            else if (bi == BX_SUB) { e[0] = OP_ACC_SUB; e[1] = p0[1]; put(&cur, -1, 2, e); }
            else { e[0] = OP_ACC; e[1] = (uint8_t)bi; e[2] = p0[1]; put(&cur, -1, 3, e); }
            cur.pc = in[i + 2].pc; cur.lead = 0;
            map[i + 2] = map[i + 3] = (int32_t)m;
            o[m++] = cur;
            i += 4; *changed = true;
            continue;
        }
        /* GET_LOCAL a; GET_LOCAL i; <push>; SET_INDEX; POP -> <push>; SET_INDEX_LL a i */
        if (p0[0] == OP_GET_LOCAL && op1 == OP_GET_LOCAL && f3 && i + 4 < n && !in[i + 4].lead && op3 == OP_SET_INDEX &&
            OPC(&in[i + 4]) == OP_POP && !captured[p0[1]] && !captured[p1[1]] && one_push(op2)) {
            map[i] = map[i + 1] = map[i + 2] = (int32_t)m;
            cur = in[i + 2]; cur.lead = in[i].lead; cur.pc = in[i].pc;
            o[m++] = cur;
            e[0] = OP_SET_INDEX_LL; e[1] = p0[1]; e[2] = p1[1]; put(&cur, -1, 3, e);
            cur.pc = in[i + 3].pc; cur.lead = 0;
            map[i + 3] = map[i + 4] = (int32_t)m;
            o[m++] = cur;
            i += 5; *changed = true;
            continue;
        }
        if (p0[0] == OP_GET_LOCAL && op1 == OP_GET_LOCAL && f2 && op2 == OP_GET_INDEX) {
            e[0] = OP_GET_INDEX_LL; e[1] = p0[1]; e[2] = p1[1]; put(&cur, -1, 3, e);
            map[i] = map[i + 1] = map[i + 2] = (int32_t)m;
            o[m++] = cur;
            i += 3; *changed = true;
            continue;
        }
#if MCS_ENABLE_FLOAT
        /* INT8 v; CONV float -> CONST v.0 */
        if (p0[0] == OP_INT8 && f1 && op1 == OP_CONV && p1[1] == CV_FLOAT) {
            uint32_t kk = mcs_fn_add_const(vm, fn, mcs_float((mcs_float_t)(int8_t)p0[1]));
            if (kk <= 0xFFFF) {
                e[0] = OP_CONST; e[1] = (uint8_t)(kk >> 8); e[2] = (uint8_t)kk; put(&cur, -1, 3, e);
                map[i] = map[i + 1] = (int32_t)m;
                o[m++] = cur;
                i += 2; *changed = true;
                continue;
            }
        }
#endif
        /* GET_LOCAL s; <push>; SET_FIELD k; POP -> <push>; SETF_L s k (same reordering rule) */
        if (p0[0] == OP_GET_LOCAL && f3 && op2 == OP_SET_FIELD && op3 == OP_POP && !captured[p0[1]] && one_push(op1)) {
            const uint8_t* p2 = BY(&in[i + 2]);
            map[i] = map[i + 1] = (int32_t)m;
            cur = in[i + 1]; cur.lead = in[i].lead; cur.pc = in[i].pc;
            o[m++] = cur;
            e[0] = OP_SETF_L; e[1] = p0[1]; e[2] = p2[1]; e[3] = p2[2]; put(&cur, -1, 4, e);
            cur.pc = in[i + 2].pc; cur.lead = 0;
            map[i + 2] = map[i + 3] = (int32_t)m;
            o[m++] = cur;
            i += 4; *changed = true;
            continue;
        }
        if (p0[0] == OP_GET_LOCAL && f2 && (op1 == OP_GET_LOCAL || op1 == OP_CONST || op1 == OP_INT8) &&
            ((bi = binop_index(op2)) >= 0 || (ji = jcmp_index(op2)) >= 0)) {
            bi = binop_index(op2); ji = jcmp_index(op2);
            uint8_t a = p0[1];
            bool imm = op1 == OP_INT8 && (bi < 0 || imm_ok(bi, (int8_t)p1[1]));
            uint16_t k = 0; bool kc = false;
            if (!imm && op1 != OP_GET_LOCAL) {
                if (op1 == OP_CONST) k = rd16(p1 + 1);
                else { uint32_t kk = mcs_fn_add_const(vm, fn, mcs_int((int8_t)p1[1])); if (kk > 0xFFFF) goto plain; k = (uint16_t)kk; }
                kc = true;
            }
            if (bi >= 0) {
                bool st = f3 && op3 == OP_SET_LOCAL_POP;
                uint8_t d = st ? BY(&in[i + 3])[1] : 0;
                if (imm && st) { e[0] = OP_BIN_LIS; e[1] = (uint8_t)bi; e[2] = a; e[3] = p1[1]; e[4] = d; put(&cur, -1, 5, e); }
                else if (imm) { e[0] = (uint8_t)(OP_LI_ADD + bi); e[1] = a; e[2] = p1[1]; put(&cur, -1, 3, e); }
                else if (kc && st) { e[0] = OP_BIN_LKS; e[1] = (uint8_t)bi; e[2] = a; e[3] = (uint8_t)(k >> 8); e[4] = (uint8_t)k; e[5] = d; put(&cur, -1, 6, e); }
                else if (kc) { e[0] = OP_BIN_LK; e[1] = (uint8_t)bi; e[2] = a; e[3] = (uint8_t)(k >> 8); e[4] = (uint8_t)k; put(&cur, -1, 5, e); }
                else if (st) { e[0] = OP_BIN_LLS; e[1] = (uint8_t)bi; e[2] = a; e[3] = p1[1]; e[4] = d; put(&cur, -1, 5, e); }
                else { e[0] = OP_BIN_LL; e[1] = (uint8_t)bi; e[2] = a; e[3] = p1[1]; put(&cur, -1, 4, e); }
                used = st ? 4 : 3;
            } else {
                int32_t t = in[i + 2].tgt;
                if (imm) { e[0] = (uint8_t)(OP_JFLI_EQ + ji); e[1] = a; e[2] = p1[1]; e[3] = e[4] = 0; put(&cur, t, 5, e); }
                else if (kc) { e[0] = OP_JF_LK; e[1] = (uint8_t)ji; e[2] = a; e[3] = (uint8_t)(k >> 8); e[4] = (uint8_t)k; e[5] = e[6] = 0; put(&cur, t, 7, e); }
                else { e[0] = (uint8_t)(OP_JFLL_EQ + ji); e[1] = a; e[2] = p1[1]; e[3] = e[4] = 0; put(&cur, t, 5, e); }
                used = 3;
            }
        } else if ((p0[0] == OP_GET_LOCAL || p0[0] == OP_CONST || p0[0] == OP_INT8) && f1 &&
                   ((bi = binop_index(op1)) >= 0 || (ji = jcmp_index(op1)) >= 0)) {
            bi = binop_index(op1); ji = jcmp_index(op1);
            bool imm = p0[0] == OP_INT8 && (bi < 0 || imm_ok(bi, (int8_t)p0[1]));
            uint16_t k = 0; bool kc = false;
            if (!imm && p0[0] != OP_GET_LOCAL) {
                if (p0[0] == OP_CONST) k = rd16(p0 + 1);
                else { uint32_t kk = mcs_fn_add_const(vm, fn, mcs_int((int8_t)p0[1])); if (kk > 0xFFFF) goto plain; k = (uint16_t)kk; }
                kc = true;
            }
            if (bi >= 0) {
                if (imm) { e[0] = (uint8_t)(OP_SI_ADD + bi); e[1] = p0[1]; put(&cur, -1, 2, e); }
                else if (kc) { e[0] = OP_BIN_SK; e[1] = (uint8_t)bi; e[2] = (uint8_t)(k >> 8); e[3] = (uint8_t)k; put(&cur, -1, 4, e); }
                else { e[0] = OP_BIN_SL; e[1] = (uint8_t)bi; e[2] = p0[1]; put(&cur, -1, 3, e); }
            } else {
                int32_t t = in[i + 1].tgt;
                if (imm) { e[0] = (uint8_t)(OP_JFSI_EQ + ji); e[1] = p0[1]; e[2] = e[3] = 0; put(&cur, t, 4, e); }
                else if (kc) { e[0] = OP_JF_SK; e[1] = (uint8_t)ji; e[2] = (uint8_t)(k >> 8); e[3] = (uint8_t)k; e[4] = e[5] = 0; put(&cur, t, 6, e); }
                else { e[0] = OP_JF_SL; e[1] = (uint8_t)ji; e[2] = p0[1]; e[3] = e[4] = 0; put(&cur, t, 5, e); }
            }
            used = 2;
        } else if (p0[0] == OP_GET_LOCAL && f1 && op1 == OP_GET_FIELD) {
            e[0] = OP_GET_FIELD_L; e[1] = p0[1]; e[2] = p1[1]; e[3] = p1[2];
            put(&cur, -1, 4, e); used = 2;
        } else if (p0[0] == OP_GET_LOCAL && f1 && op1 == OP_RETURN) {
            e[0] = OP_RETURN_LOCAL; e[1] = p0[1];
            put(&cur, -1, 2, e); used = 2;
        }
    plain:
        if (used > 1) *changed = true;
        for (uint32_t k = 0; k < used; k++) map[i + k] = (int32_t)m;
        o[m++] = cur;
        i += used;
    }
    map[n] = (int32_t)m;
    for (uint32_t k = 0; k < m; k++) if (o[k].tgt >= 0) o[k].tgt = map[o[k].tgt];
    memcpy(in, o, sizeof(oi_t) * m);
    return m;
}

static void mark_leaders(oi_t* in, uint32_t n) {
    for (uint32_t k = 0; k < n; k++) in[k].lead = 0;
    for (uint32_t k = 0; k < n; k++) if (in[k].tgt >= 0 && (uint32_t)in[k].tgt < n) in[in[k].tgt].lead = 1;
}

static void optimize_one(mcs_vm_t* vm, mcs_function_t* fn) {
    uint32_t clen = fn->code_len;
    if (!clen || (fn->flags & FN_XIP)) return;
    /* decode */
    uint32_t n = 0;
    for (uint32_t pc = 0; pc < clen;) {
        uint32_t len = mcs_insn_len(fn, pc);
        if (!len || pc + len > clen) return;   /* malformed: leave alone */
        pc += len; n++;
    }
    oi_t* in = MCS_ALLOC(vm, oi_t, n);
    oi_t* o = MCS_ALLOC(vm, oi_t, n);
    int32_t* at = MCS_ALLOC(vm, int32_t, clen + 1);     /* original pc -> instruction index */
    int32_t* map = MCS_ALLOC(vm, int32_t, n + 1);
    int32_t* pcmap = MCS_ALLOC(vm, int32_t, n + 1);     /* original index -> final index */
    uint8_t captured[256];
    memset(captured, 0, sizeof captured);
    for (uint32_t i = 0; i <= clen; i++) at[i] = -1;
    uint32_t i = 0;
    for (uint32_t pc = 0; pc < clen; i++) {
        uint32_t len = mcs_insn_len(fn, pc);
        in[i].pc = pc; in[i].raw = fn->code + pc; in[i].len = (uint16_t)len;
        in[i].tgt = branch_target(fn, pc, len);
        if (fn->code[pc] == OP_CLOSURE) {
            uint16_t k = rd16(fn->code + pc + 1);
            mcs_function_t* f = AS_FUNCTION(fn->consts[k]);
            for (uint32_t u = 0; u < f->upvalue_count; u++)
                if (fn->code[pc + 3 + u * 2]) captured[fn->code[pc + 4 + u * 2]] = 1;
        }
        at[pc] = (int32_t)i;
        pc += len;
    }
    at[clen] = (int32_t)n;   /* "end of function" is a valid fall-through position */
    bool ok = true;
    for (i = 0; i < n; i++) {
        if (in[i].tgt < 0) continue;
        if (in[i].tgt > (int32_t)clen || at[in[i].tgt] < 0) { ok = false; break; }
        in[i].tgt = at[in[i].tgt];   /* now an index (n = end) */
    }
    if (!ok) goto out;
    for (i = 0; i <= n; i++) pcmap[i] = (int32_t)i;

    /* fuse + thread until nothing changes (each pass can build on the previous one);
     * jump threading: JUMP -> RETURN/RETURN_NULL/RETURN_LOCAL becomes the return itself */
    uint32_t m = n;
    for (int pass = 0; pass < 6; pass++) {
        bool changed = false;
        mark_leaders(in, m);
        uint32_t m2 = fuse_pass(vm, fn, in, m, o, map, captured, &changed);
        for (i = 0; i <= n; i++) pcmap[i] = map[pcmap[i]];
        m = m2;
        for (uint32_t k = 0; k < m; k++) {
            if (OPC(&in[k]) != OP_JUMP || in[k].tgt < 0 || (uint32_t)in[k].tgt >= m) continue;
            oi_t* t = &in[in[k].tgt];
            uint8_t top = OPC(t);
            if (top == OP_RETURN || top == OP_RETURN_NULL || top == OP_RETURN_LOCAL) {
                uint32_t pc = in[k].pc;
                uint8_t ld = in[k].lead;
                in[k] = *t; in[k].pc = pc; in[k].tgt = -1; in[k].lead = ld;
                changed = true;
            }
        }
        if (!changed) break;
    }

    /* loop rotation: LOOP -> head, where head is a fused compare-and-branch whose
     * exit is right after the LOOP: re-test at the bottom, jump back into the body */
    for (uint32_t k = 0; k < m; k++) {
        if (OPC(&in[k]) != OP_LOOP || in[k].tgt < 0) continue;
        uint32_t h = (uint32_t)in[k].tgt;
        if (h >= k) continue;
        oi_t* hd = &in[h];
        uint8_t hop = OPC(hd);
        if (hd->raw || hd->tgt != (int32_t)(k + 1)) continue;
        uint8_t bop;
        if (hop == OP_JF_LK) bop = OP_JB_LK;
        else if (OP_IS_JFLI(hop)) bop = (uint8_t)(OP_JBLI_EQ + (hop - OP_JFLI_EQ));
        else if (OP_IS_JFLL(hop)) bop = (uint8_t)(OP_JBLL_EQ + (hop - OP_JFLL_EQ));
        else continue;
        uint32_t pc = in[k].pc;
        uint8_t ld = in[k].lead;
        in[k] = *hd; in[k].pc = pc; in[k].lead = ld;
        in[k].b[0] = bop;
        in[k].tgt = (int32_t)(h + 1);
    }

    /* drop unreachable code (e.g. the implicit return after an explicit one) */
    {
        uint8_t* reach = (uint8_t*)o;   /* o[] is free now: m <= n entries of >= 1 byte */
        memset(reach, 0, m);
        uint32_t* work = (uint32_t*)map;
        uint32_t wn = 0;
        if (m) { reach[0] = 1; work[wn++] = 0; }
        while (wn) {
            uint32_t k = work[--wn];
            uint8_t op = OPC(&in[k]);
            bool falls = !(op == OP_JUMP || op == OP_LOOP || op == OP_RETURN || op == OP_RETURN_NULL ||
                           op == OP_RETURN_LOCAL || op == OP_THROW);
            if (falls && k + 1 < m && !reach[k + 1]) { reach[k + 1] = 1; work[wn++] = k + 1; }
            int32_t t = in[k].tgt;
            if (t >= 0 && (uint32_t)t < m && !reach[t]) { reach[t] = 1; work[wn++] = (uint32_t)t; }
        }
        /* new index of every old one (removed entries map to the next kept one) */
        uint32_t w = 0;
        for (uint32_t k = 0; k < m; k++) { map[k] = (int32_t)w; if (reach[k]) w++; }
        map[m] = (int32_t)w;
        if (w < m) {
            uint32_t q = 0;
            for (uint32_t k = 0; k < m; k++) if (reach[k]) in[q++] = in[k];
            for (uint32_t k = 0; k < w; k++) if (in[k].tgt >= 0) in[k].tgt = map[in[k].tgt];
            for (i = 0; i <= n; i++) pcmap[i] = map[pcmap[i]];
            m = w;
        }
    }

    /* layout */
    {
        uint32_t npc = 0;
        for (uint32_t k = 0; k < m; k++) { in[k].npc = npc; npc += in[k].len; }
        uint32_t nlen = npc;
        bool fits = true;
        uint8_t* code = MCS_ALLOC(vm, uint8_t, nlen ? nlen : 1);
        for (uint32_t k = 0; k < m && fits; k++) {
            uint8_t* d = code + in[k].npc;
            memcpy(d, BY(&in[k]), in[k].len);
            if (in[k].tgt < 0) continue;
            uint32_t tpc = (uint32_t)in[k].tgt < m ? in[(uint32_t)in[k].tgt].npc : nlen;
            uint32_t next = in[k].npc + in[k].len;
            uint8_t op = d[0];
            int32_t off = is_back(op) ? (int32_t)next - (int32_t)tpc : (int32_t)tpc - (int32_t)next;
            if (off < 0 || off > 0xFFFF) { fits = false; break; }
            uint8_t* at16 = d + off_pos(op);
            at16[0] = (uint8_t)(off >> 8); at16[1] = (uint8_t)off;
        }
        if (fits) {
#if MCS_ENABLE_LINES
            /* remap the line table: old pc -> new pc of the instruction holding it */
            uint32_t w = 0;
            for (uint32_t li = 0; li < fn->line_count; li++) {
                uint32_t opc = fn->lines[li].pc;
                uint32_t idx = opc < clen && at[opc] >= 0 ? (uint32_t)pcmap[at[opc]] : m;
                uint32_t np = idx < m ? in[idx].npc : nlen;
                if (w && fn->lines[w - 1].pc == np) { fn->lines[w - 1].line = fn->lines[li].line; continue; }
                if (w && fn->lines[w - 1].line == fn->lines[li].line) continue;
                fn->lines[w].pc = np; fn->lines[w].line = fn->lines[li].line; w++;
            }
            fn->line_count = w;
#endif
            if (fn->code) MCS_FREE(vm, uint8_t, fn->code, fn->code_cap);
            fn->code = code; fn->code_cap = nlen ? nlen : 1;
            fn->code_len = nlen;
        } else {
            MCS_FREE(vm, uint8_t, code, nlen ? nlen : 1);
        }
    }
out:
    MCS_FREE(vm, int32_t, pcmap, n + 1);
    MCS_FREE(vm, int32_t, map, n + 1);
    MCS_FREE(vm, int32_t, at, clen + 1);
    MCS_FREE(vm, oi_t, o, n);
    MCS_FREE(vm, oi_t, in, n);
}

void mcs_optimize(mcs_vm_t* vm, mcs_function_t* fn, int depth) {
    if (!fn || depth > 64) return;
    vm->gc_pause++;
    optimize_one(vm, fn);
    for (uint32_t k = 0; k < fn->const_count; k++)
        if (IS_KIND(fn->consts[k], MCS_O_FUNCTION)) mcs_optimize(vm, AS_FUNCTION(fn->consts[k]), depth + 1);
    vm->gc_pause--;
}
#endif
