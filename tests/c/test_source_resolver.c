/* Dependency-discovery boundaries; host-only POSIX tests use a temporary tree. */
#include "mcs_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif
static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); } } while (0)
static size_t live;
static int fail_at = -1, calls;
static void* tracked(void* ud, void* ptr, size_t old, size_t size) {
    (void)ud;
    if (!size) { CHECK(live >= old); live -= old; free(ptr); return NULL; }
    if (fail_at >= 0 && calls++ == fail_at) return NULL;
    /* Always relocate scratch growth to catch stale DFS frame pointers. */
    void* next = malloc(size);
    if (next) {
        if (ptr) { memcpy(next, ptr, old < size ? old : size); free(ptr); }
        CHECK(live >= old); live = live - old + size;
    }
    return next;
}
static void quiet(void* ud, const char* s, size_t n) { (void)ud; (void)s; (void)n; }
static mcs_vm_t* new_vm(void) {
    mcs_config_t cfg; mcs_config_default(&cfg); cfg.write_fn = quiet;
    mcs_vm_t* vm = mcs_new(&cfg); CHECK(vm != NULL); return vm;
}
static void put(mcs_vfs_t* v, const char* path, const char* text) {
    CHECK(mcs_vfs_write_file(v, path, text, strlen(text), false) == MCS_VFS_OK);
}
static void run(mcs_vm_t* vm, mcs_vfs_t* v, const char* path, int expected) {
    mcs_result_t r = mcs_exec_file(vm, v, path);
    if (r != MCS_OK) fprintf(stderr, "%s: %s\n", path, mcs_last_error(vm));
    CHECK(r == MCS_OK);
    CHECK(mcs_to_int(vm, mcs_get_global(vm, "result")) == expected);
    CHECK(live == 0);
}
typedef struct { int count, stop; bool ordered; } listing_t;
static int listed(void* ud, const char* name, const mcs_vfs_stat_t* st) {
    listing_t* l = (listing_t*)ud;
    char expected[40]; snprintf(expected, sizeof expected, "item%03d.txt", l->count);
    if (strcmp(name, expected) || st->is_dir) l->ordered = false;
    l->count++;
    return l->stop && l->count == l->stop;
}
static void ramfs_listing(void) {
    mcs_vfs_t v; mcs_vfs_init(&v); mcs_ramfs_t fs; mcs_ramfs_init(&fs, 65536, NULL, NULL);
    CHECK(!mcs_vfs_mount(&v, "/", &mcs_ramfs_ops, &fs, 0));
    CHECK(!mcs_vfs_mkdir(&v, "/items"));
    for (int i = 0; i < 193; i++) { char p[40]; snprintf(p, sizeof p, "/items/item%03d.txt", i); put(&v, p, "x"); }
    listing_t l = {0, 0, true}; CHECK(!mcs_vfs_list(&v, "/items", listed, &l)); CHECK(l.count == 193 && l.ordered);
    l = (listing_t){0, 65, true}; CHECK(!mcs_vfs_list(&v, "/items", listed, &l)); CHECK(l.count == 65 && l.ordered);
    CHECK(!mcs_vfs_mkdir(&v, "/empty")); l = (listing_t){0, 0, true};
    CHECK(!mcs_vfs_list(&v, "/empty", listed, &l)); CHECK(l.count == 0);
    mcs_ramfs_free(&fs);
}
static void nested_mounts(void) {
    mcs_vm_t* vm = new_vm(); if (!vm) return;
    mcs_vfs_t v; mcs_vfs_init(&v); v.realloc_fn = tracked;
    mcs_ramfs_t apps, drivers; mcs_ramfs_init(&apps, 8192, NULL, NULL); mcs_ramfs_init(&drivers, 8192, NULL, NULL);
    CHECK(!mcs_vfs_mount(&v, "/apps", &mcs_ramfs_ops, &apps, 0));
    CHECK(!mcs_vfs_mount(&v, "/drivers/display", &mcs_ramfs_ops, &drivers, 0));
    put(&v, "/apps/app.cs", "using Mount.Display; int result = Mounted.Value;");
    put(&v, "/drivers/display/oled.cs", "namespace Mount.Display { class Mounted { public static int Value = 42; } }");
    CHECK(mcs_check_file(vm, &v, "/apps/app.cs") == MCS_OK); CHECK(live == 0);
    run(vm, &v, "/apps/app.cs", 42);
    /* Executable mounts remain enforced even when the provider was discovered
     * by entering its nested mount root directly. */
    v.mounts[1].flags = MCS_VFS_NOEXEC;
    CHECK(mcs_exec_file(vm, &v, "/apps/app.cs") == MCS_ERR_RUNTIME); CHECK(live == 0);
    v.mounts[1].flags = 0; run(vm, &v, "/apps/app.cs", 42);
    mcs_ramfs_free(&apps); mcs_ramfs_free(&drivers); mcs_free(vm);
}
static void deep_chain(int depth) {
    mcs_vm_t* vm = new_vm(); if (!vm) return;
    mcs_vfs_t v; mcs_vfs_init(&v); v.realloc_fn = tracked;
    mcs_ramfs_t fs; mcs_ramfs_init(&fs, 262144, NULL, NULL);
    CHECK(!mcs_vfs_mount(&v, "/", &mcs_ramfs_ops, &fs, 0));
    char path[64], src[256];
    for (int i = 0; i < depth; i++) {
        snprintf(path, sizeof path, "/n%d.cs", i);
        snprintf(src, sizeof src, "using Chain.N%d; namespace Chain.N%d { class Chain%d { public static int Value = %d; } }", (i + 1) % depth, i, i, i + 17);
        put(&v, path, src);
    }
    put(&v, "/app.cs", "using Chain.N0; int result = Chain0.Value;");
    run(vm, &v, "/app.cs", 17);
    CHECK(mcs_check_file(vm, &v, "/app.cs") == MCS_OK); CHECK(live == 0);
    if (depth <= 12) {
        bool complete = false;
        for (int fault = 0; fault < 4096; fault++) {
            fail_at = fault; calls = 0;
            mcs_result_t r = mcs_check_file(vm, &v, "/app.cs");
            fail_at = -1; CHECK(live == 0);
            if (r == MCS_OK) { complete = true; break; }
            CHECK(r == MCS_ERR_MEMORY);
        }
        CHECK(complete);
        run(vm, &v, "/app.cs", 17);
    }
    mcs_ramfs_free(&fs); mcs_free(vm); CHECK(live == 0);
}
#if defined(__unix__) || defined(__APPLE__)
static void posix_listing(void) {
    char root[] = "/tmp/microcs-resolver-XXXXXX"; CHECK(mkdtemp(root) != NULL);
    mcs_posixfs_t fs; CHECK(!mcs_posixfs_init(&fs, root));
    mcs_vfs_t v; mcs_vfs_init(&v); v.realloc_fn = tracked;
    CHECK(!mcs_vfs_mount(&v, "/", &mcs_posixfs_ops, &fs, 0));
    /* Create backwards to ensure the backend really sorts all 300 entries. */
    for (int i = 299; i >= 0; i--) { char p[40]; snprintf(p, sizeof p, "/item%03d.txt", i); put(&v, p, "x"); }
    listing_t l = {0, 0, true}; CHECK(!mcs_vfs_list(&v, "/", listed, &l)); CHECK(l.count == 300 && l.ordered);
    l = (listing_t){0, 257, true}; CHECK(!mcs_vfs_list(&v, "/", listed, &l)); CHECK(l.count == 257 && l.ordered);
    mcs_vm_t* vm = new_vm();
    put(&v, "/zz-provider.cs", "namespace Posix.Display { class PosixDisplay { public static int Value = 29; } }");
    put(&v, "/app.cs", "using Posix.Display; int result = PosixDisplay.Value;");
    if (vm) { run(vm, &v, "/app.cs", 29); mcs_free(vm); }
    for (int i = 0; i < 300; i++) { char p[40]; snprintf(p, sizeof p, "/item%03d.txt", i); CHECK(!mcs_vfs_remove(&v, p)); }
    CHECK(!mcs_vfs_remove(&v, "/zz-provider.cs")); CHECK(!mcs_vfs_remove(&v, "/app.cs")); CHECK(!rmdir(root)); CHECK(live == 0);
}
#endif
int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "--deep-chain")) deep_chain(512);
    else { ramfs_listing(); nested_mounts(); deep_chain(12); deep_chain(80);
#if defined(__unix__) || defined(__APPLE__)
        posix_listing();
#endif
    }
    printf("%s source resolver: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures != 0;
}
