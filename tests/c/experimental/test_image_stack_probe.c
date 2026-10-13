/* Experimental host-only dense-PC stack-flow probe. NOT a production verifier:
 * no operand types, captured-slot lifetime analysis, or embedded memory budget.
 * Never executes an image. Inputs must first pass the actual image loader. */
#include "mcs_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
mcs_function_t *mcs_load_image(mcs_vm_t *, const uint8_t *, size_t);
typedef struct {
    uint32_t height, handler;
    unsigned pending;
} state_t;
typedef struct {
    uint32_t height, parent;
} context_t;
static unsigned functions, rejected;
static uint32_t u16(const uint8_t *p) {
    return ((uint32_t)p[0] << 8) | p[1];
}
static bool merge(state_t *s, uint32_t length, uint32_t pc, uint32_t height, uint32_t handler) {
    if (pc >= length || !height)
        return false;
    if (s[pc].height)
        return s[pc].height == height && s[pc].handler == handler;
    s[pc].height = height;
    s[pc].handler = handler;
    s[pc].pending = 1;
    return true;
}
static bool local(uint32_t index, uint32_t height) {
    return index < height;
}
static bool verify(mcs_function_t *fn) {
    functions++;
    for (uint32_t k = 0; k < fn->const_count; k++)
        if (IS_KIND(fn->consts[k], MCS_O_FUNCTION) && !verify(AS_FUNCTION(fn->consts[k])))
            return false;
    uint32_t n = fn->code_len;
    state_t *states = calloc(n, sizeof(*states));
    context_t *contexts = calloc(n, sizeof(*contexts));
    if (!states || !contexts) {
        free(states);
        free(contexts);
        return false;
    }
    bool ok = merge(states, n, 0, (uint32_t)fn->arity + 1, 0);
    uint32_t failed_pc = 0;
    while (ok) {
        uint32_t pc = 0;
        while (pc < n && states[pc].pending != 1)
            pc++;
        if (pc == n)
            break;
        states[pc].pending = 2;
        failed_pc = pc;
        uint32_t h = states[pc].height, handler = states[pc].handler, len = mcs_insn_len(fn, pc),
                 next = pc + len;
        const uint8_t *a = fn->code + pc + 1;
        uint8_t op = fn->code[pc];
        uint32_t need = 0, pop = 0, push = 0, target = 0;
        int branch = 0;
        bool terminal = false;
        switch (op) {
        case OP_CONST:
        case OP_NULL:
        case OP_TRUE:
        case OP_FALSE:
        case OP_INT8:
        case OP_GET_GLOBAL:
        case OP_GET_UPVAL:
        case OP_CLASS:
            push = 1;
            break;
        case OP_POP:
        case OP_CLOSE_UPVAL:
            need = pop = 1;
            break;
        case OP_DUP:
            need = 1;
            push = 1;
            break;
        case OP_DUP2:
            need = 2;
            push = 2;
            break;
        case OP_SWAP:
            need = 2;
            break;
        case OP_ROT:
            need = (uint32_t)a[0] + 1;
            break;
        case OP_GET_LOCAL:
            ok = local(a[0], h);
            push = 1;
            break;
        case OP_SET_LOCAL:
            ok = local(a[0], h);
            need = 1;
            break;
        case OP_INC_LOCAL:
            ok = local(a[0], h);
            break;
        case OP_SET_LOCAL_POP:
            ok = h > 1 && local(a[0], h - 1);
            need = pop = 1;
            break;
        case OP_SET_GLOBAL:
        case OP_SET_UPVAL:
            need = 1;
            break;
        case OP_SET_GLOBAL_POP:
            need = pop = 1;
            break;
        case OP_GET_FIELD:
        case OP_NEG:
        case OP_BNOT:
        case OP_NOT:
        case OP_NEW_ARRAY:
        case OP_CONV:
        case OP_TOSTR:
        case OP_IS:
        case OP_AS:
        case OP_CAST:
            need = 1;
            break;
        case OP_SET_FIELD:
        case OP_GET_INDEX:
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:
        case OP_BAND:
        case OP_BOR:
        case OP_BXOR:
        case OP_SHL:
        case OP_SHR:
        case OP_USHR:
        case OP_EQ:
        case OP_NE:
        case OP_LT:
        case OP_LE:
        case OP_GT:
        case OP_GE:
        case OP_TOSTR_FMT:
        case OP_INHERIT:
            need = 2;
            pop = 1;
            break;
        case OP_SET_INDEX:
            need = 3;
            pop = 2;
            break;
        case OP_IMPLEMENTS:
        case OP_FIELD:
            need = 1;
            break;
        case OP_METHOD:
        case OP_STATIC:
        case OP_GETTER:
        case OP_SETTER:
            need = 2;
            pop = 1;
            break;
        case OP_ARRAY:
            need = pop = u16(a);
            push = 1;
            break;
        case OP_CONCAT:
            need = pop = a[0];
            push = 1;
            break;
        case OP_CALL:
            need = (uint32_t)a[0] + 1;
            pop = a[0];
            break;
        case OP_INVOKE:
            need = (uint32_t)a[2] + 1;
            pop = a[2];
            break;
        case OP_SUPER_INVOKE:
            need = (uint32_t)a[2] + 2;
            pop = (uint32_t)a[2] + 1;
            break;
        case OP_CLOSURE: {
            push = 1;
            mcs_function_t *child = AS_FUNCTION(fn->consts[u16(a)]);
            for (uint32_t i = 0; i < child->upvalue_count && ok; i++)
                if (a[2 + i * 2])
                    ok = local(a[3 + i * 2], h + 1);
            break;
        }
        case OP_RETURN:
            need = 1;
            terminal = true;
            break;
        case OP_RETURN_NULL:
            terminal = true;
            break;
        case OP_THROW:
            need = 1;
            terminal = true;
            break;
        case OP_JUMP:
            branch = 2;
            target = next + u16(a);
            break;
        case OP_LOOP:
            branch = 2;
            target = next - u16(a);
            break;
        case OP_JUMP_IF_FALSE:
        case OP_JUMP_IF_TRUE:
            need = pop = 1;
            branch = 1;
            target = next + u16(a);
            break;
        case OP_JUMP_IF_FALSE_KEEP:
        case OP_JUMP_IF_TRUE_KEEP:
        case OP_JUMP_IF_NULL_KEEP:
        case OP_JUMP_IF_NOT_NULL_KEEP:
            need = 1;
            branch = 1;
            target = next + u16(a);
            break;
        case OP_ARGC_JUMP:
            branch = 1;
            target = next + u16(a + 1);
            break;
        case OP_JF_EQ:
        case OP_JF_NE:
        case OP_JF_LT:
        case OP_JF_LE:
        case OP_JF_GT:
        case OP_JF_GE:
            need = pop = 2;
            branch = 1;
            target = next + u16(a);
            break;
        case OP_FOR_ITER: {
            uint32_t s = a[0] & 127, extra = a[0] < 128 ? 1 : 3;
            ok = local(s + extra, h);
            branch = 3;
            target = next + u16(a + 1);
            push = 1;
            break;
        }
        case OP_TRY:
            contexts[pc].height = h;
            contexts[pc].parent = handler;
            ok = h < UINT32_MAX && merge(states, n, next + u16(a), h + 1, handler);
            handler = pc + 1;
            break;
        case OP_END_TRY:
            if (!handler)
                ok = false;
            else
                handler = contexts[handler - 1].parent;
            break;
#if MCS_ENABLE_SUPEROPS
        case OP_BIN_LL:
            ok = local(a[1], h) && local(a[2], h);
            push = 1;
            break;
        case OP_BIN_LK:
            ok = local(a[1], h);
            push = 1;
            break;
        case OP_BIN_SL:
            ok = local(a[1], h);
            need = 1;
            break;
        case OP_BIN_SK:
            need = 1;
            break;
        case OP_BIN_LLS:
            ok = local(a[1], h) && local(a[2], h) && local(a[3], h);
            break;
        case OP_BIN_LKS:
        case OP_BIN_LIS:
            ok = local(a[1], h) && local(a[2], h);
            break;
        case OP_JF_LK:
        case OP_JB_LK:
            ok = local(a[1], h);
            branch = 1;
            target = op == OP_JF_LK ? next + u16(a + 4) : next - u16(a + 4);
            break;
        case OP_JF_SL:
            ok = local(a[1], h);
            need = pop = 1;
            branch = 1;
            target = next + u16(a + 2);
            break;
        case OP_JF_SK:
            need = pop = 1;
            branch = 1;
            target = next + u16(a + 3);
            break;
        case OP_GET_FIELD_L:
            ok = local(a[0], h);
            push = 1;
            break;
        case OP_RETURN_LOCAL:
            ok = local(a[0], h);
            terminal = true;
            break;
        case OP_ACC:
            ok = h > 1 && local(a[1], h - 1);
            need = pop = 1;
            break;
        case OP_ACC_ADD:
        case OP_ACC_SUB:
            ok = h > 1 && local(a[0], h - 1);
            need = pop = 1;
            break;
        case OP_SETF_L:
            ok = local(a[0], h);
            need = pop = 1;
            break;
        case OP_GET_INDEX_LL:
            ok = local(a[0], h) && local(a[1], h);
            push = 1;
            break;
        case OP_SET_INDEX_LL:
            ok = local(a[0], h) && local(a[1], h);
            need = pop = 1;
            break;
#endif
        default:
#if MCS_ENABLE_SUPEROPS
            if (OP_IS_LI(op)) {
                ok = local(a[0], h);
                push = 1;
            } else if (OP_IS_SI(op)) {
                need = 1;
            } else if (OP_IS_JFLI(op) || OP_IS_JBLI(op)) {
                ok = local(a[0], h);
                branch = 1;
                target = OP_IS_JFLI(op) ? next + u16(a + 2) : next - u16(a + 2);
            } else if (OP_IS_JFSI(op)) {
                need = pop = 1;
                branch = 1;
                target = next + u16(a + 1);
            } else if (OP_IS_JFLL(op) || OP_IS_JBLL(op)) {
                ok = local(a[0], h) && local(a[1], h);
                branch = 1;
                target = OP_IS_JFLL(op) ? next + u16(a + 2) : next - u16(a + 2);
            } else
                ok = false;
#else
            ok = false;
#endif
        }
        if (!ok || h < need || (uint64_t)h - pop + push > UINT32_MAX) {
            ok = false;
            break;
        }
        if (terminal)
            continue;
        uint32_t after = h - pop + push;
        if (!after || (handler && after < contexts[handler - 1].height)) {
            ok = false;
            break;
        }
        if (branch)
            ok = merge(states, n, target, branch == 3 ? h : after, handler);
        if (ok && branch != 2)
            ok = merge(states, n, next, after, handler);
    }
    if (!ok) {
        fprintf(stderr, "stack-flow rejection fn=%s pc=%u\n", fn->name ? fn->name->chars : "?",
                failed_pc);
        rejected++;
    }
    free(states);
    free(contexts);
    return ok;
}
static void quiet(void *ud, const char *p, size_t n) {
    (void)ud;
    (void)p;
    (void)n;
}
static bool probe_case(const char *name, const uint8_t *code, size_t length, uint8_t slots,
                       bool wanted) {
    uint8_t image[128] = {77, 67, 83, 66, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, slots, 0, 0};
    if (length >= 128 || length + 20 > sizeof(image))
        return false;
    image[18] = (uint8_t)length;
    memcpy(image + 19, code, length);
    image[19 + length] = 0;
    mcs_config_t cfg;
    mcs_config_default(&cfg);
    cfg.write_fn = quiet;
    cfg.error_fn = quiet;
    mcs_vm_t *vm = mcs_new(&cfg);
    if (!vm)
        return false;
    mcs_function_t *fn = mcs_load_image(vm, image, length + 20);
    bool ok = fn && verify(fn);
    bool result = fn && ok == wanted;
    printf("%s %s: flow=%s; LOAD ONLY\n", result ? "PASS" : "FAIL", name,
           ok ? "accepted" : "rejected");
    mcs_free(vm);
    return result;
}
static int self_test(void) {
    unsigned passed = 0, total = 0;
#define CASE_TEST(name, slots, wanted, ...)                                                        \
    do {                                                                                           \
        const uint8_t code[] = {__VA_ARGS__};                                                      \
        total++;                                                                                   \
        passed += probe_case(name, code, sizeof(code), slots, wanted);                             \
    } while (0)
    CASE_TEST("balanced return", 1, true, OP_NULL, OP_RETURN);
    CASE_TEST("reachable fallthrough", 1, false, OP_NULL);
    CASE_TEST("pop below callee", 1, false, OP_POP, OP_RETURN_NULL);
    CASE_TEST("growth on loop backedge", 1, false, OP_NULL, OP_LOOP, 0, 4);
    CASE_TEST("mismatched branch depths", 1, false, OP_TRUE, OP_JUMP_IF_FALSE, 0, 1, OP_NULL,
              OP_RETURN_NULL);
    CASE_TEST("local beyond initialized height", 2, false, OP_GET_LOCAL, 1, OP_RETURN_NULL);
    CASE_TEST("initialized local", 2, true, OP_NULL, OP_GET_LOCAL, 1, OP_POP, OP_RETURN_NULL);
    CASE_TEST("END_TRY without handler", 1, false, OP_END_TRY, OP_RETURN_NULL);
    CASE_TEST("TRY normal and catch", 1, true, OP_TRY, 0, 2, OP_END_TRY, OP_RETURN_NULL,
              OP_RETURN_NULL);
    CASE_TEST("pop below saved handler height", 1, false, OP_TRY, 0, 2, OP_POP, OP_END_TRY,
              OP_RETURN_NULL);
    /* Deliberate known gap: correct stack heights do NOT establish class types.
     * The loader and probe accept it; executing this artifact remains unsafe. */
    CASE_TEST("KNOWN TYPE GAP, never execute", 1, true, OP_TRUE, OP_TRUE, OP_INHERIT,
              OP_RETURN_NULL);
#undef CASE_TEST
    printf("%u/%u experimental flow assertions; NO image executed; type/lifetime/embedded-memory "
           "verification absent\n",
           passed, total);
    return passed == total ? 0 : 1;
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--self-test"))
        return self_test();
    if (argc < 2)
        return 64;
    unsigned accepted = 0;
    for (int k = 1; k < argc; k++) {
        FILE *f = fopen(argv[k], "rb");
        if (!f)
            return 2;
        if (fseek(f, 0, SEEK_END)) {
            fclose(f);
            return 2;
        }
        long n = ftell(f);
        if (n <= 0 || n > 1000000) {
            fclose(f);
            return 2;
        }
        rewind(f);
        uint8_t *bytes = malloc((size_t)n);
        if (!bytes || fread(bytes, 1, (size_t)n, f) != (size_t)n || ferror(f)) {
            free(bytes);
            fclose(f);
            return 2;
        }
        if (fclose(f)) {
            free(bytes);
            return 2;
        }
        mcs_config_t cfg;
        mcs_config_default(&cfg);
        cfg.write_fn = quiet;
        cfg.error_fn = quiet;
        mcs_vm_t *vm = mcs_new(&cfg);
        if (!vm) {
            free(bytes);
            return 2;
        }
        mcs_function_t *fn = mcs_load_image(vm, bytes, (size_t)n);
        if (!fn) {
            fprintf(stderr, "actual loader rejection: %s\n", argv[k]);
            mcs_free(vm);
            free(bytes);
            return 2;
        }
        bool ok = verify(fn);
        if (!ok)
            fprintf(stderr, "image: %s\n", argv[k]);
        accepted += ok;
        mcs_free(vm);
        free(bytes);
    }
    printf("EXPERIMENTAL LOAD-ONLY probe: %u/%d images accepted, %u functions visited, %u flow "
           "rejections; NO execution/type/lifetime/security proof\n",
           accepted, argc - 1, functions, rejected);
    return accepted == (unsigned)(argc - 1) ? 0 : 1;
}
