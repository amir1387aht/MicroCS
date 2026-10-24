/* File memory ownership: exact-size reads and recovery from native OOM.
 * The backend deliberately arms VM allocation failure only after a resource is
 * acquired, so a compiler/call-setup OOM cannot accidentally satisfy the test. */
#include "mcs.h"
#include "mcs_vfs.h"
#include "mcs_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); failures++; } } while (0)
typedef struct {
    size_t actual, stated, offset, alloc_bytes, max_alloc;
    int live_buffers, handles, directories, allocations, lock_depth;
    mcs_vfs_lock_t lock;
    bool fail_growth, fail_shrink, fail_vm, arm_read, arm_write, arm_list;
    mcs_pool_t pool;
} fixture_t;
static const char contents[] = "one\r\ntwo\nthree";
static uint8_t heap[256 * 1024];

static void* temp_alloc(void* ud, void* p, size_t old, size_t n) {
    fixture_t* c = ud;
    if (!n) { CHECK(c->alloc_bytes == old); free(p); c->live_buffers--; c->alloc_bytes = 0; return NULL; }
    c->allocations++;
    if (n > c->max_alloc) c->max_alloc = n;
    if ((p && n > old && c->fail_growth) || (p && n < old && c->fail_shrink)) return NULL;
    void* q = realloc(p, n);
    if (q) { if (!p) c->live_buffers++; c->alloc_bytes = n; }
    return q;
}
static void* vm_alloc(void* ud, void* p, size_t old, size_t n) {
    fixture_t* c = ud;
    if (c->fail_vm && n > old) return NULL;
    return mcs_pool_realloc(&c->pool, p, old, n);
}
static int open_file(void* ud, const char* p, int flags, void** fh) {
    fixture_t* c = ud; (void)p;
    c->handles++; c->offset = 0; *fh = c;
    if ((flags & (MCS_VFS_WRITE | MCS_VFS_APPEND)) && c->arm_write) c->fail_vm = true;
    return 0;
}
static int read_file(void* ud, void* fh, void* buf, size_t n) {
    fixture_t* c = ud; (void)fh;
    if (n > c->actual - c->offset) n = c->actual - c->offset;
    for (size_t i = 0; i < n; i++) ((char*)buf)[i] = contents[(c->offset + i) % (sizeof contents - 1)];
    c->offset += n;
    if (!n && c->arm_read) c->fail_vm = true;
    return (int)n;
}
static int write_file(void* ud, void* fh, const void* buf, size_t n) { (void)ud; (void)fh; (void)buf; return (int)n; }
static int close_file(void* ud, void* fh) { (void)fh; ((fixture_t*)ud)->handles--; return 0; }
static int stat_file(void* ud, const char* p, mcs_vfs_stat_t* st) {
    fixture_t* c = ud;
    st->is_dir = !strcmp(p, "/"); st->size = st->is_dir ? 0 : (uint32_t)c->stated;
    return 0;
}
static int list_dir(void* ud, const char* p, mcs_vfs_list_cb cb, void* data) {
    fixture_t* c = ud; (void)p;
    mcs_vfs_stat_t st = { 12, false };
    c->directories++;
    if (c->arm_list) c->fail_vm = true;
    cb(data, "file.txt", &st);
    c->directories--;
    return 0;
}
static const mcs_vfs_ops_t ops = {
    .open = open_file, .read = read_file, .write = write_file,
    .close = close_file, .stat = stat_file, .list = list_dir
};
static void quiet(void* ud, const char* s, size_t n) { (void)ud; (void)s; (void)n; }
static void lock_enter(void* ud) { fixture_t* c = ud; CHECK(c->lock_depth == 0); c->lock_depth++; }
static void lock_leave(void* ud) { fixture_t* c = ud; CHECK(c->lock_depth == 1); c->lock_depth--; }
static void init(fixture_t* c, mcs_vfs_t* vfs, size_t actual, size_t stated) {
    memset(c, 0, sizeof *c); c->actual = actual; c->stated = stated;
    mcs_vfs_init(vfs); vfs->realloc_fn = temp_alloc; vfs->alloc_ud = c;
    c->lock = (mcs_vfs_lock_t){ lock_enter, lock_leave, c }; vfs->lock = &c->lock;
    CHECK(mcs_vfs_mount(vfs, "/", &ops, c, 0) == 0);
}
static void test_read_sizes(void) {
    fixture_t c; mcs_vfs_t vfs; char* data; size_t len;
    const size_t sizes[] = { 0, 1, sizeof contents - 1, 32768 };
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        init(&c, &vfs, sizes[i], sizes[i]); c.fail_growth = true;
        CHECK(mcs_vfs_read_file(&vfs, "/file", &data, &len) == 0);
        CHECK(data && len == sizes[i]);
        CHECK(c.allocations == 1 && c.max_alloc == sizes[i] + 1);
        if (data) { CHECK(data[len] == 0); mcs_vfs_free(&vfs, data, len); }
        CHECK(c.live_buffers == 0 && c.handles == 0);
    }
    /* Size changed between stat and read (including a zero-size stat). */
    const size_t stated[] = { 0, 2, 200 };
    for (size_t i = 0; i < sizeof stated / sizeof stated[0]; i++) {
        init(&c, &vfs, sizeof contents - 1, stated[i]);
        CHECK(mcs_vfs_read_file(&vfs, "/file", &data, &len) == 0);
        CHECK(data && len == sizeof contents - 1);
        if (data) { CHECK(!strcmp(data, contents)); mcs_vfs_free(&vfs, data, len); }
        CHECK(c.live_buffers == 0 && c.handles == 0);
    }
    init(&c, &vfs, 100, 2); c.fail_growth = true;
    CHECK(mcs_vfs_read_file(&vfs, "/file", &data, &len) == MCS_VFS_ENOMEM);
    CHECK(!data && !len && c.live_buffers == 0 && c.handles == 0);
    init(&c, &vfs, 2, 100); c.fail_shrink = true;
    CHECK(mcs_vfs_read_file(&vfs, "/file", &data, &len) == MCS_VFS_ENOMEM);
    CHECK(!data && !len && c.live_buffers == 0 && c.handles == 0);
}
static void test_oom_recovery(void) {
    fixture_t c; mcs_vfs_t vfs;
    init(&c, &vfs, sizeof contents - 1, sizeof contents - 1);
    mcs_pool_init(&c.pool, heap, sizeof heap);
    mcs_config_t cfg; mcs_config_default(&cfg);
    cfg.realloc_fn = vm_alloc; cfg.alloc_ud = &c; cfg.write_fn = quiet;
    mcs_vm_t* vm = mcs_new(&cfg); CHECK(vm != NULL); if (!vm) return;
    mcs_fs_open_lib(vm, &vfs);
    const char* scripts[] = {
        "File.ReadAllText(\"/file\");", "File.ReadAllLines(\"/file\");",
        "File.ReadAllBytes(\"/file\");", "File.WriteAllLines(\"/out\", new int[] {1234567, 9876543});",
        "File.AppendAllLines(\"/out\", new int[] {1234567});", "Directory.GetFiles(\"/\");"
    };
    for (int repeat = 0; repeat < 25; repeat++) {
        for (size_t i = 0; i < sizeof scripts / sizeof scripts[0]; i++) {
            c.arm_read = i < 3; c.arm_write = i == 3 || i == 4; c.arm_list = i == 5;
            CHECK(mcs_exec_source(vm, "oom.cs", scripts[i]) == MCS_ERR_MEMORY);
            CHECK(c.fail_vm);  /* reached the resource boundary, not compile OOM */
            CHECK(c.live_buffers == 0 && c.handles == 0 && c.directories == 0 && c.lock_depth == 0);
            c.fail_vm = c.arm_read = c.arm_write = c.arm_list = false;
            mcs_gc(vm);
            CHECK(mcs_exec_source(vm, "recovery.cs", "File.WriteAllText(\"/out\", \"ok\"); Console.WriteLine(42);") == MCS_OK);
            CHECK(c.live_buffers == 0 && c.handles == 0);
        }
    }
    CHECK(mcs_exec_source(vm, "ok.cs", "Console.WriteLine(File.ReadAllText(\"/file\")); Console.WriteLine(File.ReadAllLines(\"/file\").Length); Console.WriteLine(File.ReadAllBytes(\"/file\").Length); Console.WriteLine(Directory.GetFiles(\"/\").Length);") == MCS_OK);
    CHECK(c.live_buffers == 0 && c.handles == 0 && c.directories == 0 && c.lock_depth == 0);
    mcs_free(vm);
    CHECK(c.pool.used == 0);
}
/* Exercise the actual Studio shell protocol, with file data sharing a fixed
 * heap with the VM. Replacing files must not retain .part buffers/handles. */
