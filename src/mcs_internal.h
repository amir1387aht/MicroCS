/* MicroCS internal definitions. Not part of the public API. */
#ifndef MCS_INTERNAL_H
#define MCS_INTERNAL_H

#include "mcs.h"
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>

#define MCS_UNUSED(x) (void)(x)
/* small MCS_ERROR_SIZE values truncate messages on purpose */
#if MCS_ERROR_SIZE < 256 && defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 7
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

/* ----------------------------------------------------------- objects */
struct mcs_obj {
    uint8_t kind;
    uint8_t marked;
    uint16_t aux;   /* strings: global slot + 1 (0 = not a global name). Lives in
                     * what would otherwise be alignment padding: costs no RAM. */
    struct mcs_obj* next;
};

typedef struct mcs_string_s {
    mcs_obj_t obj;
    uint32_t hash;
    uint32_t len;
    char chars[1]; /* flexible, NUL terminated */
} mcs_string_t;

typedef struct { mcs_value_t key; mcs_value_t value; } mcs_entry_t;
/* weak set of interned strings: one pointer per slot instead of a full
 * key/value entry (4 B vs 16-32 B per slot on 32-bit targets) */
typedef struct { uint32_t count, cap; struct mcs_string_s** slots; } mcs_strset_t;
typedef struct { uint32_t count; uint32_t cap; mcs_entry_t* entries; } mcs_table_t;

typedef struct { uint32_t pc; uint32_t line; } mcs_line_t;

#define FN_HAS_PARAMS 0x01   /* last parameter is `params T[]` */
#define FN_IS_CTOR    0x02
#define FN_IS_STATIC  0x04
#define FN_XIP        0x08   /* code points into a caller-owned image (not freed) */

/* parameter type tags used for overload resolution */
enum { PT_ANY = 0, PT_INT, PT_FLOAT, PT_BOOL, PT_STRING, PT_CHAR, PT_VOID, PT_OBJECT };

typedef struct mcs_function {
    mcs_obj_t obj;
    uint8_t arity, min_arity, upvalue_count, flags;
    uint16_t max_slots;
    uint32_t code_len, code_cap;
    uint8_t* code;
    uint32_t const_count, const_cap;
    mcs_value_t* consts;
    uint32_t line_count, line_cap;
    mcs_line_t* lines;
    uint8_t* param_types; /* arity entries or NULL */
    mcs_string_t* name;
    mcs_string_t* source;
#if MCS_ENABLE_XIP
    const uint16_t* gmap;          /* XIP: image global index -> VM slot (NULL = identity) */
    struct mcs_string_s* gmap_obj; /* GC owner of gmap (shared by an image's functions) */
#endif
#if MCS_FIELD_CACHE
    struct mcs_fcache* fcache;  /* lazily allocated, indexed by name constant (const_count entries) */
    uint32_t fcache_n;
#endif
} mcs_function_t;
#if MCS_FIELD_CACHE
typedef struct mcs_fcache { struct mcs_class* cls; uint32_t slot; } mcs_fcache_t;
#endif

typedef struct mcs_upvalue {
    mcs_obj_t obj;
    mcs_value_t* location;
    mcs_value_t closed;
    struct mcs_upvalue* next_open;
} mcs_upvalue_t;

typedef struct {
    mcs_obj_t obj;
    mcs_function_t* fn;
    uint32_t upvalue_count;
    mcs_upvalue_t* upvalues[1];
} mcs_closure_t;

typedef struct {
    mcs_obj_t obj;
    mcs_native_fn fn;
    int16_t arity;
    mcs_string_t* name;
} mcs_native_t;

#define CLS_SCRIPT    0
#define CLS_BUILTIN   1   /* string, List... value-type helpers   */
#define CLS_STATIC    2   /* module: only static members           */
#define CLS_USERDATA  3   /* native class from mcs_class_def_t     */
#define CLS_INTERFACE 4
#define CLS_TUPLE     5   /* ValueTuple: structural equality/hash, Item1..ItemN */

typedef struct mcs_rom {        /* lazily materialized registration table */
    const mcs_reg_t* regs;
    struct mcs_rom* next;
    uint8_t statics;
} mcs_rom_t;

