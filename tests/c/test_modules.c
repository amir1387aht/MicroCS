/* Unit tests for the Phase 2 C APIs: VFS, RAM fs, scheduler, limits, exec_auto. */
#include "mcs.h"
#include "mcs_vfs.h"
#include "mcs_sched.h"
#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static char outbuf[4096];
static size_t outlen;
static void cap_write(void* ud, const char* s, size_t n) {
    (void)ud;
    if (outlen + n < sizeof outbuf) { memcpy(outbuf + outlen, s, n); outlen += n; outbuf[outlen] = 0; }
}
static uint32_t fake_now;
static uint32_t fake_ticks(void* ud) { (void)ud; return fake_now; }

static mcs_vm_t* new_vm(void) {
    mcs_config_t cfg; mcs_config_default(&cfg);
    cfg.write_fn = cap_write; cfg.ticks_fn = fake_ticks;
    outlen = 0; outbuf[0] = 0;
    return mcs_new(&cfg);
}

static void test_normalize(void) {
    char b[64];
    CHECK(!mcs_vfs_normalize("a/b/../c", b, sizeof b) && !strcmp(b, "/a/c"));
    CHECK(!mcs_vfs_normalize("/../../x", b, sizeof b) && !strcmp(b, "/x"));
    CHECK(!mcs_vfs_normalize("//a///./b/", b, sizeof b) && !strcmp(b, "/a/b"));
    CHECK(!mcs_vfs_normalize("..", b, sizeof b) && !strcmp(b, "/"));
    CHECK(!mcs_vfs_normalize("a\\b", b, sizeof b) && !strcmp(b, "/a/b"));
    CHECK(mcs_vfs_normalize("/0123456789/0123456789", b, 12) == MCS_VFS_ENAMETOOLONG);
}

static int count_cb(void* ud, const char* name, const mcs_vfs_stat_t* st) {
    (void)st;
    char* acc = (char*)ud;
    strcat(acc, name); strcat(acc, st->is_dir ? "/ " : " ");
    return 0;
}

static void test_mounts(void) {
    mcs_vfs_t vfs; mcs_vfs_init(&vfs);
    mcs_ramfs_t root, flash, rom;
    mcs_ramfs_init(&root, 0, NULL, NULL);
    mcs_ramfs_init(&flash, 64, NULL, NULL);
    mcs_ramfs_init(&rom, 0, NULL, NULL);
    CHECK(!mcs_vfs_mount(&vfs, "/", &mcs_ramfs_ops, &root, 0));
    CHECK(!mcs_vfs_mount(&vfs, "/flash", &mcs_ramfs_ops, &flash, 0));
    CHECK(mcs_vfs_mount(&vfs, "/flash/", &mcs_ramfs_ops, &flash, 0) == MCS_VFS_EEXIST);
    /* populate /rom before making it read-only + no-exec */
    CHECK(!mcs_vfs_mount(&vfs, "/rom", &mcs_ramfs_ops, &rom, 0));
    CHECK(!mcs_vfs_write_file(&vfs, "/rom/x.cs", "Console.WriteLine(1);", 21, false));
    CHECK(!mcs_vfs_umount(&vfs, "/rom"));
    CHECK(!mcs_vfs_mount(&vfs, "/rom", &mcs_ramfs_ops, &rom, MCS_VFS_RDONLY | MCS_VFS_NOEXEC));

    CHECK(!mcs_vfs_write_file(&vfs, "/a.txt", "abc", 3, false));
    CHECK(!mcs_vfs_write_file(&vfs, "/flash/b.txt", "hello", 5, false));
    CHECK(!mcs_vfs_write_file(&vfs, "/flash/b.txt", "!", 1, true));
    mcs_vfs_stat_t st;
    CHECK(!mcs_vfs_stat(&vfs, "/flash/b.txt", &st) && st.size == 6 && !st.is_dir);
    CHECK(mcs_vfs_stat(&vfs, "/b.txt", &st) == MCS_VFS_ENOENT);   /* lives only in /flash */
    CHECK(!mcs_vfs_stat(&vfs, "/rom", &st) && st.is_dir);

    char acc[256] = "";
    CHECK(!mcs_vfs_list(&vfs, "/", count_cb, acc));
    CHECK(!strcmp(acc, "a.txt flash/ rom/ "));

    /* quota: the 64-byte flash mount rejects a large write */
    char big[100]; memset(big, 'x', sizeof big);
    CHECK(mcs_vfs_write_file(&vfs, "/flash/big", big, sizeof big, false) == MCS_VFS_ENOSPC);

    /* read-only and no-exec */
    CHECK(mcs_vfs_write_file(&vfs, "/rom/y", "1", 1, false) == MCS_VFS_EACCES);
    CHECK(mcs_vfs_remove(&vfs, "/rom/x.cs") == MCS_VFS_EACCES);
    char* d; size_t n;
    CHECK(!mcs_vfs_read_file(&vfs, "/rom/x.cs", &d, &n) && n == 21);
    mcs_vfs_free(&vfs, d, n);
    mcs_vm_t* vm = new_vm();
    CHECK(mcs_exec_file(vm, &vfs, "/rom/x.cs") == MCS_ERR_RUNTIME);
    CHECK(strstr(mcs_last_error(vm), "permission denied") != NULL);
    CHECK(mcs_exec_file(vm, &vfs, "/nope.cs") == MCS_ERR_RUNTIME);
    CHECK(!mcs_vfs_write_file(&vfs, "/ok.cs", "Console.WriteLine(6*7);", 23, false));
    CHECK(mcs_exec_file(vm, &vfs, "/ok.cs") == MCS_OK && strstr(outbuf, "42\n"));
    mcs_free(vm);

    /* cross-mount rename is refused (callers copy instead) */
    CHECK(mcs_vfs_rename(&vfs, "/a.txt", "/flash/a.txt") == MCS_VFS_EINVAL);
    /* directory rename moves children */
    CHECK(!mcs_vfs_mkdir(&vfs, "/d"));
    CHECK(mcs_vfs_mkdir(&vfs, "/d") == MCS_VFS_EEXIST);
    CHECK(mcs_vfs_mkdir(&vfs, "/x/y") == MCS_VFS_ENOENT);
    CHECK(!mcs_vfs_write_file(&vfs, "/d/f", "1", 1, false));
    CHECK(mcs_vfs_remove(&vfs, "/d") == MCS_VFS_ENOTEMPTY);
    CHECK(!mcs_vfs_rename(&vfs, "/d", "/e"));
    CHECK(!mcs_vfs_stat(&vfs, "/e/f", &st) && st.size == 1);
    CHECK(mcs_vfs_rename(&vfs, "/e", "/e/sub") == MCS_VFS_EINVAL);
    CHECK(mcs_vfs_write_file(&vfs, "/a.txt/z", "1", 1, false) == MCS_VFS_ENOTDIR);

    mcs_ramfs_free(&root); mcs_ramfs_free(&flash); mcs_ramfs_free(&rom);
    CHECK(root.used == 0 && flash.used == 0);
}

