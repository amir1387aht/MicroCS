/* User-reported app: repeated upload/check/Project launch with real U8g2 and
 * live GPIO + hardware-timer callbacks. POSIX files model off-pool flash;
 * compiler, VFS scratch, bytecode, VM and display allocations share one pool. */
#include "mcs_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
    char* app = fixture("tests/projects/oled_repeat/app.cs",&app_len);
    char* provider = fixture("tests/projects/oled_repeat/oled.cs",&oled_len);
    char dir[] = "/tmp/microcs-repeat-XXXXXX";
    if (!app || !provider || !mkdtemp(dir)) { free(app); free(provider); return 1; }
    mcs_hal_t hal; mcs_hal_sim_t sim; memset(&sim,0,sizeof sim); mcs_hal_sim_init(&hal,&sim); sim.adc[4] = 828;
    mcs_sim_oled_t oled; memset(&oled,0,sizeof oled); oled.height = 32; mcs_hal_sim_attach_oled(&sim,&oled);
    mcs_posixfs_t fs; if (mcs_posixfs_init(&fs,dir)) return 1;
    mcs_runtime_t rt; mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap; cfg.heap_size = sizeof heap; cfg.hal = &hal;
    cfg.fs_ops = &mcs_posixfs_ops; cfg.fs_ctx = &fs; cfg.run_boot_scripts = false;
    cfg.mode = MCS_RUNTIME_HEADLESS; cfg.console.write = capture; cfg.ticks = ticks; cfg.delay = delay;
    if (mcs_runtime_start(&rt,&cfg)) return 1;
    CHECK(!mcs_vfs_mkdir(&rt.vfs,"/files")); CHECK(!mcs_vfs_mkdir(&rt.vfs,"/files/text"));
    CHECK(!mcs_vfs_write_file(&rt.vfs,"/files/text/sample.txt","hello",5,false));
    size_t warm_min = SIZE_MAX, warm_max = 0;
    for (int i = 0; i < 100; i++) {
        /* Vary source size without changing the user's application logic. */
        size_t padding = (size_t)(i % 5) * 256;
        memset(app + app_len,'\n',padding); app[app_len + padding] = 0;
        CHECK(!mcs_vfs_write_file(&rt.vfs,"/app.cs",app,app_len + padding,false));
        CHECK(!mcs_vfs_write_file(&rt.vfs,"/oled.cs",provider,oled_len,false));
        mcs_value_t prior = mcs_get_global(rt.vm,"app");
        uint32_t frames = oled.frames, fired = sim.timer_fired[0];
        CHECK(mcs_check_file(rt.vm,&rt.vfs,"/app.cs") == MCS_OK);
        CHECK(mcs_check_file(rt.vm,&rt.vfs,"/oled.cs") == MCS_OK);
        CHECK(oled.frames == frames && sim.timer_fired[0] == fired);
        mcs_value_t after = mcs_get_global(rt.vm,"app");
        CHECK(prior.type == after.type && (mcs_is_null(prior) || prior.as.o == after.as.o));
        if (i >= 10) {
            if (rt.pool.used < warm_min) warm_min = rt.pool.used;
            if (rt.pool.used > warm_max) warm_max = rt.pool.used;
        }
        CHECK(mcs_sched_start_project(&rt.sched,"startup /app.cs\n") == 1);
        mcs_sched_poll(&rt.sched);
        CHECK(mcs_sched_active(&rt.sched) == 0 && rt.sched.jobs[0].state == MCS_JOB_DONE);
        CHECK(oled.frames > frames && sim.timer_on[0]);
        /* Compile-only checks leave both callbacks usable, including the old
         * app graph while the next Project has not yet committed. */
        CHECK(mcs_check_file(rt.vm,&rt.vfs,"/app.cs") == MCS_OK);
        mcs_hal_sim_set_input(&sim,8,1); mcs_hal_poll(rt.vm);
        mcs_hal_sim_set_input(&sim,8,0); mcs_hal_poll(rt.vm);
        CHECK(mcs_to_int(rt.vm,mcs_get_field(rt.vm,mcs_get_global(rt.vm,"app"),"current_screen_index")) == 3);
        output_len = 0; output[0] = 0; sim.timer_next[0] = sim.virt_us;
        mcs_hal_poll(rt.vm);
        CHECK(strstr(output,"828") != NULL);
    }
    CHECK(warm_max - warm_min < 2048);
    mcs_mem_stats_t memory; mcs_mem_stats(rt.vm,&memory);
    CHECK(memory.stack_limit == MCS_DEFAULT_STACK && memory.frame_limit == MCS_DEFAULT_FRAMES);
#ifdef OLED_REPEAT_BUDGET
    CHECK(rt.pool.peak < 60 * 1024); /* headroom inside the unchanged 64 KiB pool */
#endif
    size_t peak = rt.pool.peak;
    CHECK(!mcs_vfs_remove(&rt.vfs,"/app.cs")); CHECK(!mcs_vfs_remove(&rt.vfs,"/oled.cs"));
    CHECK(!mcs_vfs_remove(&rt.vfs,"/files/text/sample.txt"));
    CHECK(!mcs_vfs_remove(&rt.vfs,"/files/text")); CHECK(!mcs_vfs_remove(&rt.vfs,"/files"));
    mcs_runtime_stop(&rt); CHECK(rt.pool.used == 0); rmdir(dir); free(app); free(provider);
    printf("%s OLED repeat %u KiB: %d checks, %d failures; peak %zu; warm %zu..%zu bytes\n",
           failures ? "FAIL" : "PASS",OLED_REPEAT_HEAP_KB,checks,failures,peak,warm_min,warm_max);
    return failures != 0;
}