typedef struct mcs_class {
    mcs_obj_t obj;
    uint8_t ckind;
    uint8_t layout_shared;  /* fields + field_defaults borrowed from super (copy on write) */
    uint16_t field_count;
    mcs_string_t* name;
    struct mcs_class* super;
    mcs_table_t methods;   /* name -> closure/native/overloads */
    mcs_table_t getters;   /* name -> fn (self = instance or class) */
    mcs_table_t setters;
    mcs_table_t statics;   /* static fields + static methods */
    mcs_table_t fields;    /* name -> int slot */
    mcs_table_t ifaces;    /* interface name -> true */
    mcs_value_t* field_defaults;
    mcs_native_fn native_ctor;       /* builtin `new` (List, ...) */
    const mcs_class_def_t* def;      /* userdata classes */
    mcs_rom_t* rom;                  /* native members not yet materialized */
} mcs_class_t;

typedef struct {
    mcs_obj_t obj;
    mcs_class_t* cls;
    mcs_value_t fields[1];
} mcs_instance_t;

typedef struct {
    mcs_obj_t obj;
    mcs_class_t* cls;
    size_t size;
    uint64_t data[1]; /* aligned storage */
} mcs_userdata_t;

typedef struct {
    mcs_obj_t obj;
    mcs_value_t receiver;
    mcs_value_t method;
} mcs_bound_t;

typedef struct {
    mcs_obj_t obj;
    uint32_t count, cap;
    mcs_value_t* items;
} mcs_overloads_t;

typedef struct {            /* used for both arrays and List<T> */
    mcs_obj_t obj;
    uint32_t count, cap;
    mcs_value_t* items;
} mcs_list_t;

/* Dictionary / HashSet: insertion-ordered keys[]/vals[] arrays plus a compact
 * open-addressing index of positions (CPython-style). Index slots are 1, 2 or
 * 4 bytes wide depending on icap (0 = empty, all-ones = deleted, else
 * position + 1), so a small dictionary's index costs 1 byte per slot instead
 * of a full key/value entry. */
typedef struct {
    mcs_obj_t obj;
    uint32_t count, cap;     /* entries in keys[]/vals[] */
    uint32_t icap, iused;    /* index slots (power of two); used = live + deleted */
    void* idx;
    mcs_value_t* keys;
    mcs_value_t* vals;
} mcs_dict_t;
/* obj.aux flag: keys-only dictionary (HashSet storage, vals == NULL) */
#define DICT_KEYS_ONLY 1
#define DICT_VAL(d, i) ((d)->vals ? (d)->vals[i] : mcs_bool(true))

/* --------------------------------------------------------------- VM */
typedef struct {
    mcs_closure_t* closure;
    uint8_t* ip;
    mcs_value_t* slots;
    uint8_t argc;
} mcs_frame_t;

typedef struct {
    int frame;          /* frame index owning the handler */
    uint8_t* ip;        /* catch address */
    uint32_t sp;        /* stack depth to restore */
} mcs_handler_t;

enum {
    EXC_EXCEPTION, EXC_SYSTEM, EXC_NULLREF, EXC_INDEX, EXC_DIVZERO, EXC_INVCAST,
    EXC_ARGUMENT, EXC_ARGNULL, EXC_ARGRANGE, EXC_INVOP, EXC_KEYNOTFOUND,
    EXC_FORMAT, EXC_NOTSUPPORTED, EXC_NOTIMPL, EXC_OVERFLOW, EXC_STACKOVF,
    EXC_OOM, EXC_MISSINGMEMBER, EXC_IO, EXC__COUNT
};

#ifndef MCS_MAX_PINS
#define MCS_MAX_PINS 16
#endif

struct mcs_vm {
    mcs_config_t cfg;
    mcs_value_t* stack;
    mcs_value_t* sp;
    mcs_value_t* stack_end;
    mcs_frame_t* frames;
    int frame_count;
    mcs_handler_t handlers[MCS_MAX_HANDLERS];
    int handler_count;
    mcs_upvalue_t* open_upvalues;

    mcs_strset_t strings;       /* interned strings (weak) */
    mcs_table_t tuple_classes;  /* "arity:names" -> ValueTuple class (NOT inside the cls_* range marked by the GC) */
    mcs_value_t* globals;
    mcs_string_t** global_names;
    uint32_t global_count, global_cap;

