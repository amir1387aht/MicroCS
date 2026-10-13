/* Link/render/lifecycle tests against real LVGL, not mocked headers. */
#include "mcs_lvgl.h"
#include "mcs_hal.h"
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks, failures, flushes, sink_error;
static unsigned long bytes_seen;
static unsigned long hash;
static int expect_red;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static void quiet(void* ud, const char* s, size_t n) { (void)ud; (void)s; (void)n; }
static int sink(void* ud, int x, int y, int w, int h, const void* pixels, size_t stride, size_t bytes) {
    (void)ud;
    CHECK(x >= 0 && y >= 0 && w > 0 && h > 0);
    CHECK(stride >= (size_t)w * 2); CHECK(bytes == stride * (size_t)h);
    const unsigned char* p = (const unsigned char*)pixels;
    for (size_t i = 0; i < bytes; i++) hash = (hash * 33) ^ p[i];
    if (expect_red) {
        for (int row = 0; row < h; row++) for (int col = 0; col < w; col++) {
            uint16_t pixel;
            memcpy(&pixel, p + (size_t)row * stride + (size_t)col * 2, sizeof pixel);
            CHECK(pixel == 0xF800); /* true RGB565, not just a flush callback */
        }
    }
    bytes_seen += bytes; flushes++;
    return sink_error;
}
static mcs_vm_t* make_vm(void) {
    mcs_config_t c; mcs_config_default(&c); c.write_fn = quiet; c.error_fn = quiet;
    return mcs_new(&c);
}
static void run(mcs_vm_t* vm, const char* s) {
    mcs_result_t r = mcs_exec_source(vm, "lvgl-test.cs", s);
    if (r != MCS_OK) printf("unexpected script error: %s\n", mcs_last_error(vm));
    CHECK(r == MCS_OK);
}
int main(void) {
    CHECK(!lv_is_initialized());
    mcs_vm_t* vm = make_vm(); CHECK(vm != NULL);
    CHECK(mcs_capability_has(vm, "LVGL"));
    CHECK(!lv_is_initialized());  /* capability lookup must not initialize LVGL */
    mcs_lvgl_backend_t backend = { sink, NULL, NULL };
    CHECK(!mcs_lvgl_set_backend(vm, &backend));
    expect_red = 1;
    uint32_t scope = mcs_lvgl_scope_begin(vm);
    run(vm, "var g = new LvGui(33, 17); g.Screen.SetStyle(\"bg_color\", 0xFF0000); g.Pump(40);");
    CHECK(lv_is_initialized()); CHECK(flushes > 0); CHECK(bytes_seen >= 33u * 17u * 2u); CHECK(hash != 0);
    CHECK(mcs_lvgl_set_backend(vm, NULL) == -1);
    run(vm, "try { var duplicate = new LvGui(10, 10); throw new Exception(\"accepted\"); } catch (InvalidOperationException e) {} g.Pump(40);");
    mcs_lvgl_scope_end(vm, scope); CHECK(!lv_is_initialized()); expect_red = 0;
    run(vm, "try { g.Pump(40); throw new Exception(\"stale accepted\"); } catch (ObjectDisposedException e) {}");
    CHECK(!mcs_lvgl_set_backend(vm, NULL));
    /* Repeated allocation/destruction + GC: no retained LVGL instance. */
    for (int i = 0; i < 30; i++) {
        scope = mcs_lvgl_scope_begin(vm);
        run(vm, "var repeat = new LvGui(48, 32); var root = repeat.Screen; var l = root.Create(\"label\"); l.Text = \"test\"; repeat.Pump(40);");
        mcs_lvgl_scope_end(vm, scope); mcs_gc(vm); CHECK(!lv_is_initialized());
    }
    /* Queue is bounded; overflow count is truthful; deletion invalidates queued handles. */
    run(vm, "var q = new LvGui(50, 30); var button = q.Screen.Create(\"button\"); for (int i = 0; i < 100; i++) button.SendEvent(LvGui.Clicked); if (q.DroppedEvents == 0) throw new Exception(\"no overflow\"); button.Delete(); if (q.PollEvent() != null) throw new Exception(\"stale queue\"); q.Dispose();");
    /* Flush failures are errors, not fake success; flush-ready is still signaled. */
    CHECK(!mcs_lvgl_set_backend(vm, &backend)); sink_error = -7;
    scope = mcs_lvgl_scope_begin(vm);
    run(vm, "var bad = new LvGui(20, 20); try { bad.Pump(40); throw new Exception(\"accepted I/O failure\"); } catch (IOException e) {}");
    mcs_lvgl_scope_end(vm, scope); CHECK(!lv_is_initialized()); sink_error = 0;
    /* VM finalization closes an undisposed live GUI before releasing VM buffers. */
    run(vm, "var last = new LvGui(20, 20); last.Screen.Create(\"label\").Text = \"bye\"; last.Pump(40);");
    mcs_free(vm); CHECK(!lv_is_initialized());
    /* External LVGL is never silently reinitialized/deinitialized. */
    lv_init(); vm = make_vm();
    run(vm, "try { var stolen = new LvGui(20, 20); throw new Exception(\"external stolen\"); } catch (InvalidOperationException e) {}");
    mcs_free(vm); CHECK(lv_is_initialized()); lv_deinit();
    printf("LVGL %d.%d.%d real headless: %d checks, %d failures\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, checks, failures);
    return failures != 0;
}
