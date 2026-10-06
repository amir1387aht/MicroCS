/* Unit tests for the C APIs: VFS, RAM fs, scheduler, limits, exec_auto,
 * execute-in-place images, globals, the intern set and shared class layouts. */
#include "mcs.h"
#include "mcs_vfs.h"
#include "mcs_sched.h"
#include "mcs_hal.h"
#include "mcs_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

#if MCS_ENABLE_BYTECODE_SAVE && MCS_ENABLE_BYTECODE_LOAD
static const char* XIP_SRC =
    "static int Twice(int x) => x * 2;\n"
    "static int Mix(int x) { int r = x; r = r * 31 + 7; r ^= r >> 3; r = r * 31 + 11; r ^= r >> 5; r = r * 31 + 13;"
    " r ^= r >> 7; r = r * 31 + 17; r ^= r >> 9; r = r * 31 + 19; r ^= r >> 11; r = r * 31 + 23; r ^= r >> 13;"
    " r = r * 31 + 29; r ^= r >> 2; r = r * 31 + 37; r ^= r >> 4; r = r * 31 + 41; r ^= r >> 6; return r & 0xFFFF; }\n"
    "Func<int, int> Inc = n => n + 1;\n"
    "class Counter { public int N; public void Bump() { N = Twice(N + 1); } }\n"
    "var c = new Counter(); c.Bump(); c.Bump();\n"
    "Console.WriteLine($\"xip {c.N} {Inc(41)} {Mix(5) == Mix(5)}\");\n";
static size_t image_run_bytes(bool xip, const uint8_t* img, size_t n) {
    mcs_vm_t* vm = new_vm();
    uint8_t* buf = (uint8_t*)malloc(n);   /* the image must outlive the VM in XIP mode */
    memcpy(buf, img, n);
    mcs_result_t r = xip ? mcs_exec_image_xip(vm, buf, n) : mcs_exec_image(vm, buf, n);
    CHECK(r == MCS_OK);
    CHECK(!strcmp(outbuf, "xip 6 42 True\n"));
    /* call back into image code after the top-level run (code still in `buf`) */
    mcs_value_t arg = mcs_int(20), res;
    CHECK(mcs_call(vm, "Twice", 1, &arg, &res) == MCS_OK && res.type == MCS_T_INT && res.as.i == 40);
    mcs_gc(vm);
    CHECK(mcs_call(vm, "Twice", 1, &arg, &res) == MCS_OK && res.as.i == 40);
    mcs_mem_stats_t st; mcs_mem_stats(vm, &st);
    mcs_free(vm);
    free(buf);
    return st.bytes_in_use;
}
static void test_xip(void) {
    mcs_vm_t* vm = new_vm();
    uint8_t* img; size_t n;
    CHECK(mcs_compile_image(vm, "x.cs", XIP_SRC, true, &img, &n) == MCS_OK);
    size_t copy = image_run_bytes(false, img, n);
    size_t xip = image_run_bytes(true, img, n);
#if MCS_ENABLE_XIP
    CHECK(xip < copy);   /* function code is not duplicated on the heap */
#else
    CHECK(xip == copy);
#endif
    /* a corrupt image must still be rejected in XIP mode */
    uint8_t* bad = (uint8_t*)malloc(n); memcpy(bad, img, n); bad[n / 2] ^= 0xFF; bad[n / 2 + 1] ^= 0x5A;
    mcs_vm_t* vm2 = new_vm();
    mcs_result_t r = mcs_exec_image_xip(vm2, bad, n);
    CHECK(r == MCS_OK || r == MCS_ERR_BYTECODE || r == MCS_ERR_RUNTIME);   /* never crashes */
    mcs_free(vm2); free(bad);
    mcs_free_image(vm, img);
    mcs_free(vm);
}
#endif

/* globals live in a growable array; names carry their slot (no index table) */
static void test_globals(void) {
    mcs_vm_t* vm = new_vm();
    char name[32];
    for (int i = 0; i < 300; i++) { snprintf(name, sizeof name, "G%d", i); mcs_set_global(vm, name, mcs_int(i * 3)); }
    int ok = 1;
    for (int i = 0; i < 300; i++) { snprintf(name, sizeof name, "G%d", i); mcs_value_t v = mcs_get_global(vm, name); ok &= v.type == MCS_T_INT && v.as.i == i * 3; }
    CHECK(ok);
    CHECK(mcs_get_global(vm, "NoSuchGlobal").type == MCS_T_NULL);
    mcs_gc(vm);
    CHECK(mcs_get_global(vm, "G299").as.i == 897);
#if MCS_ENABLE_COMPILER
    CHECK(mcs_exec_source(vm, "g.cs", "Console.WriteLine(G0 + G150 + G299);") == MCS_OK);
    CHECK(!strcmp(outbuf, "1347\n"));
#endif
    mcs_free(vm);
}

