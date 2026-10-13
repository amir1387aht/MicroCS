/* Targeted image metadata/varint rejection, not full stack/type verification.
 * Baseline mode LOADS ONLY malformed fixtures; never executes unsafe artifacts. */
#include "../../src/mcs_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
mcs_function_t *mcs_load_image_ex(mcs_vm_t *vm, const uint8_t *image, size_t length, bool xip);
static unsigned checks, failures, baseline_accepts;
static size_t live_bytes;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c);                                        \
        }                                                                                          \
    } while (0)
/* Track actual allocation sizes independently of the VM's old-size hints.
 * Legacy zero-arity parameter metadata allocates one byte but frees with old=0;
 * baseline records that accounting mismatch without mistaking it for a leak. */
typedef union {
    size_t size;
    long double alignment;
    void *pointer_alignment;
} allocation_t;
static unsigned size_mismatches;
static void *allocate(void *ud, void *p, size_t old, size_t n) {
    (void)ud;
    allocation_t *previous = p ? (allocation_t *)p - 1 : NULL;
    size_t actual = previous ? previous->size : 0;
    if (actual != old)
        size_mismatches++;
    if (!n) {
        free(previous);
        live_bytes -= actual;
        return NULL;
    }
    allocation_t *result = realloc(previous, sizeof(*result) + n);
    if (!result)
        return NULL;
    result->size = n;
    live_bytes = live_bytes - actual + n;
    return result + 1;
}
static void quiet(void *ud, const char *s, size_t n) {
    (void)ud;
    (void)s;
    (void)n;
}
static mcs_vm_t *create(void) {
    mcs_config_t c;
    mcs_config_default(&c);
    c.realloc_fn = allocate;
    c.write_fn = quiet;
    c.error_fn = quiet;
    return mcs_new(&c);
}
typedef struct {
    uint8_t bytes[256];
    size_t used;
    int version;
} image_t;
static void byte(image_t *b, unsigned n) {
    if (b->used >= sizeof b->bytes)
        abort();
    b->bytes[b->used++] = (uint8_t)n;
}
static void varint(image_t *b, uint64_t n) {
    while (n >= 128) {
        byte(b, (unsigned)(n | 128));
        n >>= 7;
    }
    byte(b, (unsigned)n);
}
static void count(image_t *b, uint32_t n) {
    if (b->version >= 3)
        varint(b, n);
    else
        for (unsigned i = 0; i < 4; i++)
            byte(b, n >> (8 * i));
}
static void null_string(image_t *b) {
    if (b->version >= 3)
        byte(b, 0);
    else {
        byte(b, 255);
        byte(b, 255);
    }
}
static void header(image_t *b, int version, int malformed_varint) {
    memset(b, 0, sizeof *b);
    b->version = version;
    const char *magic = "MCSB";
    for (int i = 0; i < 4; i++)
        byte(b, (unsigned)magic[i]);
    byte(b, (unsigned)version);
    byte(b, 0);
    byte(b, 0);
    byte(b, 0);
    if (malformed_varint == 1) {
        byte(b, 128);
        byte(b, 0);
    } // nonminimal0
    else if (malformed_varint == 2) {
        for (int i = 0; i < 9; i++)
            byte(b, 128);
        byte(b, 2);
    } // uint64 overflow -> formerly0
    else
        count(b, 0);
}
static void function_header(image_t *b, unsigned arity, unsigned min, unsigned up, unsigned flags,
                            uint32_t slots, unsigned ptypes, unsigned tag) {
    null_string(b);
    null_string(b);
    byte(b, arity);
    byte(b, min);
    byte(b, up);
    byte(b, flags);
    if (b->version >= 3)
        count(b, slots);
    else {
        byte(b, slots);
        byte(b, slots >> 8);
    }
    byte(b, ptypes);
    if (ptypes)
        for (unsigned i = 0; i < arity; i++)
            byte(b, tag);
}
static void body(image_t *b, const uint8_t *code, uint32_t n) {
    count(b, n);
    for (uint32_t i = 0; i < n; i++)
        byte(b, code[i]);
    count(b, 0); // no debug lines
}
static void simple(image_t *b, int version, unsigned arity, unsigned min, unsigned up,
                   unsigned flags, uint32_t slots, unsigned ptypes, unsigned tag, int empty,
                   int bad_varint) {
    header(b, version, bad_varint);
    function_header(b, arity, min, up, flags, slots, ptypes, tag);
    count(b, 0);
    uint8_t code[] = {OP_RETURN_NULL};
    body(b, code, empty ? 0 : 1);
}
static void trial(const image_t *b, bool valid, bool baseline) {
    for (int xip = 0; xip < 2; xip++) {
        mcs_vm_t *vm = create();
        CHECK(vm != NULL);
        if (!vm)
            continue;
        mcs_function_t *fn = mcs_load_image_ex(vm, b->bytes, b->used, xip != 0);
        if (baseline && !valid)
            baseline_accepts += fn != NULL;
        else
            CHECK((fn != NULL) == valid);
        if (valid || (!baseline && !fn)) {
#if MCS_ENABLE_DISASM
            CHECK(mcs_disassemble_image(vm, b->bytes, b->used) ==
                  (valid ? MCS_OK : MCS_ERR_BYTECODE));
#endif
            // Malformed images are executed only AFTER the new rejection checks.
            CHECK((xip ? mcs_exec_image_xip(vm, b->bytes, b->used)
                       : mcs_exec_image(vm, b->bytes, b->used)) ==
                  (valid ? MCS_OK : MCS_ERR_BYTECODE));
        }
        mcs_free(vm);
        CHECK(live_bytes == 0);
    }
}
static void nested(image_t *b, int version, int kind) {
    header(b, version, 0);
    function_header(b, 0, 0, 0, 0, 1, 0, 0);
    count(b, 1);
    byte(b, 7); // K_FUNC
    function_header(b, (kind == 1 || kind >= 4) ? 1 : 0, kind == 1 ? 2 : 0,
                    (kind == 2 || kind == 3) ? 1 : 0, kind == 5 ? FN_HAS_PARAMS : 0,
                    (kind == 1 || kind >= 4) ? 2 : 1, kind >= 4 ? 1 : 0, kind == 4 ? 8 : PT_ANY);
    count(b, 0);
    uint8_t returned[] = {OP_RETURN_NULL};
    body(b, returned, 1);
    if (kind == 2 || kind == 3) {
        // Captures are [is_local,index], never an arbitrary truthy byte.
        uint8_t closure[] = {OP_CLOSURE, 0, 0, (uint8_t)(kind == 2 ? 2 : 1), 0, OP_RETURN_NULL};
        body(b, closure, 6);
    } else
        body(b, returned, 1);
}
static void integer_constant(image_t *b, int version, uint64_t value, int malformed) {
    header(b, version, 0);
    function_header(b, 0, 0, 0, 0, 1, 0, 0);
    count(b, 1);
    byte(b, 3); /* K_INT: zigzag in v3/4; fixed signed bits in v1/2 */
    if (version >= 3) {
        if (malformed == 1) {
            byte(b, 128);
            byte(b, 0);
        } else if (malformed == 2) {
            for (int i = 0; i < 9; i++)
                byte(b, 255);
            byte(b, 2);
        } else
            varint(b, value);
    } else
        for (unsigned i = 0; i < 8; i++)
            byte(b, (unsigned)(value >> (8 * i)));
    uint8_t returned[] = {OP_RETURN_NULL};
    body(b, returned, 1);
}
static void typed_child(image_t *b, int version, unsigned tag, unsigned flags, unsigned slots) {
    header(b, version, 0);
    function_header(b, 0, 0, 0, 0, 1, 0, 0);
    count(b, 1);
    byte(b, 7);
    function_header(b, 1, 1, 0, flags, slots, 1, tag);
    count(b, 0);
    uint8_t returned[] = {OP_RETURN_NULL};
    body(b, returned, 1);
    body(b, returned, 1);
}
/* Debug records are metadata, not code. End-of-code PCs are legal because
 * the optimizer may retain a trailing remapped line entry. */
