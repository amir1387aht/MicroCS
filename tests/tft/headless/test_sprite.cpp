/* Actual unchanged upstream TFT_eSPI + MicroCS wrapper/VM. Only the Arduino
 * boundary is test-provided. Physical IO aborts, never returns fake success. */
#include "TFT_eSPI.h"
#include "mcs.h"
#include "mcs_hal.h"
#include "../src/sprite_contract.h"
TestSerial Serial;
SPIClass SPI;
static int checks, failures;
static size_t vm_bytes, fail_vm_size, fail_native_bytes;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c);                                   \
        }                                                                                          \
    } while (0)
extern "C" void *__real_calloc(size_t count, size_t size);
extern "C" void *__wrap_calloc(size_t count, size_t size) {
    if (fail_native_bytes && size && count <= SIZE_MAX / size &&
        count * size == fail_native_bytes) {
        fail_native_bytes = 0;
        return NULL; // Allocator fault, not a substitute sprite implementation.
    }
    return __real_calloc(count, size);
}
static void *alloc(void *, void *p, size_t old, size_t n) {
    if (!n) {
        free(p);
        vm_bytes -= old;
        return NULL;
    }
    if (fail_vm_size && n == fail_vm_size) {
        fail_vm_size = 0;
        return NULL;
    }
    void *next = realloc(p, n);
    if (next)
        vm_bytes = vm_bytes - old + n;
    return next;
}
static void quiet(void *, const char *, size_t) {}
static mcs_vm_t *create() {
    mcs_config_t c;
    mcs_config_default(&c);
    c.write_fn = quiet;
    c.error_fn = quiet;
    c.realloc_fn = alloc;
    return mcs_new(&c);
}
static void run(mcs_vm_t *vm, const char *code) {
    mcs_result_t r = mcs_exec_source(vm, "sprite-test.cs", code);
    CHECK(r == MCS_OK);
    if (r != MCS_OK)
        fprintf(stderr, "%s\n", mcs_last_error(vm));
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--attempt-io")) {
        TFT_eSPI physical;
        physical.init(); // Harness self-test MUST abort on actual upstream IO.
        return 99;
    }
    // Exhaustive actual upstream storage roundtrip/quantization. This is RAM,
    // not a physical panel readback or a replacement graphics implementation.
    {
        TFT_eSPI parent;
        TFT_eSprite full(&parent), reduced(&parent);
        CHECK(full.createSprite(2, 2));
        reduced.setColorDepth(8);
        CHECK(reduced.createSprite(2, 2));
        const uint16_t blue[] = {0, 11, 21, 31};
        for (uint32_t color = 0; color <= 65535; color++) {
            full.drawPixel(1, 1, color);
            CHECK(full.readPixel(1, 1) == color);
            reduced.drawPixel(1, 1, color);
            uint16_t r3 = color >> 13, g3 = (color >> 8) & 7, b2 = (color >> 3) & 3;
            uint16_t expected =
                (((r3 << 2) | (r3 >> 1)) << 11) | (((g3 << 3) | g3) << 5) | blue[b2];
            CHECK(reduced.readPixel(1, 1) == expected);
        }
    }
    mcs_vm_t *vm = create();
    CHECK(vm);
    if (!vm)
        return 1;
    CHECK(mcs_capability_has(vm, "TFT"));
    CHECK(mcs_capability_has(vm, "TFT.Sprite"));
    mcs_mem_stats_t before, after;
    mcs_mem_stats(vm, &before);
    for (int i = 0; i < 100; i++) {
        CHECK(mcs_global_exists(vm, "TftSprite"));
        CHECK(mcs_capability_has(vm, "TFT.Sprite"));
    }
    mcs_mem_stats(vm, &after);
    CHECK(before.bytes_in_use == after.bytes_in_use && before.objects == after.objects);
    run(vm, sprite_contract);
    mcs_free(vm);
    CHECK(vm_bytes == 0);
    for (int i = 0; i < 20; i++) {
        vm = create();
        CHECK(vm);
        if (!vm)
            return 1;
        run(vm, "var p = new Tft(); var a = new TftSprite(p,12,10); var b = new "
                "TftSprite(p,8,8,8); a.Fill(0xF800); b.Fill(0xFFFF);");
        mcs_free(vm);
        CHECK(vm_bytes == 0);
    }
    vm = create();
    CHECK(vm);
    if (!vm)
        return 1;
    run(vm, "var parent = new Tft(); var warm = new TftSprite(parent,12,10); warm.Dispose();");
    mcs_value_t parent = mcs_get_global(vm, "parent"), result;
    mcs_value_t args[] = {parent, mcs_int(12), mcs_int(10)};
    fail_native_bytes = 242;
    CHECK(mcs_new_object(vm, "TftSprite", 3, args, &result) == MCS_ERR_RUNTIME);
    CHECK(fail_native_bytes == 0);
    CHECK(strstr(mcs_last_error(vm), "OutOfMemoryException"));
    run(vm, "if(parent.SpriteCount!=0 || parent.SpriteBytes!=0) throw new Exception(\"native OOM "
            "reservation\");");
    // Fault during VM-accounted native object allocation, AFTER reservation.
    fail_vm_size = sizeof(TFT_eSprite);
    CHECK(mcs_new_object(vm, "TftSprite", 3, args, &result) == MCS_ERR_MEMORY);
    CHECK(fail_vm_size == 0);
    mcs_gc(vm); // Panicking constructor's unreachable userdata rolls back here.
    run(vm, "if(parent.SpriteCount!=0 || parent.SpriteBytes!=0) throw new Exception(\"VM OOM "
            "reservation\"); var retry=new TftSprite(parent,12,10); retry.Dispose(); "
            "parent.Dispose();");
    mcs_free(vm);
    CHECK(vm_bytes == 0);
    // Termination through error/Stop must still allow GC+VM destruction.
    for (int scenario = 0; scenario < 2; scenario++) {
        vm = create();
        CHECK(vm);
        if (!vm)
            return 1;
        if (scenario) {
            mcs_limits_t limits = {};
            limits.steps = 500;
            mcs_set_limits(vm, &limits);
        }
        mcs_result_t r = mcs_exec_source(
            vm, "unwind.cs",
            scenario ? "var p=new Tft(); var s=new TftSprite(p,12,10); while(true) {}"
                     : "var p=new Tft(); var s=new TftSprite(p,12,10); throw new Exception(\"test "
                       "error\");");
        CHECK(r == (scenario ? MCS_ERR_ABORTED : MCS_ERR_RUNTIME));
        mcs_free(vm);
        CHECK(vm_bytes == 0);
    }
    printf("%d/%d actual upstream sprite/VM checks passed; native C# contract +20 undisposed "
           "teardowns; hardware IO forbidden\n",
           checks - failures, checks);
    return failures != 0;
}
