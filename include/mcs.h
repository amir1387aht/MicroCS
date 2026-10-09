/*
 * MicroCS - a compact C# interpreter for microcontrollers.
 * Public embedding API.
 *
 *   mcs_config_t cfg; mcs_config_default(&cfg);
 *   mcs_vm_t* vm = mcs_new(&cfg);
 *   mcs_register_module(vm, "Gpio", gpio_fns);   // expose C functions
 *   mcs_exec_source(vm, "main.cs", "Console.WriteLine(\"hi\");");
 *   mcs_free(vm);
 */
#ifndef MCS_H
#define MCS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "mcs_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MCS_VERSION_MAJOR 1
#define MCS_VERSION_MINOR 7
#define MCS_VERSION_PATCH 0
#define MCS_VERSION_STRING "1.7.0"

#if MCS_INT64
typedef int64_t mcs_int_t;
typedef uint64_t mcs_uint_t;
#else
typedef int32_t mcs_int_t;
typedef uint32_t mcs_uint_t;
#endif
#if MCS_FLOAT_DOUBLE
typedef double mcs_float_t;
#else
typedef float mcs_float_t;
#endif

typedef struct mcs_vm mcs_vm_t;
typedef struct mcs_obj mcs_obj_t;

/* ------------------------------------------------------------- values */
typedef enum {
    MCS_T_NULL = 0,
    MCS_T_BOOL,
    MCS_T_INT,
    MCS_T_FLOAT,
    MCS_T_CHAR,
    MCS_T_OBJ,
    MCS_T_UNDEF /* internal: unassigned global */
} mcs_type_t;

#if MCS_COMPACT_VALUES
#pragma pack(push, 4)   /* 12-byte values: 8-byte payload at offset 4 */
#endif
typedef struct {
    uint8_t type;
    union {
        bool b;
        mcs_int_t i;
#if MCS_ENABLE_FLOAT
        mcs_float_t f;   /* without floats a double would make every value 12-16 bytes */
#endif
        mcs_obj_t* o;
    } as;
} mcs_value_t;
#if MCS_COMPACT_VALUES
#pragma pack(pop)
#endif

/* Object kinds (returned by mcs_obj_kind) */
typedef enum {
    MCS_O_STRING, MCS_O_FUNCTION, MCS_O_CLOSURE, MCS_O_UPVALUE, MCS_O_NATIVE,
    MCS_O_CLASS, MCS_O_INSTANCE, MCS_O_BOUND, MCS_O_OVERLOADS, MCS_O_ARRAY,
    MCS_O_LIST, MCS_O_DICT, MCS_O_USERDATA
} mcs_obj_kind_t;

typedef enum {
    MCS_OK = 0,
    MCS_ERR_COMPILE,   /* syntax / semantic error */
    MCS_ERR_RUNTIME,   /* uncaught exception */
    MCS_ERR_MEMORY,    /* out of memory */
    MCS_ERR_BYTECODE,  /* invalid/incompatible image */
    MCS_ERR_ABORTED    /* hook requested abort */
} mcs_result_t;

/* Native function. `self` is the receiver for methods / getters / setters
 * (null for module functions). Return the result (mcs_null() for void).
 * Raise errors with mcs_raise(); the return value is then ignored. */
typedef mcs_value_t (*mcs_native_fn)(mcs_vm_t* vm, mcs_value_t self,
                                     int argc, mcs_value_t* argv);

/* Table entry used to register functions, methods and properties.
 *   kind 'f' : function / method       name: "Write"
 *   kind 'g' : property getter          name: "Value"
 *   kind 's' : property setter          name: "Value"  (argv[0] = value)
 * arity: exact number of arguments or -1 for variadic. */
typedef struct {
    const char* name;
    mcs_native_fn fn;
    int8_t arity;
    char kind;
} mcs_reg_t;

#define MCS_FN(name, fn, arity)  { name, fn, arity, 'f' }
#define MCS_GET(name, fn)        { name, fn, 0, 'g' }
#define MCS_SET(name, fn)        { name, fn, 1, 's' }
#define MCS_REG_END              { NULL, NULL, 0, 0 }