    mcs_obj_t* objects;
    size_t bytes_allocated, next_gc, peak_bytes;
    uint32_t object_count, gc_count;
    mcs_obj_t** gray;
    uint32_t gray_count, gray_cap;
    int gc_pause;
    bool gc_wanted;
    mcs_value_t roots[MCS_MAX_ROOTS];
    int root_count;
    mcs_value_t pins[MCS_MAX_PINS];
    mcs_value_t order_last, order_spec; /* OrderBy result + [sel, desc, ...] for ThenBy */
    mcs_function_t* compiling;  /* chain of functions under compilation */

    /* built-in classes */
    mcs_class_t* cls_object;
    mcs_class_t* cls_string;
    mcs_class_t* cls_int;
    mcs_class_t* cls_float;
    mcs_class_t* cls_bool;
    mcs_class_t* cls_char;
    mcs_class_t* cls_array;
    mcs_class_t* cls_list;
    mcs_class_t* cls_dict;
    mcs_class_t* cls_kvp;
    mcs_class_t* cls_delegate;
    mcs_class_t* exc[EXC__COUNT];

    /* pre-interned names */
    mcs_string_t* s_ctor;
    mcs_string_t* s_init;
    mcs_string_t* s_tostring;
    mcs_string_t* s_message;
    mcs_string_t* s_item;
    mcs_string_t* s_main;
    mcs_string_t* s_key;
    mcs_string_t* s_value;
    mcs_string_t* s_equals;

    /* exceptions */
    bool has_exc;
    mcs_value_t exc_value;
    int run_depth;
    volatile bool abort_req;
    uint32_t hook_counter;
    uint32_t hook_reload;       /* value hook_counter was last set to */
    uint32_t steps_used;
    uint32_t run_start;
    mcs_limits_t limits;
    uint8_t abort_reason;
    uint8_t abort_hint;         /* reason recorded by mcs_safepoint() for the pending abort */
    mcs_idle_fn idle_fn;        /* run while Thread.Sleep waits (HAL event dispatch) */
    void* idle_ud;
    jmp_buf* panic;
    void* ext[MCS_EXT__COUNT];

    char error[MCS_ERROR_SIZE];
};

/* -------------------------------------------------------- opcodes */
#define MCS_OPCODES(X) \
    X(CONST, 2) X(NULL, 0) X(TRUE, 0) X(FALSE, 0) X(INT8, 1) \
    X(POP, 0) X(DUP, 0) X(DUP2, 0) X(SWAP, 0) X(ROT, 1) \
    X(GET_LOCAL, 1) X(SET_LOCAL, 1) X(GET_UPVAL, 1) X(SET_UPVAL, 1) \
    X(GET_GLOBAL, 2) X(SET_GLOBAL, 2) X(GET_FIELD, 2) X(SET_FIELD, 2) \
    X(GET_INDEX, 0) X(SET_INDEX, 0) \
    X(ADD, 0) X(SUB, 0) X(MUL, 0) X(DIV, 0) X(MOD, 0) X(NEG, 0) \
    X(BAND, 0) X(BOR, 0) X(BXOR, 0) X(BNOT, 0) X(SHL, 0) X(SHR, 0) X(USHR, 0) X(NOT, 0) \
    X(EQ, 0) X(NE, 0) X(LT, 0) X(LE, 0) X(GT, 0) X(GE, 0) \
    X(INC_LOCAL, 2) \
    X(JUMP, 2) X(JUMP_IF_FALSE, 2) X(JUMP_IF_TRUE, 2) X(JUMP_IF_FALSE_KEEP, 2) \
    X(JUMP_IF_TRUE_KEEP, 2) X(JUMP_IF_NULL_KEEP, 2) X(JUMP_IF_NOT_NULL_KEEP, 2) X(LOOP, 2) \
    X(ARGC_JUMP, 3) \
    X(CALL, 1) X(INVOKE, 3) X(SUPER_INVOKE, 3) \
    X(CLOSURE, 2) X(CLOSE_UPVAL, 0) X(RETURN, 0) X(RETURN_NULL, 0) \
    X(CLASS, 3) X(INHERIT, 0) X(IMPLEMENTS, 2) X(FIELD, 4) X(METHOD, 3) X(STATIC, 3) \
    X(GETTER, 3) X(SETTER, 3) \
    X(ARRAY, 2) X(NEW_ARRAY, 1) X(CONV, 1) X(TOSTR, 0) X(TOSTR_FMT, 0) X(CONCAT, 1) \
    X(IS, 2) X(AS, 2) X(CAST, 2) X(FOR_ITER, 3) \
    X(TRY, 2) X(END_TRY, 0) X(THROW, 0) \
    /* image v2: superinstructions (statement stores, fused compare-and-branch) */ \
    X(SET_LOCAL_POP, 1) X(SET_GLOBAL_POP, 2) \
    X(JF_EQ, 2) X(JF_NE, 2) X(JF_LT, 2) X(JF_LE, 2) X(JF_GT, 2) X(JF_GE, 2)

