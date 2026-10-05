/*
 * MicroCS host benchmark: separates VM startup, source compilation, image
 * loading and execution.  Usage: build/bench_host script.cs [iterations]
 * (built by `make bench`; numbers are host-CPU wall-clock, best of N runs)
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mcs.h"

static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static void quiet(void* ud, const char* s, size_t n) { (void)ud; (void)s; (void)n; }
static char* slurp(const char* p, size_t* n) {
    FILE* f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    char* b = (char*)malloc((size_t)l + 1); *n = fread(b, 1, (size_t)l, f); b[*n] = 0; fclose(f); return b;
}
static mcs_vm_t* new_vm(void) {
    mcs_config_t cfg; mcs_config_default(&cfg); cfg.write_fn = quiet; return mcs_new(&cfg);
}
#define BEST(var, expr) do { double _b = 1e30; for (int _i = 0; _i < iters; _i++) { double _t = now_us(); expr; _t = now_us() - _t; if (_t < _b) _b = _t; } var = _b; } while (0)

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s script.cs [iterations]\n", argv[0]); return 64; }
    int iters = argc > 2 ? atoi(argv[2]) : 5;
    size_t n; char* src = slurp(argv[1], &n);
    double t_new, t_compile, t_src, t_img;
    BEST(t_new, { mcs_vm_t* vm = new_vm(); mcs_free(vm); });
    /* compile to an in-memory image (compile only, nothing executes) */
    uint8_t* img = NULL; size_t img_len = 0;
    BEST(t_compile, { mcs_vm_t* vm = new_vm(); uint8_t* o; size_t ol;
        if (mcs_compile_image(vm, argv[1], src, false, &o, &ol) != MCS_OK) { fprintf(stderr, "compile failed\n"); return 1; }
        free(img); img = (uint8_t*)malloc(ol); memcpy(img, o, ol); img_len = ol; mcs_free_image(vm, o); mcs_free(vm); });
    BEST(t_src, { mcs_vm_t* vm = new_vm(); mcs_exec_source(vm, argv[1], src); mcs_free(vm); });
    BEST(t_img, { mcs_vm_t* vm = new_vm(); mcs_exec_image(vm, img, img_len); mcs_free(vm); });
    mcs_vm_t* vm = new_vm(); mcs_exec_image(vm, img, img_len);
    mcs_mem_stats_t st; mcs_mem_stats(vm, &st); mcs_free(vm);
    printf("%-22s src=%5zu B image=%5zu B | new %7.1f us | compile %8.1f us | src run %9.1f us | image run %9.1f us | gc peak %zu B\n",
           argv[1], n, img_len, t_new, t_compile - t_new, t_src - t_new, t_img - t_new, st.peak_bytes);
    free(img); free(src);
    return 0;
}