/* Definition of a native (userdata) class, e.g. a wrapper around a UART
 * handle. Instances carry `instance_size` bytes of C data. */
typedef struct {
    const char* name;
    size_t instance_size;
    /* Called for `new Name(args)`; `self` is the new instance, its data is
     * zeroed. May raise. */
    void (*ctor)(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv);
    /* Called when the GC frees the instance (close handles, etc.). */
    void (*finalizer)(mcs_vm_t* vm, void* data);
    const mcs_reg_t* members;      /* methods & properties */
    const mcs_reg_t* statics;      /* static methods / properties */
} mcs_class_def_t;

/* Integer constants of a module, kept in flash: { "Output", 1 }. The table
 * must have static storage duration; see mcs_register_consts(). */
typedef struct {
    const char* name;
    int32_t value;
} mcs_const_t;
#define MCS_CONST(name, value)   { name, value }
#define MCS_CONST_END            { NULL, 0 }

/* --------------------------------------------------------- configuration */
typedef void* (*mcs_realloc_fn)(void* ud, void* ptr, size_t old_size, size_t new_size);
typedef void (*mcs_write_fn)(void* ud, const char* text, size_t len);
typedef int (*mcs_readline_fn)(void* ud, char* buf, size_t cap); /* <0 = EOF */
typedef uint32_t (*mcs_ticks_fn)(void* ud);       /* milliseconds */
typedef void (*mcs_delay_fn)(void* ud, uint32_t ms);
/* Called periodically from the interpreter loop. Use it to feed a watchdog,
 * poll for Ctrl-C, or yield to an RTOS. Return non-zero to abort the script. */
typedef int (*mcs_hook_fn)(mcs_vm_t* vm, void* ud);

typedef struct {
    mcs_realloc_fn realloc_fn;  /* NULL = standard realloc/free */
    mcs_write_fn write_fn;      /* Console output; NULL = stdout   */
    mcs_write_fn error_fn;      /* error output;   NULL = write_fn */
    mcs_readline_fn readline_fn;/* Console.ReadLine; NULL = stdin  */
    mcs_ticks_fn ticks_fn;      /* Environment.TickCount           */
    mcs_delay_fn delay_fn;      /* Thread.Sleep                    */
    mcs_hook_fn hook_fn;        /* periodic hook, may be NULL      */
    void* user_data;            /* passed to I/O + hook callbacks  */
    void* alloc_ud;             /* passed to realloc_fn            */
    uint32_t stack_slots;       /* value stack size                */
    uint16_t max_frames;        /* call depth                      */
    size_t heap_limit;          /* 0 = unlimited; else GC hard cap */
    uint8_t stdlib;             /* MCS_LIB_* bitmask of libraries  */
    uint8_t alloc_overhead;     /* allocator bytes per block (header); 0 = count
                                   requested sizes only. With it set, every block
                                   counts as round_up(size + overhead) against
                                   heap_limit, so the GC runs before the pool is
                                   full. mcs_new() sets it for mcs_pool_realloc. */
} mcs_config_t;

#define MCS_LIB_CORE        0x01  /* Console, Convert, exceptions, string... */
#define MCS_LIB_MATH        0x02
#define MCS_LIB_COLLECTIONS 0x04  /* List, Dictionary */
#define MCS_LIB_TEXT        0x08  /* StringBuilder, String.Format */
#define MCS_LIB_SYSTEM      0x10  /* Environment, Thread, GC, Random */
#define MCS_LIB_ALL         0xFF

void mcs_config_default(mcs_config_t* cfg);

/* ------------------------------------------------------------ lifecycle */
mcs_vm_t* mcs_new(const mcs_config_t* cfg);
void mcs_free(mcs_vm_t* vm);
void* mcs_user_data(mcs_vm_t* vm);

/* ------------------------------------------------------------- running */
#if MCS_ENABLE_COMPILER
/* Compile and run C# source. Top-level statements run immediately; if the
 * program only declares classes, `static void Main()` is invoked. */