typedef enum {
#define X(name, len) OP_##name,
    MCS_OPCODES(X)
#undef X
    OP__COUNT
} mcs_opcode_t;

extern const uint8_t mcs_op_len[OP__COUNT];
extern const char* const mcs_op_name[OP__COUNT];

/* conversion kinds for OP_CONV and array defaults */
enum { CV_INT = 0, CV_FLOAT, CV_CHAR, CV_BYTE, CV_SBYTE, CV_SHORT, CV_USHORT, CV_BOOL, CV_NULL, CV_UINT };

/* --------------------------------------------------------- helpers */
#define IS_OBJ(v)      ((v).type == MCS_T_OBJ)
#define AS_OBJ(v)      ((v).as.o)
#define OBJ_KIND(v)    (AS_OBJ(v)->kind)
#define IS_KIND(v, k)  (IS_OBJ(v) && OBJ_KIND(v) == (k))
#define IS_STRING(v)   IS_KIND(v, MCS_O_STRING)
#define AS_STRING(v)   ((mcs_string_t*)AS_OBJ(v))
#define AS_CSTR(v)     (AS_STRING(v)->chars)
#define AS_CLOSURE(v)  ((mcs_closure_t*)AS_OBJ(v))
#define AS_FUNCTION(v) ((mcs_function_t*)AS_OBJ(v))
#define AS_NATIVE(v)   ((mcs_native_t*)AS_OBJ(v))
#define AS_CLASS(v)    ((mcs_class_t*)AS_OBJ(v))
#define AS_INSTANCE(v) ((mcs_instance_t*)AS_OBJ(v))
#define AS_LIST(v)     ((mcs_list_t*)AS_OBJ(v))
#define AS_DICT(v)     ((mcs_dict_t*)AS_OBJ(v))
#define AS_BOUND(v)    ((mcs_bound_t*)AS_OBJ(v))
#define AS_UDATA(v)    ((mcs_userdata_t*)AS_OBJ(v))
#define AS_OVL(v)      ((mcs_overloads_t*)AS_OBJ(v))
#define OBJ_VAL(p)     mcs_obj_val((mcs_obj_t*)(p))

static inline mcs_value_t mcs_obj_val(mcs_obj_t* o) { mcs_value_t v; v.type = MCS_T_OBJ; v.as.o = o; return v; }
static inline bool is_intlike_v(mcs_value_t v) { return v.type == MCS_T_INT || v.type == MCS_T_CHAR; }
static inline mcs_value_t mcs_undef(void) { mcs_value_t v; v.type = MCS_T_UNDEF; v.as.i = 0; return v; }

/* memory (mcs_object.c) */
void* mcs_realloc(mcs_vm_t* vm, void* p, size_t old, size_t nsz);
#define MCS_ALLOC(vm, T, n) ((T*)mcs_realloc(vm, NULL, 0, sizeof(T) * (n)))
#define MCS_FREE(vm, T, p, n) mcs_realloc(vm, p, sizeof(T) * (n), 0)
#define MCS_GROW(vm, T, p, o, n) ((T*)mcs_realloc(vm, p, sizeof(T) * (o), sizeof(T) * (n)))
void mcs_panic(mcs_vm_t* vm, mcs_result_t code, const char* msg);
void mcs_maybe_gc(mcs_vm_t* vm);
void mcs_collect(mcs_vm_t* vm);
void mcs_free_objects(mcs_vm_t* vm);

