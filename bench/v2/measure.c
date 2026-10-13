/* Reproducible host-only measurements. Every execution is result/output checked.
 * Timing excludes VM creation except vm_init; no subtraction of independent runs.
 * Memory is requested allocator bytes (including VM), not RSS/pool overhead. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mcs.h"
#include "mcs_shell.h"

typedef struct {
    size_t used, peak, allocations;
    const char* expected;
    size_t expected_len, seen;
    int mismatch, error;
} sample_t;

static void* tracked_realloc(void* ud, void* ptr, size_t old_size, size_t new_size) {
    sample_t* s = (sample_t*)ud;
    if (old_size > s->used) { fputs("allocator accounting underflow\n", stderr); exit(1); }
    if (!new_size) { free(ptr); s->used -= old_size; return NULL; }
    void* p = realloc(ptr, new_size);
    if (!p) return NULL;
    s->used = s->used - old_size + new_size;
    if (s->used > s->peak) s->peak = s->used;
    s->allocations++;
    return p;
}

static void output(void* ud, const char* text, size_t n) {
    sample_t* s = (sample_t*)ud;
    if (s->seen > s->expected_len || n > s->expected_len - s->seen ||
        memcmp(s->expected + s->seen, text, n)) s->mismatch = 1;
    s->seen += n;
}

static void error_output(void* ud, const char* text, size_t n) {
    sample_t* s = (sample_t*)ud;
    s->error = 1;
    fwrite(text, 1, n, stderr);
}

static double now_us(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) { perror("clock_gettime"); exit(1); }
    return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
}

static char* read_file(const char* path, size_t* length) {
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    if (fseek(f, 0, SEEK_END)) { perror("fseek"); exit(1); }
    long size = ftell(f);
    if (size < 0 || size > 1024 * 1024 || fseek(f, 0, SEEK_SET)) { fputs("invalid/oversized input\n", stderr); exit(1); }
    char* p = (char*)malloc((size_t)size + 1);
    if (!p || fread(p, 1, (size_t)size, f) != (size_t)size || ferror(f)) { fputs("cannot read input\n", stderr); exit(1); }
    fclose(f); p[size] = 0; *length = (size_t)size;
    return p;
}

static mcs_vm_t* new_vm(sample_t* s) {
    mcs_config_t cfg;
    mcs_config_default(&cfg);
    cfg.realloc_fn = tracked_realloc; cfg.alloc_ud = s;
    cfg.user_data = s; cfg.write_fn = output; cfg.error_fn = error_output;
    mcs_vm_t* vm = mcs_new(&cfg);
    if (!vm) { fputs("VM initialization failed\n", stderr); exit(1); }
    return vm;
}

static void checked(mcs_vm_t* vm, mcs_result_t result, sample_t* s, int verify_output) {
    if (result != MCS_OK || s->error || (verify_output && (s->mismatch || s->seen != s->expected_len))) {
        fprintf(stderr, "benchmark rejected: status=%d output=%zu/%zu mismatch=%d %s\n",
                (int)result, s->seen, s->expected_len, s->mismatch, mcs_last_error(vm));
        mcs_free(vm); exit(1);
    }
}

static void released(mcs_vm_t* vm, sample_t* s) {
    mcs_free(vm);
    if (s->used) { fprintf(stderr, "benchmark rejected: retained after teardown=%zu\n", s->used); exit(1); }
}

static uint8_t* image(const char* source, unsigned flags, size_t* length) {
    sample_t s = {0};
    mcs_vm_t* vm = new_vm(&s);
    uint8_t* data = NULL;
    checked(vm, mcs_compile_image_ex(vm, "workload.cs", source, flags, &data, length), &s, 0);
    uint8_t* copy = (uint8_t*)malloc(*length);
    if (!copy) { fputs("image copy allocation failed\n", stderr); exit(1); }
    memcpy(copy, data, *length);
    mcs_free_image(vm, data); released(vm, &s);
    return copy;
}

static void emit(const char* phase, int iteration, double elapsed, sample_t* s,
                 mcs_mem_stats_t* gc, size_t image_bytes) {
    printf("{\"phase\":\"%s\",\"iteration\":%d,\"elapsed_us\":%.3f,"
           "\"requested_peak_bytes\":%zu,\"requested_retained_bytes\":%zu,"
           "\"allocator_calls\":%zu,\"gc_peak_bytes\":%zu,\"gc_collections\":%u,"
           "\"image_bytes\":%zu,\"verified\":true}\n",
           phase, iteration, elapsed, s->peak, s->used, s->allocations,
           gc->peak_bytes, (unsigned)gc->collections, image_bytes);
}

int main(int argc, char** argv) {
    if (argc != 4) { fprintf(stderr, "usage: %s source.cs expected.out repetitions\n", argv[0]); return 64; }
    char* end;
    errno = 0;
    long repetitions = strtol(argv[3], &end, 10);
    if (errno || !*argv[3] || *end || repetitions < 1 || repetitions > 10000) { fputs("invalid repetitions (1..10000)\n", stderr); return 64; }
    size_t source_len, expected_len, opt_len, plain_len;
    char* source = read_file(argv[1], &source_len);
    char* expected = read_file(argv[2], &expected_len);
    if (memchr(source, 0, source_len)) { fputs("source contains embedded NUL\n", stderr); return 64; }
    uint8_t* opt = image(source, 0, &opt_len);
    uint8_t* plain = image(source, MCS_IMAGE_NO_OPT, &plain_len);
    const char* phases[] = {"vm_init", "compile_opt", "compile_noopt", "source_run", "image_opt", "image_noopt", "xip_opt", "xip_noopt", "repl_cold"};
    for (unsigned phase = 0; phase < sizeof phases / sizeof phases[0]; phase++) {
        for (int i = 0; i < repetitions; i++) {
            sample_t s = {0}; s.expected = expected; s.expected_len = expected_len;
            if (phase == 8) { s.expected = "42\n"; s.expected_len = 3; }
            double start = now_us();
            mcs_vm_t* vm = new_vm(&s);
            if (phase) start = now_us();
            mcs_result_t result = MCS_OK;
            uint8_t* compiled = NULL; size_t length = 0;
            if (phase == 1 || phase == 2)
                result = mcs_compile_image_ex(vm, "workload.cs", source, phase == 2 ? MCS_IMAGE_NO_OPT : 0, &compiled, &length);
            else if (phase == 3) result = mcs_exec_source(vm, "workload.cs", source);
            else if (phase == 4 || phase == 5) { length = phase == 4 ? opt_len : plain_len; result = mcs_exec_image(vm, phase == 4 ? opt : plain, length); }
            else if (phase == 6 || phase == 7) { length = phase == 6 ? opt_len : plain_len; result = mcs_exec_image_xip(vm, phase == 6 ? opt : plain, length); }
            else if (phase == 8) {
                char prepared[128];
                if (mcs_repl_prepare("21 * 2", 6, prepared, sizeof prepared) < 0) { fputs("REPL preparation failed\n", stderr); return 1; }
                result = mcs_exec_source(vm, "<repl>", prepared);
            }
            double elapsed = now_us() - start;
            checked(vm, result, &s, phase >= 3);
            mcs_mem_stats_t gc; mcs_mem_stats(vm, &gc);
            emit(phases[phase], i, elapsed, &s, &gc, length);
            if (compiled) mcs_free_image(vm, compiled);
            released(vm, &s);
        }
    }
    sample_t warm = {0}; warm.expected = "";
    mcs_vm_t* vm = new_vm(&warm);
    checked(vm, mcs_exec_source(vm, "<repl>", "int bench_counter = 41;"), &warm, 1);
    for (int i = 0; i < repetitions; i++) {
        warm.expected = "42\n"; warm.expected_len = 3; warm.seen = 0; warm.mismatch = 0;
        warm.peak = warm.used; warm.allocations = 0;
        double start = now_us();
        char prepared[128];
        if (mcs_repl_prepare("bench_counter + 1", 17, prepared, sizeof prepared) < 0) { fputs("REPL preparation failed\n", stderr); return 1; }
        mcs_result_t result = mcs_exec_source(vm, "<repl>", prepared);
        double elapsed = now_us() - start;
        checked(vm, result, &warm, 1);
        mcs_mem_stats_t gc; mcs_mem_stats(vm, &gc);
        emit("repl_warm", i, elapsed, &warm, &gc, 0);
    }
    released(vm, &warm);
    free(opt); free(plain); free(source); free(expected);
    return 0;
}
