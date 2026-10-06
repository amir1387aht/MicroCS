/* MicroCS - shared helpers for the standard library implementation. */
#ifndef MCS_LIB_H
#define MCS_LIB_H
#include "mcs_internal.h"

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#define NATIVE(name) static mcs_value_t name(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv)
#define CHECK() do { if (vm->has_exc) return mcs_null(); } while (0)
#define ARGN(n) do { if (argc < (n)) { mcs_throw(vm, EXC_ARGUMENT, "expected at least %d arguments", (n)); return mcs_null(); } } while (0)

static inline mcs_value_t lib_str(mcs_vm_t* vm, const char* s, size_t n) { return OBJ_VAL(mcs_intern(vm, s, n)); }
static inline mcs_value_t lib_cstr(mcs_vm_t* vm, const char* s) { return OBJ_VAL(mcs_intern_c(vm, s)); }
static inline bool lib_is_seq(mcs_value_t v) { return IS_KIND(v, MCS_O_ARRAY) || IS_KIND(v, MCS_O_LIST); }
/* `out`/`ref` arguments arrive as 1-element array cells (see compiler); store into one */
/* builds a ValueTuple; names = "A,B" or NULL (ItemN only) */
mcs_value_t mcs_lib_tuple(mcs_vm_t* vm, const char* names, int n, mcs_value_t* items);
static inline void lib_out_set(mcs_value_t cell, mcs_value_t v) {
    if (IS_KIND(cell, MCS_O_ARRAY) && ((mcs_list_t*)AS_OBJ(cell))->count >= 1) ((mcs_list_t*)AS_OBJ(cell))->items[0] = v;
}

/* call a delegate with n arguments; false if it threw */
static inline bool lib_call(mcs_vm_t* vm, mcs_value_t fn, int n, mcs_value_t* args, mcs_value_t* out) {
    return mcs_call_internal(vm, fn, mcs_null(), n, args, out) == MCS_OK;
}

/* shared helpers (mcs_lib.c) */
bool lib_parse_int(const char* s, size_t n, int base, mcs_int_t* out, bool* overflow);
#if MCS_ENABLE_FLOAT
bool lib_parse_float(const char* s, size_t n, mcs_float_t* out);
#endif
mcs_string_t* lib_need_str(mcs_vm_t* vm, mcs_value_t v, const char* param);
bool lib_equals(mcs_vm_t* vm, mcs_value_t a, mcs_value_t b);
mcs_class_t* lib_global_class(mcs_vm_t* vm, const char* name);
uint32_t lib_ticks(mcs_vm_t* vm);

/* per-area initialisers */
void mcs_lib_open_string(mcs_vm_t* vm, uint8_t mask);
void mcs_lib_open_collections(mcs_vm_t* vm, uint8_t mask);
#endif
