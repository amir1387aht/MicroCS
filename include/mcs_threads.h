/*
 * MicroCS - C# on real OS threads (optional, MCS_ENABLE_THREADS; needs an OS chosen
 * with MCS_OS: FreeRTOS, Zephyr or POSIX - see docs/THREADS.md). Without an OS this
 * header declares nothing and MicroCS is the single-threaded runtime it always was.
 *
 * Every thread runs its own VM with its own heap (a VM is never shared between
 * threads), so scripts on different cores really run in parallel. Threads share the
 * filesystem (every backend call is locked), the console (one line at a time), the
 * peripherals (no interrupt callbacks: those stay with the main VM) and named Channels
 * that copy values between VMs.
 *
 * C#:  var w = Thread.Start("/worker.cs");           // whole script on its own thread
 *      var w = Thread.Start("/worker.cs", 1);        // ... pinned to core 1
 *      var w = Thread.Run("/math.cs", "Fib", 30);    // call a function, any core
 *      var w = Thread.RunOn(1, "/math.cs", "Fib", 30);
 *      int r = (int)w.Result;                        // waits; w.Join([ms]), w.IsAlive, w.Stop()
 *      var p = Thread.Every(100, "/blink.cs", "Tick", 1); // OS-timed periodic task on core 1
 *      var ch = new Channel("samples", 16);  ch.Send(42);  var v = ch.Receive(500);
 *      Thread.Cores  Thread.CurrentCore  Thread.Os  Thread.Count  Thread.Id
 * jobs.cfg:  every 100ms /blink.cs thread core=1 stack=8k heap=24k prio=1
 */
#ifndef MCS_THREADS_H
#define MCS_THREADS_H
#include "mcs.h"
#include "mcs_os.h"
#ifdef __cplusplus
extern "C" {
#endif

#if MCS_ENABLE_THREADS
struct mcs_vfs;
struct mcs_hal;

typedef struct {
    struct mcs_vfs* vfs;            /* filesystem the scripts come from (shared, locked); may be NULL */
    const struct mcs_hal* hal;      /* peripherals for threads (no callbacks); NULL = none */
    mcs_write_fn write;             /* console for thread output, called with the console lock held
                                       (must not take mcs_threads_console_lock itself); NULL = stdout */
    void* write_ud;
    /* where thread heaps come from: NULL = mcs_os_malloc (the OS heap) */
    mcs_realloc_fn heap_fn;
    void* heap_ud;
    /* your own C# bindings, called for every new thread VM (like cfg.setup of the runtime) */
    void (*setup)(mcs_vm_t* vm, void* ud);
    void* ud;
    /* defaults for threads that do not choose (0 = MCS_THREAD_HEAP / _STACK / _SLOTS / _FRAMES) */
    uint32_t heap_size;
    uint32_t stack_size;
    uint32_t stack_slots;
    uint16_t max_frames;
    uint8_t stdlib;                 /* MCS_LIB_* of thread VMs, 0 = all */
} mcs_threads_cfg_t;

/* Once at start-up, before any thread is started. Returns 0, or -1 when the OS is
 * out of memory. Calling it again updates the configuration. */
int mcs_threads_init(const mcs_threads_cfg_t* cfg);
/* The C# API (Thread.Start/Run/RunOn/Every/Cores/..., Worker, Channel) in `vm`.
 * The runtime (mcs_runtime.h) does this for its VM. */
void mcs_threads_open_lib(mcs_vm_t* vm);
/* Ask every thread to stop, wait up to timeout_ms for them, free what they left.
 * Returns the number of threads still running (0 = all gone). */
int mcs_threads_shutdown(uint32_t timeout_ms);
/* Lock shared by everything that writes to the console (the runtime's own output). */
void mcs_threads_console_lock(void);
void mcs_threads_console_unlock(void);
/* Lock for heaps shared between threads (the runtime's pool); recursive use is not allowed. */
void mcs_threads_heap_lock(void);
void mcs_threads_heap_unlock(void);
/* true once a thread was started (shared resources need their locks from then on) */
bool mcs_threads_active(void);

/* ------------------------------------------------------------ threads from C */
typedef enum {
    MCS_THREAD_STARTING = 0, MCS_THREAD_RUNNING, MCS_THREAD_DONE, MCS_THREAD_FAILED, MCS_THREAD_STOPPED
} mcs_thread_state_t;

typedef struct {
    const char* path;               /* script on cfg.vfs, or ... */
    const void* code;               /* ... source / bytecode image in memory (must stay valid) */
    size_t code_len;
    const char* function;           /* NULL: run the script; else call this function after loading it */
    uint32_t delay_ms;              /* before the first run */
    uint32_t period_ms;             /* > 0: run again every period_ms (OS-timed, no drift) */
    uint16_t max_failures;          /* periodic: stop after n failed runs in a row (0 = never) */
    int core;                       /* MCS_OS_ANY_CORE or a core number */
    int priority;                   /* relative to the caller (mcs_os.h) */
    uint32_t heap_size, stack_size; /* 0 = mcs_threads_cfg_t defaults */
} mcs_thread_spec_t;
#define MCS_THREAD_SPEC_DEFAULTS { NULL, NULL, 0, NULL, 0, 0, 1, MCS_OS_ANY_CORE, MCS_THREAD_PRIORITY, 0, 0 }

typedef struct {
    int id;
    mcs_thread_state_t state;
    int core;                       /* requested core, MCS_OS_ANY_CORE = any */
    uint32_t runs, failures;
    uint32_t period_ms;
    const char* name;               /* path or "<code>" */
    const char* error;              /* last error text or "" */
} mcs_thread_info_t;

/* Start a thread. Returns its id (> 0) or a negative error: -1 table full
 * (MCS_THREADS_MAX), -2 out of memory, -3 no such core / pinning unsupported,
 * -4 bad spec, -5 mcs_threads_init not called. The caller owns one reference:
 * mcs_thread_release(id) when done with it (the thread keeps running). */
int mcs_thread_start(const mcs_thread_spec_t* spec);
void mcs_thread_stop(int id);                       /* request; returns at once */
bool mcs_thread_join(int id, uint32_t timeout_ms);  /* true once it has ended */
bool mcs_thread_info(int id, mcs_thread_info_t* out);
void mcs_thread_release(int id);
int mcs_thread_count(void);                         /* threads still running */
int mcs_thread_id_at(int index);                    /* ids for listings: 0 past the end */
const char* mcs_thread_strerror(int err);
#endif /* MCS_ENABLE_THREADS */

#ifdef __cplusplus
}
#endif
#endif