mcs_result_t mcs_exec_source(mcs_vm_t* vm, const char* name, const char* src);
#endif
#if MCS_ENABLE_COMPILER && MCS_ENABLE_BYTECODE_SAVE
/* Compile source to a portable bytecode image (free with mcs_free_image). */
mcs_result_t mcs_compile_image(mcs_vm_t* vm, const char* name, const char* src,
                               bool strip_lines, uint8_t** out, size_t* out_len);
/* Same with MCS_IMAGE_* flags. Images are optimized by default (superinstructions,
 * see docs/BYTECODE.md); MCS_IMAGE_NO_OPT produces an image for a VM built with
 * MCS_ENABLE_SUPEROPS=0. */
#define MCS_IMAGE_STRIP  1u   /* drop line tables and source names */
#define MCS_IMAGE_NO_OPT 2u   /* skip the optimizer */
mcs_result_t mcs_compile_image_ex(mcs_vm_t* vm, const char* name, const char* src,
                                  unsigned flags, uint8_t** out, size_t* out_len);
void mcs_free_image(mcs_vm_t* vm, uint8_t* image);
#endif
#if MCS_ENABLE_BYTECODE_LOAD
/* Run a bytecode image (e.g. linked into flash as a const array). */
mcs_result_t mcs_exec_image(mcs_vm_t* vm, const uint8_t* image, size_t len);
/* Same, but executes bytecode directly from `image` (execute in place) instead
 * of copying each function's code to the heap: saves RAM roughly equal to the
 * code size of the image. `image` must stay valid and unchanged until
 * mcs_free(vm) - functions and classes it defines keep pointing into it.
 * Ideal for images linked into flash. Without MCS_ENABLE_XIP it copies. */
mcs_result_t mcs_exec_image_xip(mcs_vm_t* vm, const uint8_t* image, size_t len);
#endif

/* Call a global function, or "Class.StaticMethod". */
mcs_result_t mcs_call(mcs_vm_t* vm, const char* name, int argc,
                      const mcs_value_t* argv, mcs_value_t* result);
/* Call any callable value (delegate/lambda received from script). */
mcs_result_t mcs_call_value(mcs_vm_t* vm, mcs_value_t callee, int argc,
                            const mcs_value_t* argv, mcs_value_t* result);
/* Create an object: `new ClassName(args)` (script or native class). */
mcs_result_t mcs_new_object(mcs_vm_t* vm, const char* class_name, int argc,
                            const mcs_value_t* argv, mcs_value_t* result);
/* Call method `name` on an object. */
mcs_result_t mcs_invoke(mcs_vm_t* vm, mcs_value_t recv, const char* name, int argc,
                        const mcs_value_t* argv, mcs_value_t* result);

/* Last error message (compile error or uncaught exception text). */
const char* mcs_last_error(mcs_vm_t* vm);
/* Abort the running script from an ISR/hook (checked at safe points). */
void mcs_request_abort(mcs_vm_t* vm);

/* ------------------------------------------------------------ bindings */
/* Register a static class / module: `Name.Func(...)`, `Name.Prop`. */
void mcs_register_module(mcs_vm_t* vm, const char* name, const mcs_reg_t* fns);
/* Register a native class usable with `new Name(...)`. */
void mcs_register_class(mcs_vm_t* vm, const mcs_class_def_t* def);
/* Constants inside a module: Gpio.HIGH. Forces a lazily registered module to
 * be created; prefer mcs_register_consts() for fixed values. */
void mcs_module_set(mcs_vm_t* vm, const char* module, const char* name, mcs_value_t v);
/* Global variables / functions */
void mcs_set_global(mcs_vm_t* vm, const char* name, mcs_value_t v);
mcs_value_t mcs_get_global(mcs_vm_t* vm, const char* name);
void mcs_register_function(mcs_vm_t* vm, const char* name, mcs_native_fn fn, int arity);

/* Raise an exception of the given built-in or script class name
 * ("ArgumentException", "InvalidOperationException", ...). */
