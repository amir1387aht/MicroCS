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
 * The Cortex-M builds (ports/cortex-m, tools/cm_check.sh) run it on a 48 KB
 * and on a 16 KB RAM part (NODE_HEAP_SIZE=12288), and in 64 KB of flash with
 * profiles/mcs_profile_min.h.
 *
 * No printf: the firmware prints through NODE_PUTS (a "write a C string"
 * function, default fputs to stdout), so a build without the C library's
 * formatted I/O stays small.
 */
#include <string.h>
#include "mcs.h"
#include "node_image.h"

#ifndef NODE_PUTS
#include <stdio.h>
static void node_puts(const char* s) { fputs(s, stdout); }
#define NODE_PUTS node_puts
#else
void NODE_PUTS(const char* s);
#endif
static void say(const char* s) { NODE_PUTS(s); }
static void say_u(unsigned long v) {      /* decimal, without printf */
    char b[12]; int i = (int)sizeof b - 1; b[i] = 0;
    do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v);
    NODE_PUTS(b + i);
}

#ifndef NODE_HEAP_SIZE
#if UINTPTR_MAX > 0xFFFFFFFFu
#define NODE_HEAP_SIZE (72 * 1024)   /* 64-bit host: 16-byte values, 8-byte pointers */
#else
#define NODE_HEAP_SIZE (32 * 1024)   /* Cortex-M class target */
#endif
#endif

static uint8_t g_heap[NODE_HEAP_SIZE];   /* in a real port: a dedicated .bss section */
static mcs_pool_t g_pool;

static void out_write(void* ud, const char* s, size_t n) {   /* Console output of the script */
    (void)ud; char b[64];
    while (n) { size_t k = n < sizeof b - 1 ? n : sizeof b - 1; memcpy(b, s, k); b[k] = 0; NODE_PUTS(b); s += k; n -= k; }
}
static uint32_t g_now;
static uint32_t ticks(void* ud) { (void)ud; return g_now; }   /* replace with your SysTick counter */

static int check(mcs_vm_t* vm, mcs_result_t r, const char* what) {
    if (r == MCS_OK) return 1;
    say("[c] "); say(what); say(" failed ("); say_u((unsigned long)r); say("): "); say(mcs_last_error(vm)); say("\n");
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
    if (!vm) { say("[c] not enough RAM for the VM\n"); return 1; }
    mcs_mem_stats_t st;
    mcs_mem_stats(vm, &st);
    say("[c] VM ready: "); say_u(st.bytes_in_use); say(" B of heap in use\n");

    /* bytecode is read from node_image[] (flash) for the whole lifetime of the VM */
    if (!check(vm, mcs_exec_image_xip(vm, node_image, sizeof node_image), "load image")) return 1;

    mcs_limits_t lim = { .steps = 200000 };   /* per call: stops runaway loops */
    mcs_set_limits(vm, &lim);

    for (int i = 0; i < 24; i++) {            /* the firmware main loop */
        g_now += 250;
        mcs_value_t line;
        if (!check(vm, mcs_call(vm, "Node.Tick", 0, NULL, &line), "Node.Tick")) break;
        const char* s = mcs_cstr(line);       /* valid until the next call into the VM */
        say("[node] "); say(s ? s : "?"); say("\n");
    }
    mcs_value_t sum;
    if (check(vm, mcs_call(vm, "Node.Summary", 0, NULL, &sum), "Node.Summary"))
        { say("[node] "); say(mcs_cstr(sum)); say("\n"); }

    mcs_gc(vm);
    mcs_mem_stats(vm, &st);
    say("[c] heap: "); say_u(st.bytes_in_use); say(" B in use, peak "); say_u(st.peak_bytes);
    say(" B, "); say_u(st.collections); say(" collections; pool peak "); say_u(g_pool.peak);
    say(" of "); say_u(sizeof g_heap); say(" B\n");
#ifdef NODE_STACK_HIGH_WATER
    {   /* the board's C-stack high-water mark (painted stack), when it has one */
        extern uint32_t NODE_STACK_HIGH_WATER(void);
        say("[c] C stack peak "); say_u(NODE_STACK_HIGH_WATER()); say(" B\n");
    }
#endif
    mcs_free(vm);
    return 0;
}
