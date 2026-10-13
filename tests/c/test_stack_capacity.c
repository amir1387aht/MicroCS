/* Functional resource-limit regressions. Only trusted compiler output executes.
 * Never run this suite against an unpatched runtime to reproduce corruption. */
#include "../../src/mcs_internal.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c);                                        \
        }                                                                                          \
    } while (0)
static char output[4096];
static size_t used, live;
typedef union {
    size_t size;
    long double align;
    void *pointer;
} allocation_t;
static void *allocate(void *ud, void *ptr, size_t old, size_t n) {
    (void)ud;
    (void)old;
    allocation_t *p = ptr ? (allocation_t *)ptr - 1 : NULL;
    size_t actual = p ? p->size : 0;
    if (!n) {
        live -= actual;
        free(p);
        return NULL;
    }
    allocation_t *q = realloc(p, sizeof(*q) + n);
    if (!q)
        return NULL;
    q->size = n;
    live = live - actual + n;
    return q + 1;
}
static void write_output(void *ud, const char *s, size_t n) {
    (void)ud;
    if (used + n < sizeof output) {
        memcpy(output + used, s, n);
        used += n;
        output[used] = 0;
    }
}
static void quiet(void *ud, const char *s, size_t n) {
    (void)ud;
    (void)s;
    (void)n;
}
static mcs_value_t echo(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)vm;
    (void)self;
    return argc ? argv[0] : mcs_int(42);
}
static mcs_value_t callback(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)self;
    (void)argc;
    mcs_value_t result = mcs_null();
    mcs_call_value(vm, argv[0], 0, NULL, &result);
    return result;
}
static mcs_value_t cancel(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)self;
    (void)argc;
    (void)argv;
    mcs_request_abort(vm);
    return mcs_int(42);
}
static const mcs_reg_t methods[] = {MCS_FN("Echo", echo, 1), MCS_FN("Cancel", cancel, 0),
                                    MCS_REG_END};
static mcs_vm_t *create(unsigned slots) {
    mcs_config_t cfg;
    mcs_config_default(&cfg);
    cfg.stack_slots = slots;
    cfg.heap_limit = 0;
    cfg.realloc_fn = allocate;
    cfg.write_fn = write_output;
    cfg.error_fn = quiet;
    used = 0;
    output[0] = 0;
    mcs_vm_t *vm = mcs_new(&cfg);
    CHECK(vm != NULL);
    if (vm) {
        mcs_register_function(vm, "Echo", echo, 1);
        mcs_register_function(vm, "Callback", callback, 1);
        mcs_register_function(vm, "Cancel", cancel, 0);
        mcs_register_module(vm, "Native", methods);
    }
    return vm;
}
static void destroy(mcs_vm_t *vm) {
    mcs_free(vm);
    CHECK(live == 0);
}
static void clean_stack(mcs_vm_t *vm) {
    CHECK(vm->sp == vm->stack);
    CHECK(vm->frame_count == 0);
    CHECK(vm->handler_count == 0);
    CHECK(vm->run_depth == 0);
}
static void ordinary_call(mcs_vm_t *vm) {
    mcs_value_t arg = mcs_int(42), result = mcs_null();
    CHECK(mcs_call(vm, "Echo", 1, &arg, &result) == MCS_OK);
    CHECK(mcs_is_int(result) && result.as.i == 42);
    clean_stack(vm);
}
static void api_calls(void) {
    mcs_vm_t *vm = create(128);
    if (!vm)
        return;
    mcs_value_t fn = mcs_get_global(vm, "Echo"), recv = mcs_get_global(vm, "Native");
    mcs_value_t arg = mcs_int(42), result;
    const int counts[] = {-1, 1, 128, INT_MAX};
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        const mcs_value_t *args = counts[i] == 1 ? NULL : &arg;
        result = mcs_int(7);
        CHECK(mcs_call_value(vm, fn, counts[i], args, &result) == MCS_ERR_RUNTIME);
        CHECK(mcs_is_null(result));
        clean_stack(vm);
        ordinary_call(vm);
        result = mcs_int(7);
        CHECK(mcs_invoke(vm, recv, "Echo", counts[i], args, &result) == MCS_ERR_RUNTIME);
        CHECK(mcs_is_null(result));
        clean_stack(vm);
        ordinary_call(vm);
    }
    CHECK(mcs_call_value(vm, fn, 1, &arg, &arg) == MCS_OK);
    CHECK(arg.as.i == 42); /* result may alias an input */
    CHECK(mcs_invoke(vm, recv, "Echo", 1, &arg, &result) == MCS_OK);
    CHECK(result.as.i == 42);
    /* A directly called native replaces its existing receiver/argument slots. */
    vm->sp = vm->stack_end - MCS_STACK_MARGIN - 3;
    mcs_value_t *base = vm->sp;
    CHECK(mcs_call_value(vm, fn, 1, &arg, &result) == MCS_OK);
    CHECK(vm->sp == base && result.as.i == 42);
    vm->sp = vm->stack;
    CHECK(mcs_call(vm, "Cancel", 0, NULL, &result) == MCS_ERR_ABORTED);
    clean_stack(vm);
    ordinary_call(vm);
    CHECK(mcs_invoke(vm, recv, "Cancel", 0, NULL, &result) == MCS_ERR_ABORTED);
    clean_stack(vm);
    ordinary_call(vm);
    mcs_gc(vm);
    ordinary_call(vm);
    destroy(vm);
}

