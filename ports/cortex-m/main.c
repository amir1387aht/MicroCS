/*
 * MicroCS Cortex-M reference port - firmware entry.
 *
 * 1. creates a VM on a static pool heap (no malloc)
 * 2. installs optional modules: RAM filesystem, HAL simulator, scheduler
 * 3. runs the demo as a precompiled bytecode image (always; executed in place from flash)
 * 4. full profile: compiles + runs the same demo from source on the device
 * 5. prints measurements (instructions from the emulator, heap/stack peaks)
 * 6. shell profile: serves the script-manager protocol on the UART until EOF
 *
 * Build: make -C ports/cortex-m     Run: python3 tools/cm_emu.py build/cm/<target>.elf
 */
#include <stdio.h>
#include <string.h>
#include "mcs.h"
#include "board.h"
#include "demo_image.h"
#ifndef CM_SHELL
#define CM_SHELL 0
#endif
#if MCS_ENABLE_COMPILER && !CM_SHELL
#include "demo_src.h"
#endif
#if MCS_ENABLE_FS
#include "mcs_vfs.h"
#endif
#if MCS_ENABLE_HAL
#include "mcs_hal.h"
#endif
#if MCS_ENABLE_SCHED
#include "mcs_sched.h"
#endif
#if MCS_ENABLE_SHELL && CM_SHELL
#include "mcs_shell.h"
#endif

#ifndef CM_HEAP_SIZE
#define CM_HEAP_SIZE (48 * 1024)
#endif
#ifndef CM_RAMFS_SIZE
#define CM_RAMFS_SIZE (4 * 1024)
#endif
#ifndef CM_STACK_SLOTS
#define CM_STACK_SLOTS 256
#endif
#ifndef CM_FRAMES
#define CM_FRAMES 48
#endif
/* run the demo image in place from flash (mcs_exec_image_xip); 0 = copy to heap */
#ifndef CM_XIP
#define CM_XIP 1
#endif
#ifndef CM_TARGET
#define CM_TARGET "cortex-m"
#endif

static uint8_t g_heap[CM_HEAP_SIZE] __attribute__((aligned(8)));
static mcs_pool_t g_pool;

static void w_out(void* ud, const char* s, size_t n) { (void)ud; board_write(s, (unsigned)n); }
static uint32_t w_ticks(void* ud) { (void)ud; return board_ticks_ms(); }
static void w_delay(void* ud, uint32_t ms) { (void)ud; uint32_t t = board_ticks_ms(); while (board_ticks_ms() - t < ms) {} }
static int w_hook(mcs_vm_t* vm, void* ud) {
    (void)ud;
#if MCS_ENABLE_SHELL && CM_SHELL
    return mcs_shell_poll_break(vm);
#else
    (void)vm; return 0;
#endif
}

#if MCS_ENABLE_FS
static mcs_vfs_t g_vfs;
static mcs_ramfs_t g_ramfs;
#endif
#if MCS_ENABLE_HAL
static mcs_hal_t g_hal;
static mcs_hal_sim_t g_sim;
#endif
#if MCS_ENABLE_SCHED
static mcs_sched_t g_sched;
#endif

#ifdef CM_TRACE_ALLOC
/* debug aid: log every allocation >= CM_TRACE_ALLOC bytes with its caller */
static void* trace_realloc(void* ud, void* p, size_t o, size_t n) {
    if (n >= CM_TRACE_ALLOC) printf("[alloc] %lu (was %lu) from %p\n", (unsigned long)n, (unsigned long)o, __builtin_return_address(0));
    return mcs_pool_realloc(ud, p, o, n);
}
#define CM_REALLOC trace_realloc
#else
#define CM_REALLOC mcs_pool_realloc
#endif

static mcs_vm_t* make_vm(void) {
    mcs_config_t cfg;
    mcs_config_default(&cfg);
    cfg.realloc_fn = CM_REALLOC; cfg.alloc_ud = &g_pool;
    cfg.write_fn = w_out; cfg.ticks_fn = w_ticks; cfg.delay_fn = w_delay; cfg.hook_fn = w_hook;
    cfg.stack_slots = CM_STACK_SLOTS; cfg.max_frames = CM_FRAMES;
    cfg.heap_limit = CM_HEAP_SIZE - 2048;
    mcs_vm_t* vm = mcs_new(&cfg);
    if (!vm) return NULL;
#if MCS_ENABLE_FS
    mcs_fs_open_lib(vm, &g_vfs);
#endif
#if MCS_ENABLE_HAL
    mcs_hal_open_lib(vm, &g_hal);
#endif
#if MCS_ENABLE_SCHED
    mcs_sched_init(&g_sched, vm,
#if MCS_ENABLE_FS
                   &g_vfs,
#else
                   NULL,
#endif
                   w_ticks, NULL);
    mcs_sched_open_lib(vm, &g_sched);
#endif
    return vm;
}

