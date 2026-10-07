/* Bare-metal C versions of the bench/mcu scripts, for the MicroCS comparison
 * (bench/compare/README.md). Same board, toolchain, flags and emulator as
 * ports/cortex-m/bench.c; prints the executed instructions of each workload
 * (incl. formatting + output, like the scripts). Inputs come from volatiles so
 * the compiler cannot fold the work away at build time.
 * Host:  cc -O2 -DHOST cbench.c -o cbench && ./cbench  (fib(30), 10 M loop, 1 M objects) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef HOST
#include "board.h"
#define OUT(s, n) board_write(s, n)
#else
#include <time.h>
#define OUT(s, n) fwrite(s, 1, n, stdout)
#endif

static volatile int v_fib = 18, v_loop = 50000, v_rounds = 4, v_n = 500, v_samples = 300, v_count = 200;
static char line[128];
static void out(void) { OUT(line, (unsigned)strlen(line)); }

static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
static void b_fib(void) { snprintf(line, sizeof line, "%d\n", fib(v_fib)); out(); }

static int sum(int n) {
    int s = 0;
    for (int i = 0; i < n; i++) { s += i % 7; if ((i & 3) == 0) s -= 1; }
    return s;
}
static void b_loop(void) { snprintf(line, sizeof line, "%d\n", sum(v_loop)); out(); }

/* objects: heap-allocated points in a growable array (what List<P> does) */
typedef struct { int x, y; } P;
static int len2(const P* p) { return p->x * p->x + p->y * p->y; }
static void b_objects(void) {
    int acc = 0, rounds = v_rounds, n = v_n;
    for (int r = 0; r < rounds; r++) {
        int cap = 4, cnt = 0;
        P** list = malloc(cap * sizeof *list);
        for (int i = 0; i < n; i++) {
            if (cnt == cap) { cap *= 2; list = realloc(list, cap * sizeof *list); }
            P* p = malloc(sizeof *p); p->x = i % 100; p->y = r;
            list[cnt++] = p;
        }
        for (int i = 0; i < cnt; i++) acc = (acc + len2(list[i])) % 1000003;
        for (int i = 0; i < cnt; i++) free(list[i]);
        free(list);
    }
    snprintf(line, sizeof line, "%d\n", acc); out();
}

static int seed;
static int next(void) { seed = (int)(((unsigned)seed * 1103515245u + 12345u) & 0x7fffffffu); return seed; }
static double avg(const double* a, int n) { double s = 0; for (int i = 0; i < n; i++) s += a[i]; return s / n; }
static void b_sensor(void) {
    double buf[64] = { 0 }, ema = 0; int alarms = 0, samples = v_samples;
    seed = 12345;
    for (int t = 0; t < samples; t++) {
        double v = 20.0 + (next() % 1000) / 100.0;
        buf[t % 64] = v; ema = ema * 0.9 + v * 0.1;
        if (v > 29.5) alarms++;
    }
    snprintf(line, sizeof line, "avg=%.2f ema=%.2f alarms=%d\n", avg(buf, 64), ema, alarms); out();
}

static void b_strings(void) {
    char s[1024]; int len = 0, count = v_count, total = 0;
    for (int i = 0; i < count; i++) len += snprintf(s + len, sizeof s - len, "%d,", i);
    for (char* p = s; *p; ) {           /* split on ',' and parse */
        char* e = strchr(p, ',');
        if (e != p) total += atoi(p);
        if (!e) break;
        p = e + 1;
    }
    snprintf(line, sizeof line, "%d %d\n", len, total); out();
}

#ifndef HOST
typedef struct { const char* name; void (*fn)(void); } cb_t;
static const cb_t benches[] = { { "fib", b_fib }, { "loop", b_loop }, { "objects", b_objects }, { "sensor", b_sensor }, { "strings", b_strings } };
int main(void) {
    printf("bare-metal C bench (%s)\n", CM_TARGET);
    for (unsigned i = 0; i < sizeof benches / sizeof benches[0]; i++) {
        uint32_t i0 = board_insns();
        benches[i].fn();
        uint32_t n = board_insns() - i0;
        printf("[bench] %-8s c %lu\n", benches[i].name, (unsigned long)n);
    }
    board_exit(0);
}
#else
static double ms(struct timespec a, struct timespec b) { return (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6; }
#define TIME(label, stmt) do { double best = 1e9; for (int k = 0; k < 5; k++) { struct timespec a, b; \
    clock_gettime(CLOCK_MONOTONIC, &a); stmt; clock_gettime(CLOCK_MONOTONIC, &b); if (ms(a, b) < best) best = ms(a, b); } \
    printf("%s: %.3f ms\n", label, best); } while (0)
int main(void) {
    b_fib(); b_loop(); b_objects(); b_sensor(); b_strings();      /* same output as the scripts */
    TIME("fib(30)", (v_fib = 30, b_fib()));
    TIME("loop 10M", ({ volatile int n = 10000000; int s = 0; for (int i = 0; i < n; i++) s += i % 7; snprintf(line, sizeof line, "%d\n", s); out(); }));
    TIME("objects 1M", (v_rounds = 100, v_n = 10000, b_objects()));
    v_fib = 18; v_rounds = 4; v_n = 500;
    TIME("mcu fib", b_fib()); TIME("mcu loop", b_loop()); TIME("mcu objects", b_objects());
    TIME("mcu sensor", b_sensor()); TIME("mcu strings", b_strings());
    return 0;
}
#endif