void mcs_raise(mcs_vm_t* vm, const char* exc_class, const char* fmt, ...);
bool mcs_has_exception(mcs_vm_t* vm);

/* Userdata access for native classes */
void* mcs_userdata(mcs_value_t v);                  /* NULL if not userdata */
void* mcs_check_userdata(mcs_vm_t* vm, mcs_value_t v, const mcs_class_def_t* def);

/* ------------------------------------------------------- value helpers */
static inline mcs_value_t mcs_null(void) { mcs_value_t v; v.type = MCS_T_NULL; v.as.i = 0; return v; }
static inline mcs_value_t mcs_bool(bool b) { mcs_value_t v; v.type = MCS_T_BOOL; v.as.i = 0; v.as.b = b; return v; }
static inline mcs_value_t mcs_int(mcs_int_t i) { mcs_value_t v; v.type = MCS_T_INT; v.as.i = i; return v; }
static inline mcs_value_t mcs_char(uint32_t c) { mcs_value_t v; v.type = MCS_T_CHAR; v.as.i = (mcs_int_t)c; return v; }
#if MCS_ENABLE_FLOAT
static inline mcs_value_t mcs_float(mcs_float_t f) { mcs_value_t v; v.type = MCS_T_FLOAT; v.as.f = f; return v; }
#endif
static inline bool mcs_is_null(mcs_value_t v) { return v.type == MCS_T_NULL; }
static inline bool mcs_is_bool(mcs_value_t v) { return v.type == MCS_T_BOOL; }
static inline bool mcs_is_int(mcs_value_t v) { return v.type == MCS_T_INT; }
static inline bool mcs_is_float(mcs_value_t v) { return v.type == MCS_T_FLOAT; }
static inline bool mcs_is_number(mcs_value_t v) { return v.type == MCS_T_INT || v.type == MCS_T_FLOAT || v.type == MCS_T_CHAR; }
bool mcs_is_string(mcs_value_t v);
int mcs_obj_kind(mcs_value_t v); /* -1 if not an object */

mcs_value_t mcs_string(mcs_vm_t* vm, const char* s);
mcs_value_t mcs_string_n(mcs_vm_t* vm, const char* s, size_t len);
const char* mcs_cstr(mcs_value_t v);          /* NULL if not a string */
size_t mcs_strlen(mcs_value_t v);
bool mcs_truthy(mcs_value_t v);

/* Checked conversions: raise InvalidCastException/ArgumentException on
 * mismatch and return 0 / "" so natives can be written linearly. */
mcs_int_t mcs_to_int(mcs_vm_t* vm, mcs_value_t v);
#if MCS_ENABLE_FLOAT
mcs_float_t mcs_to_float(mcs_vm_t* vm, mcs_value_t v);
#endif
bool mcs_to_bool(mcs_vm_t* vm, mcs_value_t v);
const char* mcs_to_cstr(mcs_vm_t* vm, mcs_value_t v);
/* Convert any value to its C# ToString() representation. */
mcs_value_t mcs_tostring(mcs_vm_t* vm, mcs_value_t v);

/* Arrays and lists */
mcs_value_t mcs_new_array(mcs_vm_t* vm, uint32_t len);
mcs_value_t mcs_new_list(mcs_vm_t* vm);
uint32_t mcs_len(mcs_value_t v);                      /* array/list/string */
mcs_value_t mcs_index(mcs_value_t v, uint32_t i);     /* unchecked */
void mcs_set_index(mcs_value_t v, uint32_t i, mcs_value_t x);
void mcs_list_add(mcs_vm_t* vm, mcs_value_t list, mcs_value_t x);
/* Script objects: get/set a field or property by name */
mcs_value_t mcs_get_field(mcs_vm_t* vm, mcs_value_t obj, const char* name);
void mcs_set_field(mcs_vm_t* vm, mcs_value_t obj, const char* name, mcs_value_t v);