static const char* serial_input;
static char serial_output[2048];
static size_t serial_output_len;
static int serial_read(void* ud, uint8_t* buf, size_t n, uint32_t timeout) {
    (void)ud; (void)timeout;
    size_t len = strlen(serial_input); if (len > n) len = n;
    memcpy(buf, serial_input, len); serial_input += len;
    return (int)len;
}
static void serial_write(void* ud, const char* buf, size_t n) {
    (void)ud;
    size_t room = sizeof serial_output - 1 - serial_output_len;
    if (n > room) n = room;
    memcpy(serial_output + serial_output_len, buf, n);
    serial_output_len += n; serial_output[serial_output_len] = 0;
}
static void command(mcs_runtime_t* rt, const char* input) {
    serial_input = input; serial_output_len = 0; serial_output[0] = 0;
    CHECK(mcs_runtime_step(rt, 0));
    CHECK(strstr(serial_output, "\x04" "OK\n") != NULL);
}
static void test_upload_churn(void) {
    static mcs_runtime_t rt;
    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap; cfg.heap_size = 128 * 1024; cfg.ramfs_size = 16 * 1024;
    cfg.run_boot_scripts = false; cfg.mode = MCS_RUNTIME_SHELL;
    cfg.console = (mcs_transport_t){ serial_read, serial_write, NULL };
    serial_input = ""; serial_output_len = 0;
    CHECK(mcs_runtime_start(&rt, &cfg) == 0);
    if (!rt.vm) return;
    command(&rt, "exec int retained = 42;\n");
    size_t warm_vm = 0, warm_runtime = 0;
    for (int i = 0; i < 100; i++) {
        char upload[4300];
        size_t len = (size_t)(512 + (i % 8) * 512);
        int prefix = snprintf(upload, sizeof upload, "put /churn.cs %u 1024\n", (unsigned)len);
        const char* source = "Console.WriteLine(retained); //";
        memcpy(upload + prefix, source, strlen(source));
        memset(upload + prefix + strlen(source), 'x', len - strlen(source));
        upload[prefix + len] = 0;
        command(&rt, upload);
        CHECK(!*serial_input);
        mcs_vfs_stat_t st;
        CHECK(mcs_vfs_stat(&rt.vfs, "/churn.cs.part", &st) == MCS_VFS_ENOENT);
        /* This test intentionally shares a REPL global across file executions.
         * Application replacement is covered separately by test_app_replace. */
        command(&rt, "run /churn.cs shared\n");
        CHECK(strstr(serial_output, "42\n") != NULL);
        mcs_mem_stats_t memory; mcs_mem_stats(rt.vm, &memory);
        size_t runtime_used = rt.pool.used - rt.ramfs.used;
        if (!i) { warm_vm = memory.bytes_in_use; warm_runtime = runtime_used; }
        else {
            /* The first run lazily creates normal runtime objects. Later Studio
             * upload/run cycles must not retain compiler graphs, source buffers
             * or allocator blocks. Pool placement may vary by a few aligned
             * blocks as RAMFS files change size, but it must not creep. */
            CHECK(memory.bytes_in_use == warm_vm);
            CHECK(runtime_used <= warm_runtime + 256);
        }
    }
    command(&rt, "rm /churn.cs\n");
    CHECK(rt.ramfs.used == 0);
    mcs_runtime_stop(&rt);
    CHECK(rt.pool.used == 0);
}
int main(void) {
    test_read_sizes(); test_oom_recovery(); test_upload_churn();
    printf("%s FS memory: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures != 0;
}
