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
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)
static void quiet(void *ud, const char *s, size_t n) {
    (void)ud;
    (void)s;
    (void)n;
}
static int sink(void *ud, int x, int y, int w, int h, const void *pixels, size_t stride,
                size_t bytes) {
    (void)ud;
    CHECK(x >= 0 && y >= 0 && w > 0 && h > 0);
    CHECK(stride >= (size_t)w * 2);
    CHECK(bytes == stride * (size_t)h);
    const unsigned char *p = (const unsigned char *)pixels;
    for (size_t i = 0; i < bytes; i++)
        hash = (hash * 33) ^ p[i];
    if (expect_red) {
        for (int row = 0; row < h; row++)
            for (int col = 0; col < w; col++) {
                uint16_t pixel;
                memcpy(&pixel, p + (size_t)row * stride + (size_t)col * 2, sizeof pixel);
                CHECK(pixel == 0xF800); /* true RGB565, not just a flush callback */
            }
    }
    bytes_seen += bytes;
    flushes++;
    return sink_error;
}
static size_t tracked_bytes, fail_large;
static int fail_next;
static void *tracked_alloc(void *ud, void *p, size_t old, size_t n) {
    (void)ud;
    if (!n) {
        free(p);
        tracked_bytes -= old;
        return NULL;
    }
    if (fail_next) {
        fail_next = 0;
        return NULL;
    }
    if (fail_large && n > fail_large)
        return NULL;
    void *out = realloc(p, n);
    if (out)
        tracked_bytes = tracked_bytes - old + n;
    return out;
}
static mcs_value_t fail_next_native(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)vm;
    (void)self;
    (void)argc;
    (void)argv;
    fail_next = 1;
    return mcs_null();
}
static const mcs_reg_t failure_fns[] = {MCS_FN("Next", fail_next_native, 0), MCS_REG_END};
static mcs_vm_t *make_vm(void) {
    mcs_config_t c;
    mcs_config_default(&c);
    c.write_fn = quiet;
    c.error_fn = quiet;
    c.realloc_fn = tracked_alloc;
    return mcs_new(&c);
}
static void run(mcs_vm_t *vm, const char *s) {
    mcs_result_t r = mcs_exec_source(vm, "lvgl-test.cs", s);
    if (r != MCS_OK)
        printf("unexpected script error: %s\n", mcs_last_error(vm));
    CHECK(r == MCS_OK);
}
int main(void) {
    CHECK(!lv_is_initialized());
    mcs_vm_t *vm = make_vm();
    CHECK(vm != NULL);
    CHECK(mcs_capability_has(vm, "LVGL"));
    CHECK(mcs_capability_has(vm, "LVGL.Font"));
    run(vm, "if(!LvGui.HasFont(\"Default\") || LvGui.HasFont(\"missing_font\")) throw new "
            "Exception(\"font probe\");");
    CHECK(!lv_is_initialized()); /* capability/font lookup must not initialize LVGL */
    mcs_lvgl_backend_t backend = {sink, NULL, NULL};
    CHECK(!mcs_lvgl_set_backend(vm, &backend));
    expect_red = 1;
    uint32_t scope = mcs_lvgl_scope_begin(vm);
    run(vm, "var g = new LvGui(33, 17); g.Screen.SetStyle(\"bg_color\", 0xFF0000); g.Pump(40);");
    CHECK(lv_is_initialized());
    CHECK(flushes > 0);
    CHECK(bytes_seen >= 33u * 17u * 2u);
    CHECK(hash != 0);
    CHECK(mcs_lvgl_set_backend(vm, NULL) == -1);
    run(vm, "try { var duplicate = new LvGui(10, 10); throw new Exception(\"accepted\"); } catch "
            "(InvalidOperationException e) {} g.Pump(40);");
    mcs_lvgl_scope_end(vm, scope);
    CHECK(!lv_is_initialized());
    expect_red = 0;
    run(vm, "try { g.Pump(40); throw new Exception(\"stale accepted\"); } catch "
            "(ObjectDisposedException e) {}");
    CHECK(!mcs_lvgl_set_backend(vm, NULL));
    /* Repeated allocation/destruction + GC: no retained LVGL instance. */
    for (int i = 0; i < 30; i++) {
        scope = mcs_lvgl_scope_begin(vm);
        run(vm, "var repeat = new LvGui(48, 32); var root = repeat.Screen; var l = "
                "root.Create(\"label\"); l.Text = \"test\"; repeat.Pump(40);");
        mcs_lvgl_scope_end(vm, scope);
        mcs_gc(vm);
        CHECK(!lv_is_initialized());
    }
    /* Queue is bounded; overflow count is truthful; deletion invalidates queued handles. */
    run(vm, "var q = new LvGui(50, 30); var button = q.Screen.Create(\"button\"); for (int i = 0; "
            "i < 100; i++) button.SendEvent(LvGui.Clicked); if (q.DroppedEvents == 0) throw new "
            "Exception(\"no overflow\"); button.Delete(); if (q.PollEvent() != null) throw new "
            "Exception(\"stale queue\"); q.Dispose();");
    /* Flush failures are errors, not fake success; flush-ready is still signaled. */
    CHECK(!mcs_lvgl_set_backend(vm, &backend));
    sink_error = -7;
    scope = mcs_lvgl_scope_begin(vm);
    run(vm, "var bad = new LvGui(20, 20); try { bad.Pump(40); throw new Exception(\"accepted I/O "
            "failure\"); } catch (IOException e) {}");
    mcs_lvgl_scope_end(vm, scope);
    CHECK(!lv_is_initialized());
    sink_error = 0;
    /* VM finalization closes an undisposed live GUI before releasing VM buffers. */
    run(vm, "var last = new LvGui(20, 20); last.Screen.Create(\"label\").Text = \"bye\"; "
            "last.Pump(40);");
    mcs_free(vm);
    CHECK(!lv_is_initialized());
    CHECK(tracked_bytes == 0);
    /* External LVGL is never silently reinitialized/deinitialized. */
    lv_init();
    vm = make_vm();
    run(vm, "try { var stolen = new LvGui(20, 20); throw new Exception(\"external stolen\"); } "
            "catch (InvalidOperationException e) {}");
    mcs_free(vm);
    CHECK(lv_is_initialized());
    CHECK(tracked_bytes == 0);
    lv_deinit();
    vm = make_vm();
#include "style_readback.inc"
#include "font_readback.inc"
    run(vm, "var fontErrors=new LvGui(40,30); var fontLabel=fontErrors.Screen.Create(\"label\");"
            "try { fontLabel.SetFont(0); throw new Exception(\"bad name\"); } "
            "catch(ArgumentException e) {}"
            "try { fontLabel.SetFont(null); throw new Exception(\"null name\"); } "
            "catch(ArgumentException e) {}"
            "try { fontLabel.SetFont(\"Default\",true); throw new Exception(\"bad selector "
            "type\"); } catch(ArgumentException e) {}"
            "try { fontLabel.SetFont(\"Default\",-1); throw new Exception(\"bad selector range\"); "
            "} catch(ArgumentOutOfRangeException e) {}"
            "try { fontLabel.SetFont(); throw new Exception(\"bad arity\"); } "
            "catch(ArgumentException e) {}"
            "try { fontLabel.SetFont(\"Default\",0,0); throw new Exception(\"extra args\"); } "
            "catch(ArgumentException e) {}"
            "if(LvGui.HasFont(\"\")) throw new Exception(\"empty font\");"
            "try { LvGui.HasFont(\"Default\\0hidden\"); throw new Exception(\"NUL font name\"); } "
            "catch(ArgumentException e) {}"
            "try { "
            "LvGui.HasFont("
            "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\")"
            "; throw new Exception(\"long font name\"); } catch(ArgumentException e) {}"
            "try { LvGui.HasFont(null); throw new Exception(\"bad probe name\"); } "
            "catch(ArgumentException e) {}"
            "fontLabel.SetFont(\"Default\",LvGui.Checked); fontLabel.AddState(LvGui.Checked);"
            "if(fontLabel.FontHeight<=0) throw new Exception(\"state font\");"
            "fontLabel.SetFont(\"Default\"); GC.Collect(); fontLabel.Text=\"still static\"; "
            "fontErrors.Pump(40);"
            "fontErrors.Dispose(); try { fontLabel.SetFont(\"Default\"); throw new "
            "Exception(\"stale font\"); } catch(ObjectDisposedException e) {}"
            "try { var h=fontLabel.FontHeight; throw new Exception(\"stale metric\"); } "
            "catch(ObjectDisposedException e) {}");
    mcs_free(vm);
    CHECK(!lv_is_initialized());
    CHECK(tracked_bytes == 0);
    /* A wrapper keeps its owner alive after the script drops the owner value,
     * then releases it when the last handle becomes unreachable. */
    vm = make_vm();
    run(vm,
        "var owner = new LvGui(30, 20); var held = owner.Screen.Create(\"label\"); owner = null; "
        "GC.Collect(); held.Text = \"owner retained\"; if (held.Text != \"owner retained\") throw "
        "new Exception(\"bad root\"); held = null; GC.Collect(); GC.Collect();");
    CHECK(!lv_is_initialized());
    mcs_free(vm);
    CHECK(tracked_bytes == 0);
    /* Forced VM allocation failure during draw-buffer allocation: scope must
     * unwind a partially initialized upstream instance without a VM leak. */
    vm = make_vm();
    scope = mcs_lvgl_scope_begin(vm);
    fail_large = 50000;
    CHECK(mcs_exec_source(vm, "oom.cs", "var huge = new LvGui(4096, 4096);") == MCS_ERR_MEMORY);
    fail_large = 0;
    mcs_lvgl_scope_end(vm, scope);
    CHECK(!lv_is_initialized());
    run(vm, "var retry = new LvGui(20, 20); retry.Dispose();");
    mcs_register_module(vm, "FailAlloc", failure_fns);
    run(vm, "var allocationGui = new LvGui(20, 20); var allocationRoot = allocationGui.Screen; "
            "allocationRoot.Create(\"label\").Delete(); FailAlloc.Next(); try { "
            "allocationRoot.Create(\"label\"); throw new Exception(\"OOM "
            "became success\"); } catch (OutOfMemoryException e) {} if (allocationRoot.ChildCount "
            "!= 0) throw new Exception(\"orphan native widget\"); allocationGui.Dispose();");
    CHECK(fail_next == 0);
    CHECK(!lv_is_initialized());
    mcs_free(vm);
    CHECK(tracked_bytes == 0);
    /* Runtime exception and execution-budget Stop both close foreground GUI. */
    vm = make_vm();
    scope = mcs_lvgl_scope_begin(vm);
    CHECK(
        mcs_exec_source(vm, "exception.cs",
                        "var exceptional = new LvGui(20, 20); throw new Exception(\"fixture\");") ==
        MCS_ERR_RUNTIME);
    mcs_lvgl_scope_end(vm, scope);
    CHECK(!lv_is_initialized());
    scope = mcs_lvgl_scope_begin(vm);
    mcs_limits_t limits = {0, 10};
    mcs_set_limits(vm, &limits);
    CHECK(mcs_exec_source(vm, "stop.cs", "var stopped = new LvGui(20, 20); while (true) {}") ==
          MCS_ERR_ABORTED);
    mcs_lvgl_scope_end(vm, scope);
    CHECK(!lv_is_initialized());
    mcs_free(vm);
    CHECK(tracked_bytes == 0);
    printf("LVGL %d.%d.%d real headless: %d checks, %d failures\n", LVGL_VERSION_MAJOR,
           LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, checks, failures);
    return failures != 0;
}
