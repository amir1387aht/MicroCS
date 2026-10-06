/*
 * MicroCS - firmware skeleton for a small MCU (~64 KB RAM).
 *
 * Build MicroCS with the low-RAM profile:
 *     -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_lowram.h"'
 * (no on-device compiler, single-precision floats, small VM limits) and link
 * a script precompiled on the host:
 *     mcs -C node.cs -n node_image -o node_image.h
 *
 * What it shows:
 *   - one static pool as the only heap: the RAM budget is fixed at link time
 *   - the image is executed in place (mcs_exec_image_xip): bytecode stays in
 *     flash, only object data lives in RAM
 *   - only the stdlib parts the script needs are opened (cfg.stdlib)
 *   - a per-call step budget so a buggy script cannot hang the main loop
 *   - reading heap statistics to check the headroom while developing
 *
 * `make example-lowram` builds and runs it on the host. On a 32-bit MCU the
 * same code needs about half the heap of a 64-bit host (8-byte values,
 * 4-byte pointers), so the default pool size below depends on the pointer size.
 */
#include <stdio.h>
#include <string.h>
#include "mcs.h"
#include "node_image.h"

#ifndef NODE_HEAP_SIZE
#if UINTPTR_MAX > 0xFFFFFFFFu
#define NODE_HEAP_SIZE (72 * 1024)   /* 64-bit host: 16-byte values, 8-byte pointers */
#else
#define NODE_HEAP_SIZE (32 * 1024)   /* Cortex-M class target */
#endif
#endif

static uint8_t g_heap[NODE_HEAP_SIZE];   /* in a real port: a dedicated .bss section */
static mcs_pool_t g_pool;

static void out_write(void* ud, const char* s, size_t n) { (void)ud; fwrite(s, 1, n, stdout); }
static uint32_t g_now;
static uint32_t ticks(void* ud) { (void)ud; return g_now; }   /* replace with your SysTick counter */

static int check(mcs_vm_t* vm, mcs_result_t r, const char* what) {
    if (r == MCS_OK) return 1;
    printf("[c] %s failed (%d): %s\n", what, (int)r, mcs_last_error(vm));
    return 0;
}

int main(void) {
    mcs_pool_init(&g_pool, g_heap, sizeof g_heap);

    mcs_config_t cfg;
    mcs_config_default(&cfg);
    cfg.realloc_fn = mcs_pool_realloc;
    cfg.alloc_ud = &g_pool;
    cfg.heap_limit = sizeof g_heap - 1024;   /* GC works to stay below this; leave pool slack */
    cfg.write_fn = out_write;
    cfg.ticks_fn = ticks;
    /* the script uses Console, exceptions, strings and arrays only */
    cfg.stdlib = MCS_LIB_CORE | MCS_LIB_COLLECTIONS;

    mcs_vm_t* vm = mcs_new(&cfg);
    if (!vm) { printf("[c] not enough RAM for the VM\n"); return 1; }
    mcs_mem_stats_t st;
    mcs_mem_stats(vm, &st);
    printf("[c] VM ready: %lu B of heap in use\n", (unsigned long)st.bytes_in_use);   /* newlib-nano has no %zu */

    /* bytecode is read from node_image[] (flash) for the whole lifetime of the VM */
    if (!check(vm, mcs_exec_image_xip(vm, node_image, sizeof node_image), "load image")) return 1;

    mcs_limits_t lim = { .steps = 200000 };   /* per call: stops runaway loops */
    mcs_set_limits(vm, &lim);

    for (int i = 0; i < 24; i++) {            /* the firmware main loop */
        g_now += 250;
        mcs_value_t line;
        if (!check(vm, mcs_call(vm, "Node.Tick", 0, NULL, &line), "Node.Tick")) break;
        const char* s = mcs_cstr(line);       /* valid until the next call into the VM */
        printf("[node] %s\n", s ? s : "?");
    }
    mcs_value_t sum;
    if (check(vm, mcs_call(vm, "Node.Summary", 0, NULL, &sum), "Node.Summary"))
        printf("[node] %s\n", mcs_cstr(sum));

    mcs_gc(vm);
    mcs_mem_stats(vm, &st);
    printf("[c] heap: %lu B in use, peak %lu B, %u collections; pool peak %lu of %lu B\n",
           (unsigned long)st.bytes_in_use, (unsigned long)st.peak_bytes, (unsigned)st.collections,
           (unsigned long)g_pool.peak, (unsigned long)sizeof g_heap);
    mcs_free(vm);
    return 0;
}
