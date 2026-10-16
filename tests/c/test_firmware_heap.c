/* Release-size fixed pools, not a malloc-backed --heap accounting limit.
 * Run the real caps command and the actual Studio benchmark, including reruns.
 * Host pointers/values are larger than RP2040's, so this is conservative. */
#include "mcs_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t heap[160 * 1024];
static mcs_runtime_t rt;
static mcs_hal_t hal;
static mcs_hal_sim_t sim;
static char output[8192];
static size_t output_len;
static const char* input;
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)
static void write_console(void* ud, const char* s, size_t n) {
    (void)ud;
    size_t room = sizeof output - 1 - output_len;
    if (n > room) n = room;
    memcpy(output + output_len, s, n); output_len += n; output[output_len] = 0;
}
static int read_console(void* ud, uint8_t* buf, size_t n, uint32_t timeout) {
    (void)ud; (void)timeout;
    size_t k = strlen(input); if (k > n) k = n;
    memcpy(buf, input, k); input += k; return (int)k;
}
static void reset_output(void) { output_len = 0; output[0] = 0; }

int main(void) {
    const char* path = "tools/studio/templates/27-benchmarks/bench_cpu.cs";
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    if (size < 0 || size > 8192) { fclose(f); return 1; }
    char source[8193];
    if (fread(source, 1, (size_t)size, f) != (size_t)size) { fclose(f); return 1; }
    source[size] = 0; fclose(f);
    const size_t sizes[] = { 96 * 1024, 128 * 1024, 160 * 1024 };
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        int before = failures;
        memset(&sim, 0, sizeof sim); mcs_hal_sim_init(&hal, &sim);
        mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
        cfg.heap = heap; cfg.heap_size = sizes[i]; cfg.hal = &hal;
        cfg.console = (mcs_transport_t){ read_console, write_console, NULL };
        cfg.mode = MCS_RUNTIME_SHELL; cfg.run_boot_scripts = false;
        input = ""; reset_output();
        int rc = mcs_runtime_start(&rt, &cfg);
        CHECK(rc == 0); if (rc) continue;
        reset_output(); input = "caps\n";
        size_t used = rt.pool.used;
        CHECK(mcs_runtime_step(&rt, 0));
        CHECK(strncmp(output, "capabilities=1\n", 15) == 0);
        CHECK(strstr(output, "\nCompiler=1\n") != NULL);
        CHECK(strstr(output, "\nGPIO=1\n") != NULL);
        CHECK(strstr(output, "\nU8g2=0\n") != NULL);
        CHECK(strstr(output, "\x04" "OK\n") != NULL);
        CHECK(rt.pool.used == used);  /* capability discovery stays allocation-free */
        size_t count; const char* const* names = mcs_capability_names(&count);
        for (size_t c = 0; c < count; c++) {
            char line[96];
            snprintf(line, sizeof line, "\n%s=%d\n", names[c], mcs_capability_has(rt.vm, names[c]) ? 1 : 0);
            CHECK(strstr(output, line) != NULL);
        }
        /* Hold the uploaded source in the pool as firmware does, too. */
        char* uploaded = mcs_pool_realloc(&rt.pool, NULL, 0, (size_t)size + 1);
        CHECK(uploaded != NULL);
        if (uploaded) {
            memcpy(uploaded, source, (size_t)size + 1);
            /* 96 KB is a single-run smoke test: compiling a second source while
             * the prior function is still global needs more room with host-sized
             * pointers. The reported RP2040 (128 KB) also tests repeated runs. */
            int repeats = sizes[i] == 96 * 1024 ? 1 : 2;
            for (int repeat = 0; repeat < repeats; repeat++) {
                reset_output(); mcs_gc(rt.vm);
                rc = mcs_exec_source(rt.vm, "bench_cpu.cs", uploaded);
                if (rc != MCS_OK) fprintf(stderr, "%s\n%s\n", output, mcs_last_error(rt.vm));
                CHECK(rc == MCS_OK);
                CHECK(strstr(output, "Dictionary 500") != NULL);
                CHECK(strstr(output, "total ") != NULL);
            }
            mcs_pool_realloc(&rt.pool, uploaded, (size_t)size + 1, 0);
        }
        printf("%s fixed pool %u KB: caps + benchmark (peak %u bytes)\n",
               failures != before ? "FAIL" : "PASS", (unsigned)(sizes[i] / 1024), (unsigned)rt.pool.peak);
        mcs_runtime_stop(&rt);
        CHECK(rt.pool.used == 0);
    }
    return failures != 0;
}
