/*
 * MicroCS Cortex-M benchmark firmware: runs every bench/mcu/ script as an
 * optimized image (default `mcs -c`), as an unoptimized image (`mcs -c -O0`)
 * and - when the compiler is built in - from source, each on a fresh VM, and
 * prints the executed instruction count of each run (load/compile + run).
 * Build + run: tools/mcu_bench.sh
 */
#include <stdio.h>
#include <string.h>
#include "mcs.h"
#include "board.h"
#include "bench_scripts.h"

#ifndef CM_HEAP_SIZE
#define CM_HEAP_SIZE (96 * 1024)
#endif
static uint8_t g_heap[CM_HEAP_SIZE] __attribute__((aligned(8)));
static mcs_pool_t g_pool;
static void w_out(void* ud, const char* s, size_t n) { (void)ud; board_write(s, (unsigned)n); }
static uint32_t w_ticks(void* ud) { (void)ud; return board_ticks_ms(); }

static mcs_vm_t* make_vm(void) {
    mcs_pool_init(&g_pool, g_heap, sizeof g_heap);
    mcs_config_t cfg;
    mcs_config_default(&cfg);
    cfg.realloc_fn = mcs_pool_realloc; cfg.alloc_ud = &g_pool; cfg.alloc_overhead = MCS_POOL_OVERHEAD;
    cfg.write_fn = w_out; cfg.ticks_fn = w_ticks;
    cfg.heap_limit = CM_HEAP_SIZE - 2048;
    return mcs_new(&cfg);
}

enum { M_OPT, M_O0, M_SRC };
#ifndef BENCH_MODES
#define BENCH_MODES 7   /* bit mask of the modes to run (1 = image, 2 = image -O0, 4 = source) */
#endif
static uint32_t run(const bench_t* b, int mode, int* rc, unsigned long* peak) {
    mcs_vm_t* vm = make_vm();
    if (!vm) { *rc = -1; return 0; }
    uint32_t i0 = board_insns();
    mcs_result_t r = MCS_ERR_RUNTIME;
    if (mode == M_OPT) r = mcs_exec_image_xip(vm, b->opt, b->opt_len);
    else if (mode == M_O0) r = mcs_exec_image_xip(vm, b->o0, b->o0_len);
#if MCS_ENABLE_COMPILER
    else r = mcs_exec_source(vm, b->name, b->src);
#endif
    uint32_t n = board_insns() - i0;
    mcs_mem_stats_t st; mcs_mem_stats(vm, &st);
    *peak = (unsigned long)st.peak_bytes;
    mcs_free(vm);
    *rc = (int)r;
    return n;
}

int main(void) {
    printf("MicroCS %s bench (%s)\n", MCS_VERSION_STRING, CM_TARGET);
    for (unsigned i = 0; i < sizeof benches / sizeof benches[0]; i++) {
        const bench_t* b = &benches[i];
        int rc[3] = { 0, 0, 0 }; unsigned long pk[3] = { 0, 0, 0 }; uint32_t n[3] = { 0, 0, 0 };
        if (BENCH_MODES & 1) n[M_OPT] = run(b, M_OPT, &rc[M_OPT], &pk[M_OPT]);
        if (BENCH_MODES & 2) n[M_O0] = run(b, M_O0, &rc[M_O0], &pk[M_O0]);
#if MCS_ENABLE_COMPILER
        if (BENCH_MODES & 4) n[M_SRC] = run(b, M_SRC, &rc[M_SRC], &pk[M_SRC]);
#endif
        printf("[bench] %-8s src=%u B image=%u B (-O0 %u B) | image %lu | image -O0 %lu | source %lu | rc %d %d %d | peak %lu %lu %lu\n",
               b->name, (unsigned)strlen(b->src), b->opt_len, b->o0_len,
               (unsigned long)n[M_OPT], (unsigned long)n[M_O0], (unsigned long)n[M_SRC], rc[0], rc[1], rc[2], pk[0], pk[1], pk[2]);
    }
    board_exit(0);
}