#if MCS_ENABLE_COMPILER
static const char *tails[] = {"42", "x + 2", "seq[0]", "(int)txt[0]", "g"};
static void source(char *dst, size_t cap, unsigned index) {
    size_t n = (size_t)snprintf(dst, cap,
                                "int g = 42; int Work() { int x = 40; var seq = new int[] {42}; "
                                "var txt = \"*\"; var a = new int[] {");
    for (unsigned i = 0; i < 160; i++)
        n += (size_t)snprintf(dst + n, cap - n, "0,");
    snprintf(dst + n, cap - n, "%s}; return a[160]; } Console.WriteLine(Work());", tails[index]);
}
static void catch_source(char *dst, size_t cap, bool nested) {
    size_t n = (size_t)snprintf(
        dst, cap,
        "var saved = () => 0; int Work() { int x = 42; saved = () => x; var a = new int[] {");
    for (unsigned i = 0; i < 160; i++)
        n += (size_t)snprintf(dst + n, cap - n, "0,");
    snprintf(dst + n, cap - n,
             "0}; return a.Length; } try { Console.WriteLine(%s); } catch (StackOverflowException) "
             "{ Console.WriteLine(\"caught\"); } Console.WriteLine(saved());",
             nested ? "Callback(Work)" : "Work()");
}
#endif
#if MCS_ENABLE_COMPILER && MCS_ENABLE_BYTECODE_SAVE
static int emit(const char *dir) {
    mcs_vm_t *vm = create(1024);
    if (!vm)
        return 1;
    for (unsigned i = 0; i < 7; i++) {
        char text[8192];
        if (i >= 5)
            catch_source(text, sizeof text, i == 6);
        else
            source(text, sizeof text, i);
        for (unsigned noopt = 0; noopt < 2; noopt++) {
            uint8_t *bytes = NULL;
            size_t length = 0;
            CHECK(mcs_compile_image_ex(vm, "stack-resource.cs", text, noopt ? MCS_IMAGE_NO_OPT : 0,
                                       &bytes, &length) == MCS_OK);
            if (!bytes)
                continue;
            char path[4096];
            snprintf(path, sizeof path, "%s/%u-%u.mcsb", dir, i, noopt);
            FILE *f = fopen(path, "wb");
            CHECK(f != NULL);
            if (f) {
                CHECK(fwrite(bytes, 1, length, f) == length);
                CHECK(fclose(f) == 0);
            }
            mcs_free_image(vm, bytes);
        }
    }
    destroy(vm);
    return failures ? 1 : 0;
}
#endif
static uint8_t *read_image(const char *dir, unsigned i, unsigned noopt, size_t *length) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%u-%u.mcsb", dir, i, noopt);
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (!f)
        return NULL;
    CHECK(fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f);
    CHECK(size > 0 && size < 65536);
    rewind(f);
    if (size <= 0 || size >= 65536) {
        fclose(f);
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)size);
    CHECK(bytes != NULL);
    if (bytes) {
        CHECK(fread(bytes, 1, (size_t)size, f) == (size_t)size);
        *length = (size_t)size;
    }
    CHECK(fclose(f) == 0);
    return bytes;
}
static void executions(const char *dir) {
    const unsigned capacities[] = {96, 128, 176, 192, 208, 224, 256, 1024};
    for (unsigned i = 0; i < 7; i++) {
        for (unsigned noopt = MCS_ENABLE_SUPEROPS ? 0 : 1; noopt < 2; noopt++) {
            size_t length = 0;
            uint8_t *bytes = read_image(dir, i, noopt, &length);
            if (!bytes)
                continue;
            for (unsigned ci = 0; ci < sizeof capacities / sizeof capacities[0]; ci++) {
                for (unsigned mode = 0; mode < 3; mode++) {
#if !MCS_ENABLE_COMPILER
                    if (mode == 0)
                        continue;
#endif
                    mcs_vm_t *vm = create(capacities[ci]);
                    if (!vm)
                        continue;
                    mcs_result_t r;
#if MCS_ENABLE_COMPILER
                    if (mode == 0) {
                        char text[8192];
                        if (i >= 5)
                            catch_source(text, sizeof text, i == 6);
                        else
                            source(text, sizeof text, i);
                        r = mcs_exec_source(vm, "stack-resource.cs", text);
                    } else
#endif
                        r = mode == 1 ? mcs_exec_image(vm, bytes, length)
                                      : mcs_exec_image_xip(vm, bytes, length);
                    if (i >= 5) {
                        CHECK(r == MCS_OK);
                        CHECK(!strcmp(output, "caught\n42\n") || !strcmp(output, "161\n42\n"));
                        if (capacities[ci] == 1024)
                            CHECK(!strcmp(output, "161\n42\n"));
                    } else if (r == MCS_OK) {
                        CHECK(!strcmp(output, "42\n"));
                        if (capacities[ci] == 96)
                            CHECK(0);
                    } else {
                        CHECK(r == MCS_ERR_RUNTIME);
                        CHECK(strstr(mcs_last_error(vm), "StackOverflowException") != NULL);
                        if (capacities[ci] == 1024)
                            CHECK(0);
                    }
                    clean_stack(vm);
                    mcs_gc(vm);
                    ordinary_call(vm);
                    destroy(vm);
                }
            }
            free(bytes); /* XIP storage outlives every VM that used it. */
        }
    }
}
int main(int argc, char **argv) {
#if MCS_ENABLE_COMPILER && MCS_ENABLE_BYTECODE_SAVE
    if (argc == 3 && !strcmp(argv[1], "--emit"))
        return emit(argv[2]);
#endif
    if (argc != 2) {
        fprintf(stderr, "usage: %s compiler-fixture-directory\n", argv[0]);
        return 2;
    }
    api_calls();
    executions(argv[1]);
    printf("%u checks, %u failures (trusted-source resource limits; not image verification)\n",
           checks, failures);
    return failures ? 1 : 0;
}