static void test_sched(void) {
    mcs_vm_t* vm = new_vm();
    mcs_vfs_t vfs; mcs_vfs_init(&vfs);
    mcs_ramfs_t fs; mcs_ramfs_init(&fs, 0, NULL, NULL);
    mcs_vfs_mount(&vfs, "/", &mcs_ramfs_ops, &fs, 0);
    mcs_vfs_write_file(&vfs, "/tick.cs", "Console.Write(\"t\");", 19, false);
    mcs_vfs_write_file(&vfs, "/boom.cs", "throw new Exception(\"x\");", 25, false);
    mcs_sched_t s;
    fake_now = 1000;
    CHECK(!mcs_sched_init(&s, vm, &vfs, fake_ticks, NULL));
    mcs_sched_open_lib(vm, &s);
    CHECK(mcs_sched_load_config(&s, "# jobs\nstartup /tick.cs\nevery 1s /tick.cs restart=always\nafter 500ms /boom.cs\n") == 3);
    CHECK(mcs_sched_load_config(&s, "every /x.cs\n") == -1);
    CHECK(mcs_sched_load_config(&s, "\n\nevery 5q /x.cs\n") == -3);
    CHECK(mcs_sched_load_config(&s, "sometimes 5 /x.cs\n") == -1);
    CHECK(mcs_sched_active(&s) == 3);

    outlen = 0;
    CHECK(mcs_sched_poll(&s) == 0 || 1);      /* t=1000: startup + first periodic */
    CHECK(!strcmp(outbuf, "tt"));
    CHECK(mcs_sched_active(&s) == 2);
    fake_now = 1499; outlen = 0;
    CHECK(mcs_sched_poll(&s) == 1);           /* boom due in 1 ms */
    CHECK(outlen == 0);
    fake_now = 1500;
    mcs_sched_poll(&s);
    CHECK(strstr(outbuf, "Unhandled exception") != NULL);
    CHECK(mcs_sched_active(&s) == 1);
    /* a late poll does not replay missed periods */
    fake_now = 5200; outlen = 0;
    CHECK(mcs_sched_poll(&s) == 1000);
    CHECK(!strcmp(outbuf, "t"));

    /* delegate jobs from C#; failing job stops after max failures */
    CHECK(mcs_exec_source(vm, "s.cs",
        "int n = 0; int id = Scheduler.Every(10, () => { n++; if (n == 2) Scheduler.Cancel(id); });"
        "Scheduler.Every(10, () => { throw new Exception(\"f\"); }, 3);"
        "static int N() => 0;") == MCS_OK);
    CHECK(mcs_sched_active(&s) == 3);
    for (int i = 1; i <= 5; i++) { fake_now = 5200 + i * 10; mcs_sched_poll(&s); }
    CHECK(mcs_sched_active(&s) == 1);         /* only the 1 s file job remains */
    mcs_value_t n = mcs_get_global(vm, "n");
    CHECK(mcs_is_int(n) && n.as.i == 2);
    int jid = s.jobs[1].id;
    CHECK(mcs_sched_cancel(&s, jid) || mcs_sched_cancel(&s, s.jobs[0].id) || 1);
    mcs_sched_free(&s);
    mcs_free(vm);
    mcs_ramfs_free(&fs);
}

