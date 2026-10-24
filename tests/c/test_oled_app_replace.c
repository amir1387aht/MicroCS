/* Automatic runtime/Studio Run replacement; no manual script cleanup.
 * User-reported app: repeated upload/check/Project launch with real U8g2 and
 * live GPIO + hardware-timer callbacks. POSIX files model off-pool flash;
 * compiler, VFS scratch, bytecode, VM and display allocations share one pool. */
#include "mcs_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef OLED_REPEAT_CYCLES
#define OLED_REPEAT_CYCLES 2000
#endif
#ifndef OLED_REPEAT_LIVE_BYTES
#define OLED_REPEAT_LIVE_BYTES 0
#endif
#ifndef OLED_REPEAT_HEAP_KB
#define OLED_REPEAT_HEAP_KB 128
#endif
#ifdef OLED_REPEAT_BUDGET
typedef char require_32_bit[(sizeof(void*) == 4) ? 1 : -1];
#endif
static uint8_t heap[OLED_REPEAT_HEAP_KB * 1024];
static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr,"FAIL repeat %d: %s (%s)\n",__LINE__,#x,mcs_last_error(rt.vm)); } } while (0)
static uint32_t clock_ms;
static uint32_t ticks(void* ud) { (void)ud; return clock_ms; }
static void delay(void* ud, uint32_t ms) { (void)ud; clock_ms += ms; }
static const char* input = "";
static int receive(void* ud, uint8_t* b, size_t n, uint32_t timeout) {
    (void)ud; (void)timeout; size_t k = strlen(input); if (k > n) k = n;
    memcpy(b,input,k); input += k; return (int)k;
}
static char output[256]; static size_t output_len;
static void capture(void* ud, const char* s, size_t n) {
    (void)ud; size_t room = sizeof output - 1 - output_len;
    if (n > room) n = room;
    memcpy(output + output_len, s, n); output_len += n; output[output_len] = 0;
}
static char* fixture(const char* path, size_t* len) {
    FILE* f = fopen(path,"rb"); if (!f) return NULL;
    if (fseek(f,0,SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f); if (n < 0) { fclose(f); return NULL; } rewind(f);
    char* s = malloc((size_t)n + 2049);
    if (!s || fread(s,1,(size_t)n,f) != (size_t)n) { free(s); fclose(f); return NULL; }
    fclose(f); s[n] = 0; *len = (size_t)n; return s;
}
int main(void) {
    size_t app_len, oled_len;
    char* app = fixture("tests/projects/oled_encoder_repeat/app.cs",&app_len);
    char* provider = fixture("tests/projects/oled_encoder_repeat/oled.cs",&oled_len);
    char dir[] = "/tmp/microcs-repeat-XXXXXX";
    if (!app || !provider || !mkdtemp(dir)) { free(app); free(provider); return 1; }
    mcs_hal_t hal; mcs_hal_sim_t sim; memset(&sim,0,sizeof sim); mcs_hal_sim_init(&hal,&sim); sim.adc[4] = 828;
    mcs_sim_oled_t oled; memset(&oled,0,sizeof oled); oled.height = 32; mcs_hal_sim_attach_oled(&sim,&oled);
    mcs_posixfs_t fs; if (mcs_posixfs_init(&fs,dir)) return 1;
    mcs_runtime_t rt; mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap; cfg.heap_size = sizeof heap; cfg.hal = &hal;
    cfg.fs_ops = &mcs_posixfs_ops; cfg.fs_ctx = &fs; cfg.run_boot_scripts = false;
    cfg.mode = MCS_RUNTIME_SHELL; cfg.console.write = capture; cfg.console.read = receive; cfg.ticks = ticks; cfg.delay = delay;
    if (mcs_runtime_start(&rt,&cfg)) return 1;
    CHECK(!mcs_vfs_mkdir(&rt.vfs,"/files")); CHECK(!mcs_vfs_mkdir(&rt.vfs,"/files/text"));
    CHECK(!mcs_vfs_write_file(&rt.vfs,"/files/text/sample.txt","hello",5,false));
    char* title = strstr(app, "Welcome!");
    if (!title) return 1;
    /* Extra live state narrows the headroom to the failing device scenario.
     * This C buffer only supplies input: the retained string is pool allocated. */
    char retained[OLED_REPEAT_LIVE_BYTES + 1]; memset(retained, 'r', sizeof retained);
    mcs_value_t held = mcs_string_n(rt.vm, retained, OLED_REPEAT_LIVE_BYTES);
    mcs_push_root(rt.vm, held); mcs_set_global(rt.vm, "retained_user_data", held); mcs_pop_root(rt.vm, 1);
    unsigned completed = 0;
    size_t warm_min = SIZE_MAX, warm_max = 0;
    for (int i = 0; i < OLED_REPEAT_CYCLES; i++) {
        /* Keep uploaded sources unchanged: even identical reruns must clean up. */
        size_t padding = 0;
        memset(app + app_len,'\n',padding); app[app_len + padding] = 0;
        CHECK(!mcs_vfs_write_file(&rt.vfs,"/app.cs",app,app_len + padding,false));
        CHECK(!mcs_vfs_write_file(&rt.vfs,"/oled.cs",provider,oled_len,false));

        output_len = 0; output[0] = 0; input = "newapp\n";
        CHECK(mcs_runtime_step(&rt,0));
        CHECK(strstr(output,"\x04" "OK\n") != NULL && !strstr(output,"\x04" "ERR"));
        CHECK(!sim.timer_on[0] && !sim.irq[18] && !sim.irq[7]);
        CHECK(!mcs_global_exists(rt.vm,"app"));
        mcs_value_t prior = mcs_get_global(rt.vm,"app");
        uint32_t frames = oled.frames, fired = sim.timer_fired[0];
        CHECK(mcs_check_file(rt.vm,&rt.vfs,"/app.cs") == MCS_OK);
        CHECK(mcs_check_file(rt.vm,&rt.vfs,"/oled.cs") == MCS_OK);
        CHECK(oled.frames == frames && sim.timer_fired[0] == fired);
        if (failures) { fprintf(stderr,"Failed cycle %d\n", i + 1); break; }
        mcs_value_t after = mcs_get_global(rt.vm,"app");
        CHECK(prior.type == after.type && (mcs_is_null(prior) || prior.as.o == after.as.o));
        if (i >= 10) {
            if (rt.pool.used < warm_min) warm_min = rt.pool.used;
            if (rt.pool.used > warm_max) warm_max = rt.pool.used;
        }
        output_len = 0; output[0] = 0; input = "run /app.cs\n";
        CHECK(mcs_runtime_step(&rt,0));
        CHECK(strstr(output,"\x04" "OK\n") != NULL && !strstr(output,"\x04" "ERR"));
        CHECK(oled.frames > frames && sim.timer_on[0]);
        /* Check remains non-destructive, including failed source checks. The
         * full next-app build is intentionally AFTER newapp, just like Studio. */
        prior = mcs_get_global(rt.vm,"app");
        CHECK(mcs_check_file(rt.vm,&rt.vfs,"/oled.cs") == MCS_OK);
        CHECK(mcs_check_source(rt.vm,"broken","int broken = ;") != MCS_OK);
        CHECK(mcs_get_global(rt.vm,"app").as.o == prior.as.o && sim.timer_on[0]);
        if (failures) { fprintf(stderr,"Failed cycle %d\n",i + 1); break; }
        completed++;
    }
    if (completed > 10) CHECK(warm_min != SIZE_MAX && warm_max - warm_min < 2048 * (sizeof(void*) / 4));
    mcs_mem_stats_t memory; mcs_mem_stats(rt.vm,&memory);
    CHECK(memory.stack_limit == MCS_DEFAULT_STACK && memory.frame_limit == MCS_DEFAULT_FRAMES);
#ifdef OLED_REPEAT_BUDGET
    CHECK(rt.pool.peak < sizeof heap); /* headroom inside the unchanged 64 KiB pool */
#endif
    size_t peak = rt.pool.peak;
    CHECK(!mcs_vfs_remove(&rt.vfs,"/setting.json"));
    CHECK(!mcs_vfs_remove(&rt.vfs,"/app.cs")); CHECK(!mcs_vfs_remove(&rt.vfs,"/oled.cs"));
    CHECK(!mcs_vfs_remove(&rt.vfs,"/files/text/sample.txt"));
    CHECK(!mcs_vfs_remove(&rt.vfs,"/files/text")); CHECK(!mcs_vfs_remove(&rt.vfs,"/files"));
    mcs_runtime_stop(&rt); CHECK(rt.pool.used == 0); rmdir(dir); free(app); free(provider);
    printf("%s OLED repeat %u KiB (cycles %u/%u, %u retained bytes): %d checks, %d failures; peak %zu; warm %zu..%zu bytes\n",
           failures ? "FAIL" : "PASS",OLED_REPEAT_HEAP_KB,completed,(unsigned)OLED_REPEAT_CYCLES,(unsigned)OLED_REPEAT_LIVE_BYTES,checks,failures,peak,warm_min,warm_max);
    return failures != 0;
}