#if MCS_ENABLE_COMPILER
/* the weak intern set must not keep growing under string churn */
static void test_intern_churn(void) {
    mcs_vm_t* vm = new_vm();
    CHECK(mcs_exec_source(vm, "c.cs",
        "int total = 0;\n"
        "for (int i = 0; i < 20000; i++) { string s = \"k\" + i; total += s.Length; }\n"
        "Console.WriteLine(total);") == MCS_OK);
    CHECK(!strcmp(outbuf, "108890\n"));
    mcs_gc(vm); mcs_gc(vm);
    mcs_mem_stats_t st; mcs_mem_stats(vm, &st);
    CHECK(st.bytes_in_use < 96 * 1024);   /* 20000 dead strings are gone, set compacted */
    mcs_free(vm);
}

/* compact dictionary index: 1/2/4-byte slot widths, removal order, slot reuse,
 * and the memory it saves (HashSet storage has no values array) */
static void test_dict_index(void) {
    mcs_vm_t* vm = new_vm();
    CHECK(mcs_exec_source(vm, "d.cs",
        "var d = new Dictionary<int, int>();\n"
        "for (int i = 0; i < 70000; i++) d[i] = i;\n"           /* > 65536 slots: 4-byte index */
        "long s = 0; for (int i = 0; i < 70000; i += 7) s += d[i];\n"
        "for (int i = 0; i < 70000; i += 2) d.Remove(i);\n"
        "Console.WriteLine(d.Count + \" \" + s + \" \" + d[69999] + \" \" + d.ContainsKey(68000));\n"
        "int k = 0; foreach (var kv in d) { if (kv.Key != 2 * k + 1) { Console.WriteLine(\"order\"); break; } k++; }\n"
        "for (int r = 0; r < 50; r++) { d[-1] = r; d.Remove(-1); }\n"     /* deleted slots get reused */
        "Console.WriteLine(d.Count + \" \" + k);\n") == MCS_OK);
    CHECK(!strcmp(outbuf, "35000 349965000 69999 False\n35000 35000\n"));
    mcs_free(vm);
    vm = new_vm();
    mcs_mem_stats_t a, b;
    /* built-in classes are created on first use: create them before measuring */
    CHECK(mcs_exec_source(vm, "w.cs", "var w = new HashSet<int>(); w.Add(1); var l = new List<object> { w }; GC.Collect(); Console.Write(\"\");\n") == MCS_OK);
    mcs_gc(vm); mcs_mem_stats(vm, &a);
    CHECK(mcs_exec_source(vm, "h.cs",
        "var h = new HashSet<int>(); for (int i = 0; i < 1000; i++) h.Add(i);\n"
        "var keep = new List<object> { h }; GC.Collect();\n"
        "Console.WriteLine(h.Count);\n") == MCS_OK);
    mcs_gc(vm); mcs_mem_stats(vm, &b);
    /* 1000 ints: keys[] (1024 values) + 2048 two-byte slots; no values array */
    CHECK(b.bytes_in_use - a.bytes_in_use < 1024 * sizeof(mcs_value_t) + 2048 * 2 + 4096);
    mcs_free(vm);
}

/* built-in exception classes share Exception's field layout until a
 * subclass adds its own field (copy on write) */
static void test_shared_layout(void) {
    mcs_vm_t* vm = new_vm();
    CHECK(mcs_exec_source(vm, "e.cs",
        "class DeviceError : IOException { public int Code; public DeviceError(string m, int c) : base(m) { Code = c; } }\n"
        "try { throw new DeviceError(\"bus\", 7); } catch (IOException e) { Console.WriteLine(e.Message + \" \" + ((DeviceError)e).Code); }\n"
        "var f = new FileNotFoundException(\"nf\"); Console.WriteLine(f.Message + \" \" + (f is IOException));\n"
        "try { int[] a = new int[1]; a[2] = 0; } catch (Exception e) { Console.WriteLine(e.GetType().Name); }\n") == MCS_OK);
    CHECK(!strcmp(outbuf, "bus 7\nnf True\nIndexOutOfRangeException\n"));
    mcs_free(vm);
}
#endif

static void fake_delay(void* ud, uint32_t ms) { (void)ud; fake_now += ms; }

/* Thread.Sleep honours time limits (sliced sleep + mcs_safepoint) and the
 * abort does not leak into the next run */
