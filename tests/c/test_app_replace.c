/* Automatic application replacement: same pool, mounted RAM files and console. */
#include "mcs_runtime.h"
#include "mcs_threads.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
static unsigned char heap[160 * 1024];
static mcs_runtime_t rt;
static mcs_hal_t hal;
static mcs_hal_sim_t sim;
static const char* input;
static int real_console;
static char output[4096];
static size_t used;
static int failures, checks, setups, fail_setup;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr,"FAIL replace %d: %s\n",__LINE__,#x); } } while (0)
static int read_console(void* ud, uint8_t* b, size_t n, uint32_t ms) {
    (void)ud;
    if (real_console) {
        fd_set f; FD_ZERO(&f); FD_SET(0,&f);
        struct timeval t = { (long)(ms / 1000), (long)(ms % 1000) * 1000 };
        int rc = select(1,&f,NULL,NULL,&t);
        return rc > 0 ? (int)read(0,b,n) : rc;
    }
    (void)ms; size_t k = strlen(input); if (k > n) k = n;
    memcpy(b,input,k); input += k; return (int)k;
}
static void write_console(void* ud, const char* b, size_t n) {
    (void)ud; if (real_console) { fwrite(b,1,n,stdout); fflush(stdout); return; }
    if (n > sizeof output - used - 1) n = sizeof output - used - 1;
    memcpy(output + used,b,n); used += n; output[used] = 0;
}
static void setup(mcs_vm_t* vm, void* ud) {
    (void)ud; setups++;
    if (fail_setup) mcs_mem_realloc(vm,NULL,0,sizeof heap * 2u);
    mcs_set_global(vm,"boardBinding",mcs_int(42));
}
static bool command(const char* s) {
    used = 0; output[0] = 0; input = s;
    CHECK(mcs_runtime_step(&rt,0));
    return strstr(output,"\x04" "OK\n") != NULL && !strstr(output,"\x04" "ERR");
}
int main(int argc, char** argv) {
    mcs_hal_sim_init(&hal,&sim);
    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap; cfg.heap_size = sizeof heap; cfg.ramfs_size = 16 * 1024;
    cfg.thread_heap = 32 * 1024; cfg.thread_stack = 256 * 1024;
    cfg.hal = &hal; cfg.setup = setup; cfg.run_boot_scripts = false;
    cfg.mode = MCS_RUNTIME_SHELL;
    cfg.console = (mcs_transport_t){read_console,write_console,NULL};
    mcs_posixfs_t fs;
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i],"--fs")) {
        if (mcs_posixfs_init(&fs,argv[i + 1])) return 1;
        cfg.fs_ops = &mcs_posixfs_ops; cfg.fs_ctx = &fs; cfg.ramfs_size = 0;
        real_console = 1;
        return mcs_runtime_run(&rt,&cfg);
    }
    input = ""; CHECK(mcs_runtime_start(&rt,&cfg) == 0);
    const char* app = "class OldType { public int X = 7; } var app = new OldType(); "
        "var encoder = new RotaryEncoder(18,7,2,8); "
        "var pin = new Pin(9,GPIO.InputPullUp); pin.OnChange(GPIO.Falling, v => Console.WriteLine(\"OLD IRQ\")); "
        "Timer.Start(0,60000000,()=>Console.WriteLine(\"OLD TIMER\")); Scheduler.Every(60000,()=>{});";
    CHECK(!mcs_vfs_write_file(&rt.vfs,"/app.cs",app,strlen(app),false));
    CHECK(!mcs_vfs_write_file(&rt.vfs,"/data.txt","persist",7,false));
    size_t baseline = 0;
    for (int i = 0; i < 100; i++) {
        CHECK(command("run /app.cs\n")); /* no script-side Dispose/Stop/nulls */
        CHECK(sim.timer_on[0] && sim.irq[9]);
        mcs_value_t old = mcs_get_global(rt.vm,"app");
        CHECK(command("check /app.cs\n"));
        CHECK(mcs_get_global(rt.vm,"app").as.o == old.as.o && sim.timer_on[0]);
        CHECK(!command("run /absent.cs\n"));
        CHECK(mcs_get_global(rt.vm,"app").as.o == old.as.o && sim.timer_on[0]);
        mcs_hal_post(MCS_HAL_EV_TIMER,0,1); mcs_hal_post(MCS_HAL_EV_GPIO,9,0);
        /* Keep queued serial commands intact when the first command swaps VMs. */
        CHECK(command("newapp\nexec int fresh = 0; Timer.Start(0,60000000,()=>fresh++);\n"));
        CHECK(!strstr(output,"OLD IRQ") && !strstr(output,"OLD TIMER"));
        CHECK(mcs_to_int(rt.vm,mcs_get_global(rt.vm,"fresh")) == 0);
        CHECK(!sim.irq[9] && !sim.irq[18] && !sim.irq[7]);
        CHECK(!mcs_global_exists(rt.vm,"OldType") && !mcs_global_exists(rt.vm,"app"));
        for (int j = 0; j < MCS_SCHED_MAX_JOBS; j++) CHECK(rt.sched.jobs[j].state != MCS_JOB_ACTIVE);
        CHECK(mcs_to_int(rt.vm,mcs_get_global(rt.vm,"boardBinding")) == 42);
        CHECK(command("newapp\n"));
        CHECK(!sim.timer_on[0]);
        if (!i) baseline = rt.pool.used; else CHECK(rt.pool.used == baseline);
        char* data; size_t len;
        CHECK(!mcs_vfs_read_file(&rt.vfs,"/data.txt",&data,&len));
        CHECK(len == 7 && !memcmp(data,"persist",7)); mcs_vfs_free(&rt.vfs,data,len);
    }
    CHECK(command("caps\n")); CHECK(strstr(output,"Runtime.Replace=1\n"));
    CHECK(command("run /app.cs\n"));
    const char* next = "File.WriteAllText(\"/project-ran\",\"yes\");";
    CHECK(!mcs_vfs_write_file(&rt.vfs,"/next.cs",next,strlen(next),false));
    CHECK(!mcs_vfs_write_file(&rt.vfs,"/jobs.cfg","startup /next.cs\n",17,false));
    CHECK(command("project /jobs.cfg\n"));
    CHECK(!sim.timer_on[0] && !mcs_global_exists(rt.vm,"OldType"));
    CHECK(command("exec var replacementEncoder = new RotaryEncoder(18,7,2,8);\n"));
    CHECK(mcs_global_exists(rt.vm,"replacementEncoder"));
