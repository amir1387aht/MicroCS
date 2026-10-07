/* MicroPython counterpart of MicroCS ports/cortex-m/bench.c: each bench/mcu
 * script runs on a fresh interpreter from precompiled .mpy (mpy-cross) and
 * from source (on-device compiler); prints executed instruction counts. */
#include <stdio.h>
#include <string.h>
#include "port/micropython_embed.h"
#include "py/mphal.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "board.h"
#include "bench_py.h"

#ifndef HEAP_SIZE
#define HEAP_SIZE (96 * 1024)
#endif
static char heap[HEAP_SIZE] __attribute__((aligned(8)));
static char cap[256]; static unsigned cap_n; static int capture;
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    if (capture) { for (size_t i = 0; i < len && cap_n < sizeof cap - 1; i++) cap[cap_n++] = str[i]; return; }
    board_write(str, (unsigned)len);
}
static uint32_t run(const pyb_t* b, int from_src, size_t heap_size) {
    int stack_top;
    uint32_t i0 = board_insns();
    mp_embed_init(heap, heap_size, &stack_top);
    mp_stack_set_limit(6 * 1024); /* of the 8 KB C stack */
    if (from_src) mp_embed_exec_str(b->src); else mp_embed_exec_mpy(b->mpy, b->mpy_len);
    mp_embed_deinit();
    return board_insns() - i0;
}
int main(void) {
    printf("MicroPython 1.26.0 bench (%s)\n", CM_TARGET);
    for (unsigned i = 0; i < sizeof benches / sizeof benches[0]; i++) {
        const pyb_t* b = &benches[i];
        uint32_t nm = run(b, 0, HEAP_SIZE);
        uint32_t ns = run(b, 1, HEAP_SIZE);
        /* expected output, then binary-search the smallest GC heap that still runs it (.mpy) */
        capture = 1; cap_n = 0; run(b, 0, HEAP_SIZE); cap[cap_n] = 0;
        char want[256]; strcpy(want, cap);
        size_t lo = 256, hi = HEAP_SIZE;
        while (hi - lo > 64) {
            size_t mid = (lo + hi) / 2 & ~(size_t)15;
            cap_n = 0; run(b, 0, mid); cap[cap_n] = 0;
            if (strcmp(cap, want) == 0) hi = mid; else lo = mid;
        }
        capture = 0;
        printf("[bench] %-8s mpy=%u B | mpy %lu | source %lu | min heap %u\n", b->name, b->mpy_len,
               (unsigned long)nm, (unsigned long)ns, (unsigned)hi);
    }
    board_exit(0);
}

/* no filesystem: stubs for the import / execfile hooks */
#include "py/lexer.h"
#include "py/builtin.h"
#include "py/runtime.h"
mp_lexer_t *mp_lexer_new_from_file(qstr filename) { mp_raise_OSError(2); }
mp_import_stat_t mp_import_stat(const char *path) { return MP_IMPORT_STAT_NO_EXIST; }
void mp_hal_set_interrupt_char(int c) { }