/* ------------------------------------------------------------- memory */
/* Keep a value alive while it is only referenced from C code. */
void mcs_push_root(mcs_vm_t* vm, mcs_value_t v);
void mcs_pop_root(mcs_vm_t* vm, int n);
/* Persistent handles (e.g. a callback stored by a driver). */
int mcs_pin(mcs_vm_t* vm, mcs_value_t v);   /* returns handle id or -1 */
mcs_value_t mcs_pinned(mcs_vm_t* vm, int handle);
void mcs_unpin(mcs_vm_t* vm, int handle);
void mcs_gc(mcs_vm_t* vm);
typedef struct { size_t bytes_in_use; size_t peak_bytes; size_t next_gc; uint32_t objects; uint32_t collections; } mcs_mem_stats_t;
void mcs_mem_stats(mcs_vm_t* vm, mcs_mem_stats_t* st);

#if MCS_ENABLE_POOL_HEAP
/* Simple first-fit pool allocator over a static buffer. Use as:
 *   static uint8_t heap[48*1024]; mcs_pool_t pool;
 *   mcs_pool_init(&pool, heap, sizeof heap);
 *   cfg.realloc_fn = mcs_pool_realloc; cfg.alloc_ud = &pool; */
typedef struct { uint8_t* base; size_t size; void* free_list; size_t used; size_t peak; } mcs_pool_t;
void mcs_pool_init(mcs_pool_t* pool, void* buf, size_t size);
void* mcs_pool_realloc(void* pool, void* ptr, size_t old_size, size_t new_size);
/* per-block header of the pool allocator (for cfg.alloc_overhead when
 * mcs_pool_realloc is wrapped by another function) */
#define MCS_POOL_OVERHEAD ((unsigned)sizeof(size_t) > (unsigned)MCS_POOL_ALIGN ? (unsigned)sizeof(size_t) : (unsigned)MCS_POOL_ALIGN)
#endif

#if MCS_ENABLE_DISASM && MCS_ENABLE_COMPILER
/* Print disassembly of compiled source to the output stream. */
mcs_result_t mcs_disassemble_source(mcs_vm_t* vm, const char* name, const char* src);
#endif
#if MCS_ENABLE_DISASM && MCS_ENABLE_BYTECODE_LOAD
/* Load (and validate) a bytecode image and print its disassembly. */
mcs_result_t mcs_disassemble_image(mcs_vm_t* vm, const uint8_t* image, size_t len);
#endif

/* ======================================================= Phase 2 additions
 * Everything below is additive; the Phase 1 API above is unchanged. */

/* Run source or a bytecode image, chosen by the "MCSB" magic. `data` need not be
 * NUL-terminated; source is copied when it is not. */
mcs_result_t mcs_exec_auto(mcs_vm_t* vm, const char* name, const void* data, size_t len);
bool mcs_is_image(const void* data, size_t len);

/* Execution budgets for one top-level run (exec_*, mcs_call*, mcs_invoke).
 * Checked at safepoints (backward jumps and calls), so the granularity is up to
 * MCS_HOOK_INTERVAL safepoints. time_ms needs cfg.ticks_fn. 0 = unlimited.
 * Exceeding a budget aborts the run (MCS_ERR_ABORTED); scripts cannot catch it. */
typedef struct {
    uint32_t time_ms;
    uint32_t steps;        /* safepoints: loop iterations + calls */
} mcs_limits_t;
void mcs_set_limits(mcs_vm_t* vm, const mcs_limits_t* limits);
typedef enum {
    MCS_ABORT_NONE = 0, MCS_ABORT_REQUEST, MCS_ABORT_HOOK, MCS_ABORT_TIME, MCS_ABORT_STEPS
} mcs_abort_reason_t;
mcs_abort_reason_t mcs_abort_reason(mcs_vm_t* vm);   /* of the last aborted run */
uint32_t mcs_steps_used(mcs_vm_t* vm);                /* by the current/last run  */

/* Record an error as the VM's last error, print it through error_fn and return
 * `code`. For hosts and modules that fail before or outside script execution. */
mcs_result_t mcs_fail(mcs_vm_t* vm, mcs_result_t code, const char* fmt, ...);