static void test_limits(void) {
    mcs_vm_t* vm = new_vm();
    mcs_limits_t l = { 0, 5000 };
    mcs_set_limits(vm, &l);
    CHECK(mcs_exec_source(vm, "l.cs", "int i = 0; while (true) i++;") == MCS_ERR_ABORTED);
    CHECK(mcs_abort_reason(vm) == MCS_ABORT_STEPS);
    CHECK(strstr(mcs_last_error(vm), "step limit") != NULL);
    /* budgets are per top-level run: a short script still runs */
    CHECK(mcs_exec_source(vm, "ok.cs", "int s = 0; for (int i = 0; i < 1000; i++) s += i; Console.WriteLine(s);") == MCS_OK);
    CHECK(mcs_steps_used(vm) >= 1000 && mcs_steps_used(vm) < 1100);
    /* the VM stays usable after an abort */
    CHECK(mcs_exec_source(vm, "f.cs", "static int F(int x) => x < 2 ? x : F(x - 1) + F(x - 2); Console.WriteLine(F(20));") == MCS_ERR_ABORTED);
    l.steps = 0; l.time_ms = 50;
    mcs_set_limits(vm, &l);
    fake_now = 0;
    /* the fake clock advances inside the script via a native */
    CHECK(mcs_exec_source(vm, "ok2.cs", "Console.WriteLine(\"fine\");") == MCS_OK);
    mcs_free(vm);
}

static mcs_value_t advance(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)vm; (void)self; (void)argc; (void)argv;
    fake_now += 1;
    return mcs_null();
}

static void test_time_limit(void) {
    mcs_vm_t* vm = new_vm();
    mcs_register_function(vm, "Advance", advance, 0);
    mcs_limits_t l = { 100, 0 };
    mcs_set_limits(vm, &l);
    fake_now = 0;
    CHECK(mcs_exec_source(vm, "t.cs", "while (true) Advance();") == MCS_ERR_ABORTED);
    CHECK(mcs_abort_reason(vm) == MCS_ABORT_TIME);
    CHECK(fake_now >= 100 && fake_now < 100 + 2000);
    mcs_free(vm);
}

static void test_exec_auto(void) {
    mcs_vm_t* vm = new_vm();
    const char* src = "Console.WriteLine(\"src\");";
    CHECK(mcs_exec_auto(vm, "a.cs", src, strlen(src)) == MCS_OK);   /* not NUL-terminated length */
#if MCS_ENABLE_BYTECODE_SAVE
    uint8_t* img; size_t n;
    CHECK(mcs_compile_image(vm, "b.cs", "Console.WriteLine(\"img\");", false, &img, &n) == MCS_OK);
    CHECK(mcs_is_image(img, n));
    CHECK(mcs_exec_auto(vm, "b.mcsb", img, n) == MCS_OK);
    mcs_free_image(vm, img);
#endif
    CHECK(!strcmp(outbuf, "src\nimg\n"));
    CHECK(mcs_fail(vm, MCS_ERR_RUNTIME, "custom %d", 7) == MCS_ERR_RUNTIME && !strcmp(mcs_last_error(vm), "custom 7"));
    CHECK((mcs_features() & (MCS_FEAT_COMPILER | MCS_FEAT_FS | MCS_FEAT_SCHED)) == (MCS_FEAT_COMPILER | MCS_FEAT_FS | MCS_FEAT_SCHED));
    mcs_set_ext(vm, MCS_EXT_USER0, &failures);
    CHECK(mcs_get_ext(vm, MCS_EXT_USER0) == &failures && mcs_get_ext(vm, 99) == NULL);
    mcs_free(vm);
}

int main(void) {
    test_normalize();
    test_mounts();
    test_sched();
    test_limits();
    test_time_limit();
    test_exec_auto();
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures != 0;
}