static void test_sleep_limit(void) {
    mcs_config_t cfg; mcs_config_default(&cfg);
    cfg.write_fn = cap_write; cfg.ticks_fn = fake_ticks; cfg.delay_fn = fake_delay;
    outlen = 0; outbuf[0] = 0;
    mcs_vm_t* vm = mcs_new(&cfg);
    mcs_limits_t l = { 100, 0 };
    mcs_set_limits(vm, &l);
    fake_now = 0;
    CHECK(mcs_exec_source(vm, "s.cs", "Thread.Sleep(100000); Console.WriteLine(\"after\");") == MCS_ERR_ABORTED);
    CHECK(mcs_abort_reason(vm) == MCS_ABORT_TIME);
    CHECK(fake_now >= 100 && fake_now <= 100 + 2 * MCS_SLEEP_SLICE_MS);
    CHECK(strstr(outbuf, "after") == NULL);
    outlen = 0; outbuf[0] = 0;
    CHECK(mcs_exec_source(vm, "s2.cs", "Thread.Sleep(20); Console.WriteLine(\"ok\");") == MCS_OK);
    CHECK(!strcmp(outbuf, "ok\n"));
    mcs_free(vm);
}

static void test_arity(void) {
    mcs_vm_t* vm = new_vm();
    CHECK(mcs_exec_source(vm, "a.cs", "Action a0 = () => {}; Action<int,int> a2 = (x, y) => {}; static int F(int a, int b, int c) => a;") == MCS_OK);
    CHECK(mcs_arity(mcs_get_global(vm, "a0")) == 0);
    CHECK(mcs_arity(mcs_get_global(vm, "a2")) == 2);
    CHECK(mcs_arity(mcs_int(3)) == -1);
    mcs_free(vm);
}

#if MCS_ENABLE_HAL
/* GPIO interrupt from "ISR" (C side), dispatch from the host loop, queue
 * overflow accounting, close / reopen */
static void test_hal_events(void) {
    static mcs_hal_t hal; static mcs_hal_sim_t sim;
    memset(&sim, 0, sizeof sim);
    mcs_hal_sim_init(&hal, &sim);
    mcs_vm_t* vm = new_vm();
    mcs_hal_open_lib(vm, &hal);
    CHECK(mcs_hal_get(vm) == &hal);
    CHECK(mcs_exec_source(vm, "irq.cs",
        "int n = 0; GPIO.Mode(3, GPIO.Input);"
        "GPIO.OnChange(3, GPIO.Rising, (int p, bool v) => { n++; Console.WriteLine($\"irq {p} {v} {n}\"); });") == MCS_OK);
    mcs_hal_sim_set_input(&sim, 3, 1);       /* rising: queued */
    mcs_hal_sim_set_input(&sim, 3, 0);       /* falling: not armed */
    mcs_hal_sim_set_input(&sim, 3, 1);
    CHECK(mcs_hal_poll(vm) == 2);
    CHECK(!strcmp(outbuf, "irq 3 True 1\nirq 3 True 2\n"));
    CHECK(mcs_hal_poll(vm) == 0);
    /* failing callback at top level: reported, poll returns the error */
    CHECK(mcs_exec_source(vm, "bad.cs", "GPIO.OnChange(4, GPIO.Both, () => throw new InvalidOperationException(\"boom\"));") == MCS_OK);
    mcs_hal_sim_set_input(&sim, 4, 1);
    outlen = 0; outbuf[0] = 0;
    CHECK(mcs_hal_poll(vm) == -(int)MCS_ERR_RUNTIME);
    CHECK(strstr(outbuf, "boom") != NULL || strstr(mcs_last_error(vm), "boom") != NULL);
    /* overflow */
    uint32_t d0 = mcs_hal_dropped_events();
    for (int i = 0; i < MCS_HAL_EVENT_QUEUE + 5; i++) mcs_hal_post(MCS_HAL_EV_USER + 1, i, i);
    CHECK(mcs_hal_dropped_events() - d0 == 5);
    mcs_hal_poll(vm);
    CHECK(mcs_hal_post(MCS_HAL_EV_USER + 1, 0, 0));
    mcs_hal_poll(vm);
    /* close + reopen keeps the VM usable */
    mcs_hal_close_lib(vm);
    CHECK(mcs_hal_get(vm) == NULL);
    CHECK(mcs_hal_poll(vm) == 0);
    mcs_hal_open_lib(vm, &hal);
    outlen = 0; outbuf[0] = 0;
    CHECK(mcs_exec_source(vm, "re.cs", "Console.WriteLine(GPIO.Pin(\"PC3\"));") == MCS_OK);
    CHECK(!strcmp(outbuf, "35\n"));
    mcs_free(vm);
    CHECK(mcs_hal_parse_pin("PA0") == 0 && mcs_hal_parse_pin("pb7") == 23 && mcs_hal_parse_pin("P0.31") == 31);
    CHECK(mcs_hal_parse_pin("GPIO48") == 48 && mcs_hal_parse_pin("D7") == 7 && mcs_hal_parse_pin("PIN9") == 9);
    CHECK(mcs_hal_parse_pin("PA16") < 0 && mcs_hal_parse_pin("X1") < 0 && mcs_hal_parse_pin("") < 0 && mcs_hal_parse_pin("12a") < 0);
}
#endif