/* objects */
mcs_obj_t* mcs_alloc_obj(mcs_vm_t* vm, size_t size, uint8_t kind);
mcs_string_t* mcs_intern(mcs_vm_t* vm, const char* s, size_t len);
mcs_string_t* mcs_intern_c(mcs_vm_t* vm, const char* s);
/* already-interned string or NULL (never allocates) */
mcs_string_t* mcs_find_interned(mcs_vm_t* vm, const char* s, size_t len);
void mcs_strset_free(mcs_vm_t* vm, mcs_strset_t* set);
mcs_string_t* mcs_take_buffer(mcs_vm_t* vm, char* buf, size_t len, size_t cap); /* frees buf */
uint32_t mcs_hash_bytes(const char* s, size_t len);
mcs_function_t* mcs_new_function(mcs_vm_t* vm);
mcs_closure_t* mcs_new_closure(mcs_vm_t* vm, mcs_function_t* fn);
mcs_upvalue_t* mcs_new_upvalue(mcs_vm_t* vm, mcs_value_t* slot);
#define MCS_TAB_METHODS 0
#define MCS_TAB_STATICS 1
#define MCS_TAB_GETTERS 2
#define MCS_TAB_SETTERS 3
/* member lookup that materializes ROM-registered natives on first use */
bool mcs_cls_get(mcs_vm_t* vm, mcs_class_t* c, int which, mcs_string_t* name, mcs_value_t* out);
void mcs_class_add_rom(mcs_vm_t* vm, mcs_class_t* c, const mcs_reg_t* regs, bool statics);
mcs_native_t* mcs_new_native(mcs_vm_t* vm, mcs_native_fn fn, int arity, mcs_string_t* name);
mcs_class_t* mcs_new_class(mcs_vm_t* vm, mcs_string_t* name, uint8_t ckind);
mcs_instance_t* mcs_new_instance(mcs_vm_t* vm, mcs_class_t* cls);
mcs_userdata_t* mcs_new_userdata(mcs_vm_t* vm, mcs_class_t* cls, size_t size);
mcs_bound_t* mcs_new_bound(mcs_vm_t* vm, mcs_value_t recv, mcs_value_t method);
mcs_overloads_t* mcs_new_overloads(mcs_vm_t* vm);
mcs_list_t* mcs_new_listobj(mcs_vm_t* vm, uint8_t kind, uint32_t count);
void mcs_listobj_push(mcs_vm_t* vm, mcs_list_t* l, mcs_value_t v);
void mcs_listobj_insert(mcs_vm_t* vm, mcs_list_t* l, uint32_t at, mcs_value_t v);
void mcs_listobj_remove_at(mcs_list_t* l, uint32_t at);
mcs_dict_t* mcs_new_dict(mcs_vm_t* vm);
bool mcs_dict_get(mcs_dict_t* d, mcs_value_t key, mcs_value_t* out);
void mcs_dict_set(mcs_vm_t* vm, mcs_dict_t* d, mcs_value_t key, mcs_value_t v);
bool mcs_dict_remove(mcs_vm_t* vm, mcs_dict_t* d, mcs_value_t key);
void mcs_dict_clear(mcs_vm_t* vm, mcs_dict_t* d);
void mcs_class_add_field(mcs_vm_t* vm, mcs_class_t* cls, mcs_string_t* name, mcs_value_t def);
void mcs_class_inherit_shared(mcs_vm_t* vm, mcs_class_t* cls, mcs_class_t* super);
void mcs_class_inherit(mcs_vm_t* vm, mcs_class_t* cls, mcs_class_t* super);
void mcs_class_add_method(mcs_vm_t* vm, mcs_table_t* t, mcs_string_t* name, mcs_value_t fn, bool first);

/* tables */
void mcs_table_init(mcs_table_t* t);
void mcs_table_free(mcs_vm_t* vm, mcs_table_t* t);
bool mcs_table_get(const mcs_table_t* t, mcs_value_t key, mcs_value_t* out);
bool mcs_table_set(mcs_vm_t* vm, mcs_table_t* t, mcs_value_t key, mcs_value_t v);
bool mcs_tuple_equal(mcs_value_t a, mcs_value_t b, bool same);
bool mcs_table_delete(mcs_table_t* t, mcs_value_t key);
void mcs_table_copy(mcs_vm_t* vm, const mcs_table_t* from, mcs_table_t* to);
mcs_string_t* mcs_table_find_string(const mcs_table_t* t, const char* s, size_t len, uint32_t hash);
uint32_t mcs_value_hash(mcs_value_t v);
static inline bool mcs_table_get_s(const mcs_table_t* t, mcs_string_t* s, mcs_value_t* out) { return mcs_table_get(t, OBJ_VAL(s), out); }