static void debug_record(image_t *b, uint64_t pc, int64_t line) {
    if (b->version >= 3) {
        varint(b, pc);
        varint(b, ((uint64_t)line << 1) ^ (uint64_t) - (line < 0));
    } else {
        count(b, (uint32_t)pc);
        count(b, (uint32_t)line);
    }
}
static void debug_image(image_t *b, int version, int kind) {
    header(b, version, 0);
    function_header(b, 0, 0, 0, 0, 1, 0, 0);
    count(b, 0);
    uint8_t code[] = {OP_NULL, OP_RETURN_NULL};
    body(b, code, 2);
    b->used -= version >= 3 ? 1 : 4; /* replace zero line count */
    count(b, kind == 4 ? UINT32_MAX : 2);
    if (kind == 4)
        return; /* insufficient records: never allocate the huge count */
    switch (kind) {
    case 0:
        debug_record(b, 0, 10);
        debug_record(b, 2, version >= 3 ? -1 : 9);
        break;
    case 1:
        debug_record(b, 0, 1);
        debug_record(b, 3, 0);
        break; /* beyond code */
    case 2:
        debug_record(b, 1, 1);
        debug_record(b, version >= 3 ? UINT64_MAX : 0, 0);
        break;
    case 3:
        debug_record(b, version >= 3 ? UINT64_C(1) << 32 : UINT32_MAX, 1);
        debug_record(b, 0, 0);
        break;
    case 5:
        debug_record(b, 0, 0);
        debug_record(b, 0, -1);
        break;
    case 6:
        debug_record(b, 0, 1);
        debug_record(b, 0, INT64_MAX);
        break;
    case 7:
        debug_record(b, 0, UINT32_MAX);
        debug_record(b, 0, 1);
        break;
    case 8:
        debug_record(b, 0, 0);
        debug_record(b, 0, INT64_MIN);
        break;
    case 9:
        debug_record(b, 0, UINT32_MAX);
        debug_record(b, 0, version >= 3 ? -(int64_t)UINT32_MAX : 0);
        break;
    default:
        abort();
    }
}
static void debug_trials(int version, bool baseline) {
    image_t b;
    debug_image(&b, version, 0);
    trial(&b, true, baseline);
    debug_image(&b, version, 9);
    trial(&b, true, baseline);
    for (int k = 1; k <= 4; k++) {
        debug_image(&b, version, k);
        trial(&b, false, baseline);
    }
    if (version >= 3)
        for (int k = 5; k <= 8; k++) {
            debug_image(&b, version, k);
            trial(&b, false, baseline);
        }
}
int main(int argc, char **argv) {
    bool baseline = argc > 1 && !strcmp(argv[1], "--baseline-load-only");
    bool debug_baseline = argc > 1 && !strcmp(argv[1], "--debug-baseline-load-only");
    image_t b;
    for (int v = 1; v <= 4; v++) {
        if (debug_baseline) {
            debug_trials(v, true);
            continue;
        }
        if (!baseline)
            debug_trials(v, false);
        simple(&b, v, 0, 0, 0, 0, 1, 0, 0, 0, 0);
        trial(&b, true, baseline);
        simple(&b, v, 0, 0, 0, FN_XIP, 1, 0, 0, 0, 0);
        trial(&b, true, baseline); // stored XIP flag stays runtime-only
        simple(&b, v, 0, 1, 0, 0, 1, 0, 0, 0, 0);
        trial(&b, false, baseline);
        simple(&b, v, 0, 0, 1, 0, 1, 0, 0, 0, 0);
        trial(&b, false, baseline);
        simple(&b, v, 1, 0, 0, 0, 2, 0, 0, 0, 0);
        trial(&b, false, baseline); // entry is not a parameterized function
        simple(&b, v, 0, 0, 0, FN_HAS_PARAMS, 1, 0, 0, 0, 0);
        trial(&b, false, baseline);
        simple(&b, v, 0, 0, 0, 16, 1, 0, 0, 0, 0);
        trial(&b, false, baseline);
        simple(&b, v, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        trial(&b, false, baseline);
        simple(&b, v, 0, 0, 0, 0, 1, 2, 0, 0, 0);
        trial(&b, false, baseline);
        simple(&b, v, 0, 0, 0, 0, 1, 0, 0, 1, 0);
        trial(&b, false, baseline);
        nested(&b, v, 0);
        trial(&b, true, baseline);
        nested(&b, v, 1);
        trial(&b, false, baseline); // reject malformed child even when never called
        nested(&b, v, 2);
        trial(&b, false, baseline);
        nested(&b, v, 3);
        trial(&b, true, baseline);
        nested(&b, v, 4);
        trial(&b, false, baseline);
        nested(&b, v, 5);
        trial(&b, true, baseline);
        if (!baseline) {
            /* Positive canonical boundaries include the full ten-byte uint64
             * payload used by zigzag INT64_MIN, even on an INT32 build. */
            static const uint64_t integers[] = {
                0, 1, 127, 128, UINT32_MAX, UINT64_C(1) << 63, UINT64_MAX};
            for (size_t k = 0; k < sizeof(integers) / sizeof(integers[0]); k++) {
                integer_constant(&b, v, integers[k], 0);
                trial(&b, true, false);
            }
            for (unsigned tag = PT_ANY; tag <= PT_OBJECT; tag++) {
                typed_child(&b, v, tag, FN_IS_STATIC, 2);
                trial(&b, true, false);
            }
            typed_child(&b, v, PT_OBJECT, FN_IS_CTOR, 2);
            trial(&b, true, false);
            typed_child(&b, v, PT_ANY, 0, 1);
            trial(&b, false, false); /* missing parameter slot */
            typed_child(&b, v, PT_ANY, 0, UINT16_MAX);
            trial(&b, true, false); /* uncalled child */
            simple(&b, v, 0, 0, 0, 0, 1, 0, 0, 0, 0);
            size_t full = b.used;
            for (size_t truncated = 0; truncated < full; truncated++) {
                b.used = truncated;
                trial(&b, false, false);
            }
            b.used = full;
            if (v >= 3) {
                integer_constant(&b, v, 0, 1);
                trial(&b, false, false);
                integer_constant(&b, v, 0, 2);
                trial(&b, false, false);
                typed_child(&b, v, PT_ANY, 0, 65536);
                trial(&b, false, false);
            }
        }
        if (v >= 3) {
            simple(&b, v, 0, 0, 0, 0, 65537, 0, 0, 0, 0);
            trial(&b, false, baseline);
            simple(&b, v, 0, 0, 0, 0, UINT32_MAX, 0, 0, 0, 0);
            trial(&b, false, baseline);
            simple(&b, v, 0, 0, 0, 0, 1, 0, 0, 0, 1);
            trial(&b, false, baseline);
            simple(&b, v, 0, 0, 0, 0, 1, 0, 0, 0, 2);
            trial(&b, false, baseline);
        }
    }
    CHECK(size_mismatches == (baseline ? 8u : 0u));
    printf("%u/%u image metadata checks passed; baseline accepted malformed loads=%u (never "
           "executed); allocation size-hint mismatches=%u\n",
           checks - failures, checks, baseline_accepts, size_mismatches);
    return failures ? 1 : 0;
}
