/* Actual unchanged upstream TFT_eSPI + MicroCS wrapper/VM. Only the Arduino
 * boundary is test-provided. Physical IO aborts, never returns fake success. */
#include "TFT_eSPI.h"
#include "../../../ports/arduino/mcs_tft_sprite_native.h"
#include "mcs.h"
#include "mcs_hal.h"
#include "mcs_tft.h"
#include "mcs_shell.h"
#include "mcs_vfs.h"
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
static const char scope_source[] = "var scopeParent=new Tft(); var scopeChild=new "
                                   "TftSprite(scopeParent,12,10); scopeChild.Fill(0xF800);";
static void scope_tests() {
    for (int scenario = 0; scenario < 3; scenario++) {
        mcs_vm_t *vm = create();
        CHECK(vm);
        if (!vm)
            return;
        for (int repeat = 0; repeat < 8; repeat++) {
            uint64_t checkpoint = mcs_tft_scope_begin(vm);
            mcs_limits_t limits = {0, 500};
            if (scenario == 2)
                mcs_set_limits(vm, &limits);
            const char *source =
                scenario == 0 ? scope_source
                : scenario == 1
                    ? "var scopeParent=new Tft(); var scopeChild=new TftSprite(scopeParent,12,10); "
                      "throw new Exception(\"scope error\");"
                    : "var scopeParent=new Tft(); var scopeChild=new TftSprite(scopeParent,12,10); "
                      "while(true) {}";
            mcs_result_t r = mcs_exec_source(vm, "scope.cs", source);
            CHECK(r == (scenario == 0   ? MCS_OK
                        : scenario == 1 ? MCS_ERR_RUNTIME
                                        : MCS_ERR_ABORTED));
            mcs_set_limits(vm, NULL);
            CHECK(mcs_tft_scope_end(vm, checkpoint) == 0);
            CHECK(mcs_tft_scope_end(vm, checkpoint) == 0);
            run(vm,
                "try { var n=scopeParent.SpriteCount; throw new Exception(\"live scope parent\"); "
                "} catch(ObjectDisposedException e) {} try { scopeChild.ReadPixel(0,0); throw new "
                "Exception(\"live scope child\"); } catch(ObjectDisposedException e) {} var "
                "replacement=new Tft(); replacement.Dispose();");
            mcs_gc(vm);
        }
        mcs_free(vm);
        CHECK(vm_bytes == 0);
    }
    mcs_vm_t *vm = create();
    CHECK(vm);
    if (!vm)
        return;
    run(vm, scope_source);
    uint64_t existing = mcs_tft_scope_begin(vm);
    CHECK(mcs_tft_scope_end(vm, existing) == 0); // Preserve existing embedding/REPL owners.
    run(vm,
        "if(scopeChild.ReadPixel(0,0)!=0xF800) throw new Exception(\"preexisting owner closed\");");
    mcs_vm_t *other = create();
    CHECK(other);
    if (!other) {
        mcs_free(vm);
        return;
    }
    CHECK(mcs_tft_scope_end(other, 0) == 0); // Must not close a different VM's owner.
    mcs_free(other);
    run(vm, "scopeChild.Dispose(); scopeParent.Dispose();");
    uint64_t outer = mcs_tft_scope_begin(vm);
    run(vm, scope_source);
    uint64_t inner = mcs_tft_scope_begin(vm);
    CHECK(mcs_tft_scope_end(vm, inner) == 0);
    run(vm,
        "if(scopeChild.ReadPixel(0,0)!=0xF800) throw new Exception(\"inner scope closed outer\");");
    CHECK(mcs_tft_scope_end(vm, outer) == 0);
    run(vm, "try { scopeChild.Fill(0); throw new Exception(\"outer scope not closed\"); } "
            "catch(ObjectDisposedException e) {}");
    // Scope cleanup must release a partially initialized sprite after VM panic,
    // even BEFORE collecting its failed-constructor userdata.
    outer = mcs_tft_scope_begin(vm);
    run(vm,
        "var scopeParent=new Tft(); var warm=new TftSprite(scopeParent,12,10); warm.Dispose();");
    mcs_value_t parent = mcs_get_global(vm, "scopeParent"), result;
    mcs_value_t args[] = {parent, mcs_int(12), mcs_int(10)};
    fail_vm_size = sizeof(mcs_tft_safe_sprite);
    CHECK(mcs_new_object(vm, "TftSprite", 3, args, &result) == MCS_ERR_MEMORY);
    CHECK(fail_vm_size == 0);
    CHECK(mcs_tft_scope_end(vm, outer) == 0);
    run(vm, "var afterPanic=new Tft(); afterPanic.Dispose();");
    mcs_gc(vm);
    mcs_free(vm);
    CHECK(vm_bytes == 0);
}
struct command_t {
    const char *input;
    size_t position;
    char output[4096];
    size_t written;
};
static int command_read(void *ud, uint8_t *data, size_t n, uint32_t timeout) {
    (void)timeout;
    command_t *c = (command_t *)ud;
    size_t left = strlen(c->input) - c->position;
    if (n > left)
        n = left;
    memcpy(data, c->input + c->position, n);
    c->position += n;
    return (int)n;
}
static void command_write(void *ud, const char *data, size_t n) {
    command_t *c = (command_t *)ud;
    if (n >= sizeof(c->output) - c->written)
        abort();
    memcpy(c->output + c->written, data, n);
    c->written += n;
    c->output[c->written] = 0;
}
static void shell_scope_tests() {
    mcs_vm_t *vm = create();
    CHECK(vm);
    if (!vm)
        return;
    mcs_ramfs_t ram;
    mcs_ramfs_init(&ram, 16384, NULL, NULL);
    mcs_vfs_t vfs;
    mcs_vfs_init(&vfs);
    CHECK(mcs_vfs_mount(&vfs, "/", &mcs_ramfs_ops, &ram, 0) == 0);
    CHECK(mcs_vfs_write_file(&vfs, "/scope.cs", scope_source, strlen(scope_source), false) == 0);
    uint8_t *image = NULL;
    size_t bytes = 0;
    CHECK(mcs_compile_image(vm, "scope.cs", scope_source, false, &image, &bytes) == MCS_OK);
    CHECK(mcs_vfs_write_file(&vfs, "/scope.mcsb", image, bytes, false) == 0);
    mcs_free_image(vm, image);
    command_t command = {};
    mcs_transport_t transport = {command_read, command_write, &command};
    mcs_shell_t shell;
    mcs_shell_init(&shell, vm, &vfs, NULL, transport);
    for (int kind = 0; kind < 2; kind++) {
        for (int repeat = 0; repeat < 4; repeat++) {
            command = {};
            command.input = kind ? "run /scope.mcsb\n" : "run /scope.cs\n";
            CHECK(mcs_shell_step(&shell, 0));
            CHECK(strstr(command.output, "\x04OK\n"));
            run(vm, "try { scopeChild.ReadPixel(0,0); throw new Exception(\"file scope not "
                    "closed\"); } catch(ObjectDisposedException e) {} var nextOwner=new Tft(); "
                    "nextOwner.Dispose();");
            mcs_gc(vm);
        }
    }
    // Machine exec is intentionally persistent, like interactive REPL.
    command = {};
    command.input = "exec var persistent=new Tft(); var kept=new TftSprite(persistent,2,2); "
                    "kept.Fill(0xF800);\n";
    CHECK(mcs_shell_step(&shell, 0));
    CHECK(strstr(command.output, "\x04OK\n"));
    run(vm, "if(kept.ReadPixel(0,0)!=0xF800) throw new Exception(\"exec persistence lost\"); "
            "kept.Dispose(); persistent.Dispose();");
    mcs_free(vm);
    CHECK(vm_bytes == 0);
    mcs_ramfs_free(&ram);
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
        mcs_tft_safe_sprite palette(&parent), bitmap(&parent);
        CHECK(palette.createChecked(3, 5, 4));
        CHECK(bitmap.createChecked(9, 3, 1));
        for (int i = 0; i < 16; i++)
            CHECK(palette.getPaletteColor(i) == pgm_read_word(default_4bit_palette + i));
        for (int index = 0; index < 16; index++) {
            uint16_t color = (uint16_t)(index * 4097);
            palette.setPaletteColor(index, color);
            palette.fillSprite(index);
            CHECK(palette.readPixelValue(2, 4) == index && palette.readPixel(2, 4) == color);
        }
        for (int bit = 0; bit < 2; bit++) {
            bitmap.fillSprite(bit);
            CHECK(bitmap.readPixelValue(8, 2) == bit);
        }
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
    CHECK(mcs_capability_has(vm, "TFT.Palette"));
    CHECK(mcs_capability_has(vm, "TFT.Bitmap"));
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
    mcs_value_t palette_args[] = {parent, mcs_int(12), mcs_int(10), mcs_int(4)};
    for (size_t failed_request : {size_t(32), size_t(61)}) {
        fail_native_bytes = failed_request; // first palette, then pixels after palette succeeded
        CHECK(mcs_new_object(vm, "TftSprite", 4, palette_args, &result) == MCS_ERR_RUNTIME);
        CHECK(fail_native_bytes == 0);
        CHECK(strstr(mcs_last_error(vm), "OutOfMemoryException"));
        run(vm, "if(parent.SpriteCount!=0 || parent.SpriteBytes!=0) throw new "
                "Exception(\"palette/pixel OOM reservation\"); var retry4=new "
                "TftSprite(parent,12,10,4); retry4.Dispose();");
    }
    // Fault during VM-accounted native object allocation, AFTER reservation.
    fail_vm_size = sizeof(mcs_tft_safe_sprite);
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
    scope_tests();
    shell_scope_tests();
    printf("%d/%d actual upstream sprite/VM checks passed; native C# contract +20 undisposed "
           "teardowns; hardware IO forbidden\n",
           checks - failures, checks);
    return failures != 0;
}
