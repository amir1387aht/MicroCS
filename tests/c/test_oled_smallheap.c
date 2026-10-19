/* Real U8g2 + simulated HAL, fixed 100 KB VM pool; no physical ESP32 claim. */
#include "mcs_runtime.h"
#include <stdio.h>
#include <string.h>
static uint8_t heap[100*1024];
static int checks, failures;
#define CHECK(x) do { checks++; if(!(x)) { failures++; fprintf(stderr,"FAIL %d: %s (%s)\n",__LINE__,#x,mcs_last_error(rt.vm)); } } while(0)
static uint32_t clock_ms;
static uint32_t ticks(void* ud) { (void)ud; return clock_ms; }
static void delay(void* ud,uint32_t ms) { (void)ud; clock_ms+=ms; }
static void quiet(void* ud,const char* s,size_t n) { (void)ud;(void)s;(void)n; }
int main(void) {
    mcs_hal_t hal; mcs_hal_sim_t sim; memset(&sim,0,sizeof sim); mcs_hal_sim_init(&hal,&sim);
    mcs_sim_oled_t oled; memset(&oled,0,sizeof oled); oled.height=32; mcs_hal_sim_attach_oled(&sim,&oled);
    mcs_posixfs_t fs; if(mcs_posixfs_init(&fs,"tests/projects/oled_menu"))return 1;
    mcs_runtime_t rt; mcs_runtime_cfg_t cfg=MCS_RUNTIME_DEFAULTS;
    cfg.heap=heap;cfg.heap_size=sizeof heap;cfg.hal=&hal;cfg.fs_ops=&mcs_posixfs_ops;cfg.fs_ctx=&fs;
    cfg.run_boot_scripts=false;cfg.mode=MCS_RUNTIME_HEADLESS;cfg.console.write=quiet;cfg.ticks=ticks;cfg.delay=delay;
    if(mcs_runtime_start(&rt,&cfg))return 1;
    CHECK(mcs_check_file(rt.vm,&rt.vfs,"/app.cs")==MCS_OK);
    CHECK(mcs_exec_file(rt.vm,&rt.vfs,"/app.cs")==MCS_ERR_RUNTIME);
    CHECK(strstr(mcs_last_error(rt.vm),"Display")!=NULL);
    for(int i=0;i<3;i++) CHECK(mcs_check_file(rt.vm,&rt.vfs,"/app.cs")==MCS_OK);
    CHECK(mcs_exec_file(rt.vm,&rt.vfs,"/oled.cs")==MCS_OK);
    for(int i=0;i<5;i++) {
        CHECK(mcs_check_file(rt.vm,&rt.vfs,"/oled.cs")==MCS_OK);
        CHECK(mcs_check_file(rt.vm,&rt.vfs,"/app.cs")==MCS_OK);
        CHECK(mcs_sched_start_project(&rt.sched,"startup /oled.cs\nstartup /app.cs\n")==2);
        mcs_sched_poll(&rt.sched);
        CHECK(mcs_sched_active(&rt.sched)==0);
        CHECK(rt.sched.jobs[0].state==MCS_JOB_DONE);
        CHECK(rt.sched.jobs[1].state==MCS_JOB_DONE);
        CHECK(mcs_exec_source(rt.vm,"button.cs","app.OnEnterButtonPressed(false);")==MCS_OK);
        uint32_t frames=oled.frames;
        mcs_hal_sim_set_input(&sim,8,1); mcs_hal_poll(rt.vm);
        mcs_hal_sim_set_input(&sim,8,0); mcs_hal_poll(rt.vm);
        CHECK(oled.frames>frames);
    }
    printf("%s OLED 100 KB: %d checks, %d failures; peak %zu bytes\n",failures?"FAIL":"PASS",checks,failures,rt.pool.peak);
    mcs_runtime_stop(&rt); CHECK(rt.pool.used==0); return failures!=0;
}
