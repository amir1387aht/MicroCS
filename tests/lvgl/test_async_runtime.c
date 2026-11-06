/* Runtime integration: a busy GUI VM must not block shell commands or its
 * timer callbacks. Built with POSIX threads and the LVGL host display. */
#include "mcs_runtime.h"
#include "mcs_threads.h"
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int mcs_lvgl_host_init(mcs_vm_t*, int, int);
static char input[512], output[32768];
static size_t input_pos, output_len;
static int read_console(void* ud, uint8_t* p, size_t n, uint32_t timeout) {
    (void)ud; (void)timeout;
    size_t left = strlen(input) - input_pos;
    if (n > left) n = left;
    memcpy(p, input + input_pos, n); input_pos += n; return (int)n;
}
static void write_console(void* ud, const char* p, size_t n) {
    (void)ud;
    if (n > sizeof(output) - output_len - 1) n = sizeof(output) - output_len - 1;
    memcpy(output + output_len, p, n); output_len += n; output[output_len] = 0;
}
static uint32_t ticks(void* ud) { (void)ud; return mcs_os_ticks(); }
static void delay(void* ud, uint32_t ms) { (void)ud; mcs_os_sleep(ms); }
static void send(mcs_runtime_t* rt, const char* command) {
    snprintf(input, sizeof input, "%s\n", command); input_pos = 0;
    mcs_runtime_step(rt, 0);
}
static int output_has(const char* s) {
    mcs_threads_console_lock(); int ok = strstr(output, s) != NULL; mcs_threads_console_unlock(); return ok;
}
int main(void) {
    if (mcs_lvgl_host_init(NULL, 160, 100)) return 1;
    mcs_runtime_t rt;
    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.ramfs_size = 256 * 1024;
    cfg.mode = MCS_RUNTIME_SHELL; cfg.run_boot_scripts = false; cfg.async_lvgl = true;
    cfg.console.read = read_console; cfg.console.write = write_console;
    cfg.ticks = ticks; cfg.delay = delay;
    if (mcs_runtime_start(&rt, &cfg)) { fputs("runtime start failed\n", stderr); return 1; }
    const char* gui = "lv_timer_create(t => Console.WriteLine(\"GUI_TIMER\"), 10, null); while (true) {}";
    if (mcs_vfs_write_file(rt.shell.vfs, "/gui.cs", gui, strlen(gui), false) ||
        mcs_vfs_write_file(rt.shell.vfs, "/remove.cs", "x", 1, false)) return 1;
    send(&rt, "runbg /gui.cs");
    uint32_t until = mcs_os_ticks() + 5000;
    while (!output_has("GUI_TIMER") && (int32_t)(until - mcs_os_ticks()) > 0) {
        lv_tick_inc(2); mcs_os_sleep(2); mcs_runtime_step(&rt, 0);
    }
    if (!output_has("GUI_TIMER")) { fputs("GUI timer stalled\n", stderr); return 1; }
    send(&rt, "rm /remove.cs");
    mcs_vfs_stat_t st;
    if (!mcs_vfs_stat(rt.shell.vfs, "/remove.cs", &st)) { fputs("delete failed\n", stderr); return 1; }
    send(&rt, "exec Console.WriteLine(\"SHELL_OK\");");
    if (!output_has("SHELL_OK")) { fputs("shell code blocked\n", stderr); return 1; }
    send(&rt, "runstat");
    if (!output_has(" running")) { fputs("GUI stopped unexpectedly\n", stderr); return 1; }
    send(&rt, "runstop");
    if (!mcs_thread_join(rt.shell.async_worker, 3000)) { fputs("GUI did not stop\n", stderr); return 1; }
    mcs_runtime_stop(&rt);
    /* Boot and main share the GUI VM; neither can block shell startup. */
    char root[] = "/tmp/mcs-async-boot-XXXXXX";
    if (!mkdtemp(root)) return 1;
    char path[512]; FILE* f;
    snprintf(path, sizeof path, "%s/boot.cs", root); f = fopen(path, "w");
    if (!f) return 1;
    fputs("int bootValue = 23; Console.WriteLine(\"BOOT_OK\");", f); fclose(f);
    snprintf(path, sizeof path, "%s/main.cs", root); f = fopen(path, "w");
    if (!f) return 1;
    fputs("Console.WriteLine(\"MAIN_\" + bootValue); lv_timer_create(t => Console.WriteLine(\"BOOT_TIMER\"), 10, null); while (true) {}", f); fclose(f);
    snprintf(path, sizeof path, "%s/jobs.cfg", root); f = fopen(path, "w");
    if (!f) return 1;
    fputs("every 20 /job.cs\n", f); fclose(f);
    snprintf(path, sizeof path, "%s/job.cs", root); f = fopen(path, "w");
    if (!f) return 1;
    fputs("Console.WriteLine(\"JOB_OK\");", f); fclose(f);
    mcs_posixfs_t posix;
    if (mcs_posixfs_init(&posix, root)) return 1;
    output_len = 0; output[0] = 0; input_pos = 0; input[0] = 0;
    cfg.ramfs_size = 0; cfg.fs_ops = &mcs_posixfs_ops; cfg.fs_ctx = &posix; cfg.run_boot_scripts = true;
    if (mcs_runtime_start(&rt, &cfg)) { fputs("boot runtime start failed\n", stderr); return 1; }
    until = mcs_os_ticks() + 5000;
    while ((!output_has("BOOT_TIMER") || !output_has("JOB_OK")) && (int32_t)(until - mcs_os_ticks()) > 0) {
        lv_tick_inc(2); mcs_os_sleep(2); mcs_runtime_step(&rt, 0);
    }
    if (!output_has("BOOT_OK") || !output_has("MAIN_23") || !output_has("BOOT_TIMER") || !output_has("JOB_OK")) {
        fputs("boot/main/jobs did not run correctly\n", stderr); return 1;
    }
    send(&rt, "exec Console.WriteLine(\"BOOT_SHELL_OK\");");
    if (!output_has("BOOT_SHELL_OK")) { fputs("boot app blocked shell\n", stderr); return 1; }
    send(&rt, "runstop");
    if (!mcs_thread_join(rt.shell.async_worker, 3000)) return 1;
    mcs_runtime_stop(&rt);
    snprintf(path, sizeof path, "%s/boot.cs", root); unlink(path);
    snprintf(path, sizeof path, "%s/main.cs", root); unlink(path);
    snprintf(path, sizeof path, "%s/jobs.cfg", root); unlink(path);
    snprintf(path, sizeof path, "%s/job.cs", root); unlink(path); rmdir(root);
    puts("PASS runtime GUI task, timer, delete, shell execution and boot");
    return 0;
}
