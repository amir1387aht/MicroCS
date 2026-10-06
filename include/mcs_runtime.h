/*
 * MicroCS - complete firmware runtime in one call (optional, MCS_ENABLE_RUNTIME).
 *
 * Gives you a MicroPython-style device: a C# REPL on the console UART, a
 * filesystem with boot.cs / main.cs, every peripheral of your board, the job
 * scheduler, interrupt callbacks and the script-upload protocol for
 * tools/mcs_remote.py. Your firmware only provides the board glue:
 *
 *     static uint8_t heap[96 * 1024];
 *     static mcs_runtime_t rt;
 *     mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
 *     cfg.heap = heap; cfg.heap_size = sizeof heap;
 *     cfg.console = my_uart_transport;    // read/write callbacks
 *     cfg.ticks = my_millis; cfg.delay = my_delay_ms;
 *     cfg.hal = &my_board;                // from ports/<vendor>/ or your own
 *     mcs_runtime_run(&rt, &cfg);         // never returns while the console is open
 *
 * Or keep your own main loop / RTOS task: mcs_runtime_start() once, then
 * mcs_runtime_step() as often as you like (it never blocks longer than asked).
 */
#ifndef MCS_RUNTIME_H
#define MCS_RUNTIME_H
#include "mcs.h"
#include "mcs_vfs.h"
#include "mcs_hal.h"
#include "mcs_sched.h"
#include "mcs_shell.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MCS_RUNTIME_REPL = 0,       /* interactive C# prompt (humans on a terminal) */
    MCS_RUNTIME_SHELL = 1,      /* machine protocol (mcs_remote.py, IDE plug-ins) */
    MCS_RUNTIME_HEADLESS = 2    /* no console input: run the app + jobs + callbacks only */
} mcs_runtime_mode_t;

typedef struct {
    /* memory: one static buffer for the VM heap (and the RAM filesystem) */
    void* heap;
    size_t heap_size;
    /* filesystem: your backend (e.g. &mcs_lfs_ops + a mounted lfs_t) or a RAM
     * filesystem of ramfs_size bytes carved from the heap; neither = no files */
    const mcs_vfs_ops_t* fs_ops;
    void* fs_ctx;
    size_t ramfs_size;
    /* console + time */
    mcs_transport_t console;
    mcs_ticks_fn ticks;
    mcs_delay_fn delay;
    void* time_ud;
    bool echo;                  /* echo input (raw UART terminals); false for line-buffered hosts */
    mcs_runtime_mode_t mode;
    /* board */
    const mcs_hal_t* hal;       /* NULL = no peripherals */
    /* application: optional built-in image (from `mcs -C app.cs`), run in place */
    const uint8_t* app_image;
    size_t app_image_len;
    bool run_boot_scripts;      /* /boot.cs, /jobs.cfg, /main.cs from the filesystem */
    /* your own C# bindings: called once after the VM and modules are ready */
    void (*setup)(mcs_vm_t* vm, void* ud);
    /* called every loop iteration: feed a watchdog, yield to the RTOS ... */
    void (*idle)(void* ud);
    void* ud;
    /* tuning (0 = default) */
    uint32_t stack_slots;
    uint16_t max_frames;
    uint8_t stdlib;             /* MCS_LIB_* mask, 0 = MCS_LIB_ALL */
    uint32_t time_limit_ms;     /* per top-level run (REPL line, job, callback); 0 = none */
} mcs_runtime_cfg_t;

#define MCS_RUNTIME_DEFAULTS { NULL, 0, NULL, NULL, 0, { NULL, NULL, NULL }, NULL, NULL, NULL, \
    true, MCS_RUNTIME_REPL, NULL, NULL, 0, true, NULL, NULL, NULL, 0, 0, 0, 0 }

typedef struct {
    mcs_runtime_cfg_t cfg;
    mcs_vm_t* vm;
#if MCS_ENABLE_POOL_HEAP
    mcs_pool_t pool;
#endif
#if MCS_ENABLE_FS
    mcs_vfs_t vfs;
    mcs_ramfs_t ramfs;
#endif
#if MCS_ENABLE_SCHED
    mcs_sched_t sched;
#endif
    mcs_shell_t shell;
    bool running;
} mcs_runtime_t;

/* Build everything and run the boot sequence. Returns 0 or a negative error. */
int mcs_runtime_start(mcs_runtime_t* rt, const mcs_runtime_cfg_t* cfg);
/* Serve the console, run due jobs and interrupt callbacks; waits at most
 * timeout_ms for input. Returns false once the console has closed. */
bool mcs_runtime_step(mcs_runtime_t* rt, uint32_t timeout_ms);
/* start + step forever (until the console closes) + stop. */
int mcs_runtime_run(mcs_runtime_t* rt, const mcs_runtime_cfg_t* cfg);
void mcs_runtime_stop(mcs_runtime_t* rt);

#ifdef __cplusplus
}
#endif
#endif
