/* MicroCS - complete firmware runtime: VM + filesystem + HAL + scheduler + REPL/shell. */
#include "mcs_runtime.h"
#if MCS_ENABLE_RUNTIME
#include <string.h>

/* Every callback gets the runtime itself as user data, so the board glue keeps
 * its own ud pointers (console.ud, time_ud, ud). */
static bool rt_has_input(const mcs_runtime_t* rt) {
    return rt->cfg.console.read && rt->cfg.mode != MCS_RUNTIME_HEADLESS;
}
static void rt_write(void* ud, const char* s, size_t n) {
    mcs_runtime_t* rt = (mcs_runtime_t*)ud;
    if (rt->cfg.console.write) rt->cfg.console.write(rt->cfg.console.ud, s, n);
}
static uint32_t rt_ticks(void* ud) {
    mcs_runtime_t* rt = (mcs_runtime_t*)ud;
    return rt->cfg.ticks ? rt->cfg.ticks(rt->cfg.time_ud) : 0;
}
static void rt_delay(void* ud, uint32_t ms) {
    mcs_runtime_t* rt = (mcs_runtime_t*)ud;
    if (rt->cfg.delay) rt->cfg.delay(rt->cfg.time_ud, ms);
}
static int rt_read(void* ud, uint8_t* b, size_t n, uint32_t timeout_ms) {
    mcs_runtime_t* rt = (mcs_runtime_t*)ud;
    if (rt_has_input(rt)) return rt->cfg.console.read(rt->cfg.console.ud, b, n, timeout_ms);
    if (timeout_ms) rt_delay(rt, timeout_ms);      /* headless: just pass the time */
    return 0;
}
static int rt_hook(mcs_vm_t* vm, void* ud) {
    mcs_runtime_t* rt = (mcs_runtime_t*)ud;
    if (rt->cfg.idle) rt->cfg.idle(rt->cfg.ud);
    return rt_has_input(rt) ? mcs_shell_poll_break(vm) : 0;
}

int mcs_runtime_start(mcs_runtime_t* rt, const mcs_runtime_cfg_t* cfg) {
    memset(rt, 0, sizeof *rt);
    rt->cfg = *cfg;
    mcs_config_t c;
    mcs_config_default(&c);
#if MCS_ENABLE_POOL_HEAP
    if (cfg->heap && cfg->heap_size) {
        mcs_pool_init(&rt->pool, cfg->heap, cfg->heap_size);
        c.realloc_fn = mcs_pool_realloc;
        c.alloc_ud = &rt->pool;
        size_t reserve = cfg->ramfs_size + 2048;   /* RAM fs + fragmentation slack */
        c.heap_limit = cfg->heap_size > reserve + 8192 ? cfg->heap_size - reserve : cfg->heap_size;
    }
#endif
    c.write_fn = rt_write;
    c.ticks_fn = rt_ticks;
    c.delay_fn = rt_delay;
    c.hook_fn = rt_hook;
    c.user_data = rt;
    if (cfg->stack_slots) c.stack_slots = cfg->stack_slots;
    if (cfg->max_frames) c.max_frames = cfg->max_frames;
    if (cfg->stdlib) c.stdlib = cfg->stdlib;
    rt->vm = mcs_new(&c);
    if (!rt->vm) return -1;
    mcs_vm_t* vm = rt->vm;
    if (cfg->time_limit_ms) { mcs_limits_t l; memset(&l, 0, sizeof l); l.time_ms = cfg->time_limit_ms; mcs_set_limits(vm, &l); }

    struct mcs_vfs* vfs = NULL;
#if MCS_ENABLE_FS
    mcs_vfs_init(&rt->vfs);
    int e = 1;
    if (cfg->fs_ops) e = mcs_vfs_mount(&rt->vfs, "/", cfg->fs_ops, cfg->fs_ctx, 0);
    else if (cfg->ramfs_size) {
#if MCS_ENABLE_POOL_HEAP
        if (cfg->heap && cfg->heap_size) mcs_ramfs_init(&rt->ramfs, cfg->ramfs_size, mcs_pool_realloc, &rt->pool);
        else
#endif
        mcs_ramfs_init(&rt->ramfs, cfg->ramfs_size, NULL, NULL);
        e = mcs_vfs_mount(&rt->vfs, "/", &mcs_ramfs_ops, &rt->ramfs, 0);
    }
    if (e == 0) { vfs = &rt->vfs; mcs_fs_open_lib(vm, vfs); }
#endif
#if MCS_ENABLE_HAL
    if (cfg->hal) mcs_hal_open_lib(vm, cfg->hal);
#endif
#if MCS_ENABLE_SCHED
    mcs_sched_init(&rt->sched, vm, vfs, rt_ticks, rt);
    mcs_sched_open_lib(vm, &rt->sched);
#endif
    if (cfg->setup) cfg->setup(vm, cfg->ud);

    mcs_transport_t t;
    t.read = rt_read;
    t.write = rt_write;
    t.ud = rt;
    mcs_shell_init(&rt->shell, vm, vfs,
#if MCS_ENABLE_SCHED
                   &rt->sched,
#else
                   NULL,
#endif
                   t);
    rt->shell.echo = cfg->echo;
    rt->shell.repl = cfg->mode == MCS_RUNTIME_REPL;
#if MCS_ENABLE_XIP
    if (cfg->app_image && cfg->app_image_len) mcs_exec_image_xip(vm, cfg->app_image, cfg->app_image_len);
#endif
    mcs_shell_boot(&rt->shell, cfg->run_boot_scripts);
    rt->running = true;
    return 0;
}

bool mcs_runtime_step(mcs_runtime_t* rt, uint32_t timeout_ms) {
    if (!rt->running) return false;
    if (rt->cfg.idle) rt->cfg.idle(rt->cfg.ud);
#if MCS_ENABLE_HAL
    if (rt->cfg.hal && timeout_ms > MCS_SLEEP_SLICE_MS) timeout_ms = MCS_SLEEP_SLICE_MS;
#endif
#if MCS_ENABLE_SCHED
    int32_t next = mcs_sched_poll(&rt->sched);
    if (next >= 0 && (uint32_t)next < timeout_ms) timeout_ms = (uint32_t)next;
#endif
    /* the shell step polls the scheduler and the HAL event queue itself */
    if (!mcs_shell_step(&rt->shell, timeout_ms)) rt->running = false;
    return rt->running;
}

void mcs_runtime_stop(mcs_runtime_t* rt) {
    if (!rt->vm) return;
#if MCS_ENABLE_SCHED
    mcs_sched_free(&rt->sched);
#endif
#if MCS_ENABLE_HAL
    if (rt->cfg.hal) mcs_hal_close_lib(rt->vm);
#endif
    mcs_free(rt->vm);
    rt->vm = NULL;
#if MCS_ENABLE_FS
    if (!rt->cfg.fs_ops && rt->cfg.ramfs_size) mcs_ramfs_free(&rt->ramfs);
#endif
    rt->running = false;
}

int mcs_runtime_run(mcs_runtime_t* rt, const mcs_runtime_cfg_t* cfg) {
    int r = mcs_runtime_start(rt, cfg);
    if (r) return r;
    while (mcs_runtime_step(rt, 100)) {}
    mcs_runtime_stop(rt);
    return 0;
}
#else
typedef int mcs_runtime_unused_t;   /* keep ISO C happy with an empty unit */
#endif