#if MCS_ENABLE_THREADS
    const char* worker = "while (true) Thread.Sleep(10);";
    CHECK(!mcs_vfs_write_file(&rt.vfs,"/worker.cs",worker,strlen(worker),false));
    CHECK(command("exec var worker = Thread.Start(\"/worker.cs\");\n"));
    CHECK(mcs_thread_count() > 0);
    CHECK(command("newapp\n"));
    CHECK(mcs_thread_count() == 0);
#endif
    CHECK(command("run /app.cs\n"));
    CHECK(command("run /app.cs shared\n") == false); /* shared explicitly keeps pin owner */
    CHECK(command("newapp\n")); baseline = rt.pool.used;
    fail_setup = 1;
    CHECK(!command("newapp\n")); CHECK(rt.vm == NULL);
    CHECK(!command("run /app.cs\n"));
    fail_setup = 0;
    CHECK(command("newapp\ninfo\n")); CHECK(rt.vm != NULL);
    CHECK(rt.pool.used == baseline);
    CHECK(command("run /app.cs\n"));
    CHECK(setups > 200);
    mcs_runtime_stop(&rt); CHECK(rt.pool.used == 0); CHECK(!sim.timer_on[0]);
    printf("%s automatic application replacement: %d checks, %d failures\n", failures ? "FAIL" : "PASS",checks,failures);
    return failures != 0;
}