static void report(const char* what, uint32_t i0, mcs_result_t r) {
    uint32_t i1 = board_insns();
    printf("[%s] result=%d instructions=%lu pool_used=%lu\n", what, (int)r,
           (unsigned long)(i1 - i0), (unsigned long)g_pool.used);
}

int main(void) {
    printf("MicroCS %s on %s (features 0x%lx)\n", MCS_VERSION_STRING, CM_TARGET, (unsigned long)mcs_features());
    mcs_pool_init(&g_pool, g_heap, sizeof g_heap);
#if MCS_ENABLE_FS
    mcs_vfs_init(&g_vfs);
    mcs_ramfs_init(&g_ramfs, CM_RAMFS_SIZE, mcs_pool_realloc, &g_pool);
    mcs_vfs_mount(&g_vfs, "/", &mcs_ramfs_ops, &g_ramfs, 0);
#endif
#if MCS_ENABLE_HAL
    mcs_hal_sim_init(&g_hal, &g_sim);
#endif

    uint32_t i0 = board_insns();
    mcs_vm_t* vm = make_vm();
    if (!vm) { puts("mcs_new failed"); return 1; }
    printf("[vm] created: instructions=%lu pool_used=%lu\n", (unsigned long)(board_insns() - i0), (unsigned long)g_pool.used);

    EMU_MARK = 1;
    i0 = board_insns();
#if CM_XIP
    mcs_result_t r = mcs_exec_image_xip(vm, demo_image, demo_image_len);
#else
    mcs_result_t r = mcs_exec_image(vm, demo_image, demo_image_len);
#endif
    report("image", i0, r);
    mcs_mem_stats_t st; mcs_mem_stats(vm, &st);
    printf("[image] gc_peak=%lu collections=%lu\n", (unsigned long)st.peak_bytes, (unsigned long)st.collections);

#if MCS_ENABLE_COMPILER && !CM_SHELL   /* shell build: skip, keeps emulated boot fast */
    mcs_free(vm);
    vm = make_vm();
    EMU_MARK = 2;
    i0 = board_insns();
    r = mcs_exec_source(vm, "demo.cs", demo_src);
    report("source", i0, r);
    mcs_mem_stats(vm, &st);
    printf("[source] gc_peak=%lu collections=%lu\n", (unsigned long)st.peak_bytes, (unsigned long)st.collections);
#endif
    EMU_MARK = 3;
    printf("[mem] pool_peak=%lu of %lu, stack_peak=%lu\n", (unsigned long)g_pool.peak,
           (unsigned long)sizeof g_heap, (unsigned long)board_stack_high_water());

#if MCS_ENABLE_SHELL && CM_SHELL
    {
        static mcs_shell_t sh;
        extern mcs_transport_t cm_uart_transport(void);
        mcs_shell_init(&sh, vm, &g_vfs,
#if MCS_ENABLE_SCHED
                       &g_sched,
#else
                       NULL,
#endif
                       cm_uart_transport());
        mcs_shell_boot(&sh, true);
        mcs_shell_run(&sh);
        printf("[mem] after shell: pool_peak=%lu stack_peak=%lu\n", (unsigned long)g_pool.peak,
               (unsigned long)board_stack_high_water());
    }
#endif
    mcs_free(vm);
    printf("[done] pool_used_after_free=%lu\n", (unsigned long)g_pool.used);
    return 0;
}

#if MCS_ENABLE_SHELL && CM_SHELL
static int t_read(void* ud, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    (void)ud;
    size_t got = 0;
    int c = board_getc(timeout_ms);
    if (c == -2) return -1;
    if (c < 0) return 0;
    buf[got++] = (uint8_t)c;
    while (got < n && EMU_UART_RXST == 1) buf[got++] = (uint8_t)(EMU_UART_RX & 0xFF);
    return (int)got;
}
static void t_write(void* ud, const char* d, size_t n) { (void)ud; board_write(d, (unsigned)n); }
mcs_transport_t cm_uart_transport(void) { mcs_transport_t t = { t_read, t_write, NULL }; return t; }
#endif