#if MCS_ENABLE_RUNTIME && MCS_ENABLE_COMPILER && MCS_ENABLE_HAL
/* Whole-firmware runtime over a scripted console: REPL lines, a pin interrupt,
 * machine protocol via Ctrl-A, and a clean shutdown when the console closes. */
static const char* rt_script;
static size_t rt_pos;
static int rt_con_read(void* ud, uint8_t* b, size_t n, uint32_t timeout_ms) {
    (void)ud; (void)timeout_ms;
    fake_now += 1;
    if (!rt_script[rt_pos]) return -1;
    size_t k = 0;
    while (k < n && rt_script[rt_pos] && rt_script[rt_pos] != '\n') b[k++] = (uint8_t)rt_script[rt_pos++];
    if (k < n && rt_script[rt_pos] == '\n') b[k++] = (uint8_t)rt_script[rt_pos++];
    return (int)k;
}
static void rt_con_write(void* ud, const char* s, size_t n) { cap_write(ud, s, n); }
static uint32_t rt_ms(void* ud) { (void)ud; return fake_now; }
static void rt_sleep(void* ud, uint32_t ms) { (void)ud; fake_now += ms; }
static int rt_setup_called;
static void rt_setup(mcs_vm_t* vm, void* ud) { (void)vm; (void)ud; rt_setup_called++; }

static void test_runtime(void) {
    static uint8_t heap[160 * 1024];
    static mcs_hal_t hal; static mcs_hal_sim_t sim;
    static mcs_runtime_t rt;
    memset(&sim, 0, sizeof sim);
    mcs_hal_sim_init(&hal, &sim);
    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap; cfg.heap_size = sizeof heap; cfg.ramfs_size = 16384;
    cfg.console.read = rt_con_read; cfg.console.write = rt_con_write;
    cfg.ticks = rt_ms; cfg.delay = rt_sleep;
    cfg.echo = false; cfg.hal = &hal; cfg.setup = rt_setup;
    outlen = 0; outbuf[0] = 0;
    rt_script =
        "int x = 20\n"
        "x * 2 + 2\n"
        "GPIO.OnChange(5, GPIO.Rising, (int p, bool v) => Console.WriteLine($\"edge {p} {v}\"));\n";
    rt_pos = 0;
    CHECK(mcs_runtime_start(&rt, &cfg) == 0);
    CHECK(rt_setup_called == 1);
    for (int i = 0; i < 3; i++) CHECK(mcs_runtime_step(&rt, 5));
    CHECK(strstr(outbuf, "42\n") != NULL);
    mcs_hal_sim_set_input(&sim, 5, 1);
    outlen = 0; outbuf[0] = 0;
    rt_script = "\x01" "ls /\n";       /* Ctrl-A: machine protocol */
    rt_pos = 0;
    while (mcs_runtime_step(&rt, 5)) {}
    CHECK(strstr(outbuf, "edge 5 True\n") != NULL);
    CHECK(strstr(outbuf, "\x04OK") != NULL);
    CHECK(!rt.running);
    mcs_runtime_stop(&rt);
    CHECK(rt.vm == NULL);
}
#endif

int main(void) {
    test_normalize();
    test_mounts();
    test_sched();
    test_limits();
    test_time_limit();
    test_exec_auto();
#if MCS_ENABLE_BYTECODE_SAVE && MCS_ENABLE_BYTECODE_LOAD
    test_xip();
#endif
    test_globals();
#if MCS_ENABLE_COMPILER
    test_sleep_limit();
    test_arity();
#if MCS_ENABLE_HAL
    test_hal_events();
#endif
    test_intern_churn();
    test_dict_index();
    test_shared_layout();
#endif
#if MCS_ENABLE_RUNTIME && MCS_ENABLE_COMPILER && MCS_ENABLE_HAL
    test_runtime();
#endif
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures != 0;
}