/* value semantics (mcs_value.c in vm) */
bool mcs_values_same(mcs_value_t a, mcs_value_t b);   /* identity/key equality */
bool mcs_values_equal(mcs_value_t a, mcs_value_t b);  /* C# == for builtins */
const char* mcs_type_name(mcs_vm_t* vm, mcs_value_t v);
mcs_class_t* mcs_class_of(mcs_vm_t* vm, mcs_value_t v);
/* append ToString() of v into a growable buffer; may call script ToString */
typedef struct { char* data; size_t len, cap; mcs_vm_t* vm; } mcs_buf_t;
void mcs_buf_init(mcs_buf_t* b, mcs_vm_t* vm);
void mcs_buf_putn(mcs_buf_t* b, const char* s, size_t n);
void mcs_buf_puts(mcs_buf_t* b, const char* s);
void mcs_buf_putc(mcs_buf_t* b, char c);
void mcs_buf_free(mcs_buf_t* b);
mcs_string_t* mcs_buf_to_string(mcs_buf_t* b); /* consumes buffer */
void mcs_buf_utf8(mcs_buf_t* b, uint32_t cp);
bool mcs_value_to_buf(mcs_vm_t* vm, mcs_buf_t* b, mcs_value_t v); /* false if exception */
mcs_string_t* mcs_value_to_string(mcs_vm_t* vm, mcs_value_t v);   /* NULL if exception */
void mcs_format_int(char* out, mcs_int_t v);
#if MCS_ENABLE_FLOAT
void mcs_format_float(char* out, mcs_float_t f);
#endif
bool mcs_format_spec(mcs_vm_t* vm, mcs_buf_t* b, mcs_value_t v, const char* spec, size_t len);
bool mcs_format_string(mcs_vm_t* vm, mcs_buf_t* b, const char* fmt, size_t flen, int argc, mcs_value_t* argv);

/* VM (mcs_vm.c) */
uint32_t mcs_global_slot(mcs_vm_t* vm, mcs_string_t* name);
void mcs_throw(mcs_vm_t* vm, int exc_kind, const char* fmt, ...);
void mcs_throw_value(mcs_vm_t* vm, mcs_value_t exc);
mcs_result_t mcs_run_closure(mcs_vm_t* vm, mcs_closure_t* cl);
mcs_result_t mcs_call_internal(mcs_vm_t* vm, mcs_value_t callee, mcs_value_t self, int argc, const mcs_value_t* argv, mcs_value_t* result);
bool mcs_lookup_member(mcs_vm_t* vm, mcs_value_t recv, mcs_string_t* name, mcs_value_t* out);
bool mcs_get_member(mcs_vm_t* vm, mcs_value_t obj, mcs_string_t* name, mcs_value_t* out);
bool mcs_set_member(mcs_vm_t* vm, mcs_value_t obj, mcs_string_t* name, mcs_value_t v);
bool mcs_is_instance_of(mcs_vm_t* vm, mcs_value_t v, mcs_string_t* type_name);
mcs_instance_t* mcs_make_exception(mcs_vm_t* vm, mcs_class_t* cls, const char* msg);
int mcs_compare_values(mcs_vm_t* vm, mcs_value_t a, mcs_value_t b, bool* ok);
void mcs_report_error(mcs_vm_t* vm, const char* fmt, ...);
void mcs_write(mcs_vm_t* vm, const char* s, size_t n);
uint32_t mcs_line_of(mcs_function_t* fn, uint32_t pc);

/* compiler (mcs_compiler.c) */
#if MCS_ENABLE_COMPILER
mcs_function_t* mcs_compile(mcs_vm_t* vm, const char* name, const char* src);
#endif
/* bytecode (mcs_bytecode.c) */
void mcs_fn_emit(mcs_vm_t* vm, mcs_function_t* fn, uint8_t byte, uint32_t line);
uint32_t mcs_fn_add_const(mcs_vm_t* vm, mcs_function_t* fn, mcs_value_t v);
#if MCS_ENABLE_DISASM
void mcs_disassemble(mcs_vm_t* vm, mcs_function_t* fn, int depth);
#endif

/* library (mcs_lib.c) */
void mcs_open_libs(mcs_vm_t* vm, uint8_t mask);
mcs_class_t* mcs_define_builtin_class(mcs_vm_t* vm, const char* name, mcs_class_t* super, uint8_t ckind);
void mcs_add_regs(mcs_vm_t* vm, mcs_class_t* cls, const mcs_reg_t* regs, bool statics);
void mcs_add_regs_eager(mcs_vm_t* vm, mcs_class_t* cls, const mcs_reg_t* regs, bool statics);

#endif
