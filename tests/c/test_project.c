/* Compile-only checks and transactional jobs.cfg launch; no physical board. */
#include "mcs_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int failures, checks;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); } } while (0)
static uint32_t clock_ms;
static int fail_large_alloc;
static void* test_alloc(void* ud, void* ptr, size_t old_size, size_t size) {
    (void)ud; (void)old_size;
    if (!size) { free(ptr); return NULL; }
    if (fail_large_alloc && size > 4096) return NULL;
    return realloc(ptr, size);
}
static uint32_t ticks(void* ud) { (void)ud; return clock_ms; }
static void quiet(void* ud, const char* s, size_t n) { (void)ud; (void)s; (void)n; }
static void put(mcs_vfs_t* vfs, const char* path, const char* src) { CHECK(mcs_vfs_write_file(vfs, path, src, strlen(src), false) == 0); }
int main(void) {
    mcs_config_t cfg; mcs_config_default(&cfg); cfg.realloc_fn = test_alloc; cfg.write_fn = quiet; cfg.ticks_fn = ticks;
    mcs_vm_t* vm = mcs_new(&cfg); CHECK(vm != NULL); if (!vm) return 1;
    mcs_vfs_t vfs; mcs_vfs_init(&vfs);
    mcs_ramfs_t fs; mcs_ramfs_init(&fs, 32768, NULL, NULL);
    CHECK(!mcs_vfs_mount(&vfs, "/", &mcs_ramfs_ops, &fs, 0)); mcs_fs_open_lib(vm, &vfs);
    mcs_sched_t sched; CHECK(!mcs_sched_init(&sched, vm, &vfs, ticks, NULL));
    mcs_sched_open_lib(vm, &sched);
    CHECK(mcs_exec_source(vm, "state.cs", "int retained = 42;") == MCS_OK);
    CHECK(mcs_check_source(vm, "check.cs", "retained = 99; File.WriteAllText(\"/should_not_exist\", \"bad\");") == MCS_OK);
    CHECK(mcs_to_int(vm, mcs_get_global(vm, "retained")) == 42);
    mcs_vfs_stat_t st; CHECK(mcs_vfs_stat(&vfs, "/should_not_exist", &st) == MCS_VFS_ENOENT);
    CHECK(mcs_check_source(vm, "bad.cs", "int broken = ;") == MCS_ERR_COMPILE);
    CHECK(mcs_exec_source(vm, "ok.cs", "retained += 1;") == MCS_OK);
    char* huge = malloc(65540); CHECK(huge != NULL);
    if (huge) {
        huge[0] = '"'; memset(huge + 1, 'x', 65536); strcpy(huge + 65537, "\";");
        fail_large_alloc = 1;
        CHECK(mcs_check_source(vm, "oom.cs", huge) == MCS_ERR_MEMORY);
        fail_large_alloc = 0; free(huge);
        CHECK(mcs_check_source(vm, "recovered.cs", "int recovered = 1;") == MCS_OK);
        CHECK(mcs_exec_source(vm, "state.cs", "retained += 0;") == MCS_OK);
    }
    CHECK(mcs_exec_source(vm, "display.cs", "class Display { public enum Font { Title=7, Text=9 } }") == MCS_OK);
    CHECK(mcs_exec_source(vm, "app.cs", "int font = Display.Font.Text;") == MCS_OK);
    CHECK(mcs_to_int(vm, mcs_get_global(vm, "font")) == 9);
    /* A file run resolves namespace providers in the same directory inside
     * the compiler/runtime. Dependency top-level statements and Main methods
     * must not execute; only their declarations and static initializers load. */
    put(&vfs, "/helper.cs",
        "namespace App.Helpers { public class Helper { public static int Number = 4; "
        "public static int Value() { return Number; } "
        "public static void Main() { File.WriteAllText(\"/must_not_run\", \"main\"); } } } "
        "File.WriteAllText(\"/must_not_run\", \"top\");");
    put(&vfs, "/oled.cs",
        "using App.Helpers; namespace App.Display { public class Display { "
        "public int Center_Width(string text) { return Helper.Value(); } } } "
        "File.WriteAllText(\"/must_not_run\", \"oled\");");
    put(&vfs, "/app.cs",
        "using App.Display; var display = new Display(); int linked = display.Center_Width(\"hey\");");
    mcs_result_t dep_result = mcs_check_file(vm, &vfs, "/app.cs");
    if (dep_result != MCS_OK) fprintf(stderr, "dependency check: %s\n", mcs_last_error(vm));
    CHECK(dep_result == MCS_OK);
    CHECK(mcs_vfs_stat(&vfs, "/must_not_run", &st) == MCS_VFS_ENOENT);
    dep_result = mcs_exec_file(vm, &vfs, "/app.cs");
    if (dep_result != MCS_OK) fprintf(stderr, "dependency run: %s\n", mcs_last_error(vm));
    CHECK(dep_result == MCS_OK);
    CHECK(mcs_to_int(vm, mcs_get_global(vm, "linked")) == 4);
    CHECK(mcs_vfs_stat(&vfs, "/must_not_run", &st) == MCS_VFS_ENOENT);
    put(&vfs, "/ignored.cs",
        "/* using App.Broken; */ string text = \"using App.Broken;\"; int scannerIgnored = 7;");
    put(&vfs, "/broken-provider.cs", "namespace App.Broken { public class Broken { int x = ; } }");
    CHECK(mcs_exec_file(vm, &vfs, "/ignored.cs") == MCS_OK);
    CHECK(mcs_to_int(vm, mcs_get_global(vm, "scannerIgnored")) == 7);
    put(&vfs, "/needs-broken.cs", "using App.Broken; int neverRuns = 1;");
    dep_result = mcs_check_file(vm, &vfs, "/needs-broken.cs");
    if (dep_result != MCS_ERR_COMPILE) fprintf(stderr, "broken dependency check (%d): %s\n", dep_result, mcs_last_error(vm));
    CHECK(dep_result == MCS_ERR_COMPILE);
    put(&vfs, "/one.cs", "File.AppendAllText(\"/log\", \"one\");");
    put(&vfs, "/two.cs", "File.AppendAllText(\"/log\", \"two\");");
    put(&vfs, "/bad.cs", "int a = ;");
    int old = mcs_sched_add_file(&sched, "/one.cs", 100000, 100000, 1);
    CHECK(old > 0);
    CHECK(mcs_sched_start_project(&sched, "startup /two.cs\ninvalid /one.cs\n") < 0);
    CHECK(mcs_sched_active(&sched) == 1 && mcs_sched_job(&sched, old)->state == MCS_JOB_ACTIVE);
    CHECK(mcs_sched_start_project(&sched, "startup /one.cs\nstartup /missing.cs\n") < 0);
    CHECK(mcs_sched_active(&sched) == 1 && mcs_sched_job(&sched, old)->state == MCS_JOB_ACTIVE);
    CHECK(mcs_sched_start_project(&sched, "startup /one.cs\nstartup /bad.cs\n") < 0);
    CHECK(mcs_sched_active(&sched) == 1);
    CHECK(mcs_sched_start_project(&sched, "# empty\n") < 0);
    CHECK(mcs_sched_start_project(&sched, "startup relative.cs\n") < 0);
    CHECK(mcs_vfs_stat(&vfs, "/log", &st) == MCS_VFS_ENOENT);
    clock_ms = 100;
    CHECK(mcs_sched_start_project(&sched, "startup /one.cs\nafter 1s /two.cs restart=2 main\n") == 2);
    CHECK(mcs_sched_active(&sched) == 2);
    CHECK(mcs_vfs_stat(&vfs, "/log", &st) == MCS_VFS_ENOENT);
    mcs_sched_poll(&sched);
    char* data; size_t len;
    int read_status = mcs_vfs_read_file(&vfs, "/log", &data, &len);
    CHECK(!read_status); if (!read_status) { CHECK(!strcmp(data, "one")); mcs_vfs_free(&vfs, data, len); }
    clock_ms = 1100; mcs_sched_poll(&sched);
    read_status = mcs_vfs_read_file(&vfs, "/log", &data, &len);
    CHECK(!read_status); if (!read_status) { CHECK(!strcmp(data, "onetwo")); mcs_vfs_free(&vfs, data, len); }
    CHECK(mcs_to_int(vm, mcs_get_global(vm, "retained")) == 43);
    mcs_sched_free(&sched); mcs_ramfs_free(&fs); mcs_free(vm);
    printf("%s project: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures != 0;
}