/* Compile-time features of this build (MCS_FEAT_* bits). */
#define MCS_FEAT_COMPILER    0x0001u
#define MCS_FEAT_IMAGE_LOAD  0x0002u
#define MCS_FEAT_IMAGE_SAVE  0x0004u
#define MCS_FEAT_FLOAT       0x0008u
#define MCS_FEAT_DOUBLE      0x0010u
#define MCS_FEAT_INT64       0x0020u
#define MCS_FEAT_LINES       0x0040u
#define MCS_FEAT_DISASM      0x0080u
#define MCS_FEAT_LIST        0x0100u
#define MCS_FEAT_DICT        0x0200u
#define MCS_FEAT_FS          0x1000u
#define MCS_FEAT_HAL         0x2000u
#define MCS_FEAT_SCHED       0x4000u
#define MCS_FEAT_SHELL       0x8000u
uint32_t mcs_features(void);

/* Per-VM extension slots. Optional modules (filesystem, HAL, scheduler, shell)
 * keep their context here so their natives can find it from `vm`. */
enum { MCS_EXT_VFS = 0, MCS_EXT_HAL, MCS_EXT_SCHED, MCS_EXT_SHELL,
       MCS_EXT_USER0, MCS_EXT_USER1, MCS_EXT__COUNT };
void mcs_set_ext(mcs_vm_t* vm, int slot, void* ptr);
void* mcs_get_ext(mcs_vm_t* vm, int slot);

/* ======================================================= 1.5 additions */
/* Add integer constants to module `name` (created like mcs_register_module).
 * With MCS_LAZY_CLASSES nothing is allocated until a script uses the module. */
void mcs_register_consts(mcs_vm_t* vm, const char* name, const mcs_const_t* consts);

/* ======================================================= 1.4 additions */
/* Declared arity of a callable (lambda, method, delegate); -1 if variadic or
 * not callable. Lets natives call user callbacks with the arguments they take. */
int mcs_arity(mcs_value_t callable);
/* Milliseconds from cfg.ticks_fn (or clock()). */
uint32_t mcs_ticks(mcs_vm_t* vm);
/* Sleep like Thread.Sleep: in slices of MCS_SLEEP_SLICE_MS when a hook, a time
 * limit or an idle handler is set, so long sleeps stay abortable and events are
 * dispatched while waiting. */
void mcs_sleep(mcs_vm_t* vm, uint32_t ms);
/* For natives that loop or wait for a long time: runs the abort / time-limit /
 * hook checks. Returns non-zero when the run must stop - return from the native
 * at once; the run then ends with MCS_ERR_ABORTED. */
int mcs_safepoint(mcs_vm_t* vm);
/* Idle handler called while a script sleeps (one per VM; the HAL module uses
 * it to run GPIO / timer callbacks during Thread.Sleep). */
typedef int (*mcs_idle_fn)(mcs_vm_t* vm, void* ud);
void mcs_set_idle(mcs_vm_t* vm, mcs_idle_fn fn, void* ud);
/* The current idle handler (to chain a second one in front of it). */
/* true while a script / callback is executing (e.g. inside Thread.Sleep) */
bool mcs_running(mcs_vm_t* vm);
/* Top-level variables are VM globals shared by every script. To run another script
 * nested inside a running one (a scheduler job during Thread.Sleep) without clobbering
 * the outer script's variables: h = mcs_globals_save(vm); run; mcs_globals_restore(vm, h). */
int mcs_globals_save(mcs_vm_t* vm);
void mcs_globals_restore(mcs_vm_t* vm, int handle);
mcs_idle_fn mcs_get_idle(mcs_vm_t* vm, void** ud);
/* Scratch memory for natives from the VM's own allocator (counted against
 * cfg.heap_limit; raises the out-of-memory error instead of returning NULL).
 * realloc semantics: (NULL, 0, n) allocates, (p, n, 0) frees. Not GC-managed:
 * free it before returning. */
void* mcs_mem_realloc(mcs_vm_t* vm, void* p, size_t old_size, size_t new_size);

#ifdef __cplusplus
}
#endif
#endif
