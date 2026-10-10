/* MicroCS - OS threads through the firmware runtime (build with -DMCS_OS=MCS_OS_POSIX
 * -pthread, or any OS port): Thread.Run/Start, Channels, the shared RAM filesystem and
 * pool heap, jobs.cfg `thread` jobs, cfg.thread_setup and the C API. */
#include "mcs_runtime.h"
#include "mcs_threads.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#if MCS_ENABLE_THREADS && MCS_ENABLE_RUNTIME && MCS_ENABLE_FS && MCS_ENABLE_SCHED && MCS_ENABLE_COMPILER
static char outbuf[8192];
static size_t outlen;
static void con_write(void* ud, const char* s, size_t n) {
    (void)ud;   /* the runtime serialises console output */
    if (outlen + n < sizeof outbuf) { memcpy(outbuf + outlen, s, n); outlen += n; outbuf[outlen] = 0; }
}
/* the OS layer's clock, so the same test runs on POSIX threads and FreeRTOS tasks */
static uint32_t now_ms(void* ud) { (void)ud; return mcs_os_ticks(); }
static void sleep_ms(void* ud, uint32_t ms) { (void)ud; mcs_os_sleep(ms); }

static mcs_runtime_t rt;
static int setups, thread_setups;
static mcs_value_t n_board(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)self; (void)argc; (void)argv; return mcs_string(vm, "test-board");
}
static const mcs_reg_t board_fns[] = { MCS_FN("Name", n_board, 0), MCS_REG_END };
static void setup(mcs_vm_t* vm, void* ud) {
    CHECK(ud == &rt); setups++;
    mcs_register_module(vm, "Board", board_fns);
    const char* lib = "int Sq(int x) { return x * x; }";
    const char* prod = "var ch = new Channel(\"c\", 4); for (int i = 1; i <= 10; i++) ch.Send(i);"
                       "File.WriteAllText(\"/t.txt\", \"from \" + Board.Name());";
    const char* tick = "File.AppendAllText(\"/ticks.txt\", \"x\");";
    const char* jobs = "every 50ms /tick.cs thread heap=48k\n";
    const char* m =
        "var w = Thread.Run(\"/lib.cs\", \"Sq\", 7); Console.WriteLine(\"sq \" + w.Result);"
        "var ch = new Channel(\"c\", 4); var p = Thread.Start(\"/prod.cs\");"
        "int s = 0; for (int i = 0; i < 10; i++) s += (int)ch.Receive(5000);"
        "Console.WriteLine(\"sum \" + s); p.Join(5000);"
        "Console.WriteLine(File.ReadAllText(\"/t.txt\"));"
        "var bad = Thread.Run(\"/lib.cs\", \"Nope\"); bad.Join(5000); Console.WriteLine(\"bad \" + bad.State + \" \" + (bad.Error.Length > 0));";
    mcs_vfs_write_file(&rt.vfs, "/lib.cs", lib, strlen(lib), false);
    mcs_vfs_write_file(&rt.vfs, "/prod.cs", prod, strlen(prod), false);
    mcs_vfs_write_file(&rt.vfs, "/tick.cs", tick, strlen(tick), false);
    mcs_vfs_write_file(&rt.vfs, "/jobs.cfg", jobs, strlen(jobs), false);
    mcs_vfs_write_file(&rt.vfs, "/main.cs", m, strlen(m), false);
}
static void thread_setup(mcs_vm_t* vm, void* ud) {
    (void)ud;
    __atomic_add_fetch(&thread_setups, 1, __ATOMIC_SEQ_CST);
    mcs_register_module(vm, "Board", board_fns);
}

static void test_runtime_threads(void) {
    static uint8_t heap[1024 * 1024];
    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap; cfg.heap_size = sizeof heap; cfg.ramfs_size = 32768;
    cfg.console.write = con_write;
    cfg.ticks = now_ms; cfg.delay = sleep_ms;
    cfg.mode = MCS_RUNTIME_HEADLESS;
    cfg.setup = setup; cfg.thread_setup = thread_setup; cfg.ud = &rt;
    cfg.thread_heap = 64 * 1024;
    CHECK(mcs_runtime_start(&rt, &cfg) == 0);
    CHECK(setups == 1);
    CHECK(strstr(outbuf, "sq 49\n") != NULL);
    CHECK(strstr(outbuf, "sum 55\n") != NULL);
    CHECK(strstr(outbuf, "from test-board\n") != NULL);
    CHECK(strstr(outbuf, "bad failed True\n") != NULL);
    uint32_t t0 = now_ms(NULL);
    while (now_ms(NULL) - t0 < 400) mcs_runtime_step(&rt, 20);
    char* tk; size_t tl;
    CHECK(mcs_vfs_read_file(&rt.vfs, "/ticks.txt", &tk, &tl) == 0);
    if (tk) { CHECK(tl >= 4); mcs_vfs_free(&rt.vfs, tk, tl); }

    /* C API: source in memory */
    mcs_thread_spec_t sp = MCS_THREAD_SPEC_DEFAULTS;
    const char* code = "Console.WriteLine(\"c-api \" + Board.Name());";
    sp.code = code; sp.code_len = strlen(code);
    int id = mcs_thread_start(&sp);
    CHECK(id > 0);
    CHECK(mcs_thread_join(id, 5000));
    mcs_thread_info_t inf;
    CHECK(mcs_thread_info(id, &inf) && inf.state == MCS_THREAD_DONE && inf.runs == 1);
    mcs_thread_release(id);
    CHECK(strstr(outbuf, "c-api test-board\n") != NULL);
    CHECK(thread_setups >= 5);
    mcs_runtime_stop(&rt);
    CHECK(rt.vm == NULL && mcs_thread_count() == 0);
}
#endif

static int run(void) {
#if MCS_ENABLE_THREADS && MCS_ENABLE_RUNTIME && MCS_ENABLE_FS && MCS_ENABLE_SCHED && MCS_ENABLE_COMPILER
    printf("test_threads on %s, %d core(s)\n", mcs_os_name(), mcs_os_cores());
    test_runtime_threads();
    if (failures) printf("--- output ---\n%s\n", outbuf);
#else
    printf("test_threads: no OS chosen (MCS_OS_NONE), nothing to test\n");
#endif
    printf("%d/%d thread checks passed\n", checks - failures, checks);
    fflush(stdout);
    return failures != 0;
}

#if MCS_OS == MCS_OS_FREERTOS
/* FreeRTOS (the POSIX/Linux simulator port in `make freertos-test`): the test runs as a task */
#include "FreeRTOS.h"
#include "task.h"
static void test_task(void* arg) { (void)arg; exit(run()); }
int main(void) {
    xTaskCreate(test_task, "test", 64 * 1024 / sizeof(StackType_t), NULL, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
#else
int main(void) { return run(); }
#endif
