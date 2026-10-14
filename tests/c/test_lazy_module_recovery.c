/* Ordinary trusted module/constant first use under a finite allocation window.
 * Public source/name lookup drives the operation; whitebox inventories check
 * pending order and ROM/overload duplication. This is NOT general OOM safety.
 * Build with all core/module C sources, include paths include and src, and libm.
 */
#include "mcs_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_POINTERS 8192
#define MAX_PENDING 64
#define MAX_WINDOW 128

typedef struct {
    void *ptr;
    size_t size;
} physical_t;
typedef struct {
    physical_t pointers[MAX_POINTERS];
    size_t bytes, live, seen, denied;
    int armed, fault;
} allocator_t;
static allocator_t allocator;
static unsigned checks, failures;
static const char *scenario, *target;
static int fault_index = -1, retry_round;
static unsigned nested_calls;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s fault=%d retry=%d line=%d: %s\n", scenario, fault_index, retry_round,  \
                   __LINE__, #x);                                                                  \
        }                                                                                          \
    } while (0)

static void quiet(void *ud, const char *s, size_t n) {
    (void)ud;
    (void)s;
    (void)n;
}
static void *tracked_realloc(void *ud, void *ptr, size_t old_hint, size_t size) {
    allocator_t *a = (allocator_t *)ud;
    (void)old_hint; /* Physical ownership is deliberately independent of this hint. */
    size_t slot = MAX_POINTERS;
    for (size_t i = 0; i < MAX_POINTERS; i++) {
        if ((ptr && a->pointers[i].ptr == ptr) || (!ptr && !a->pointers[i].ptr)) {
            slot = i;
            break;
        }
    }
    if (slot == MAX_POINTERS) {
        fprintf(stderr, "physical tracker ownership/capacity error\n");
        exit(2);
    }
    if (!size) {
        if (ptr) {
            a->bytes -= a->pointers[slot].size;
            a->live--;
            free(ptr);
            memset(&a->pointers[slot], 0, sizeof(physical_t));
        }
        return NULL;
    }
    if (a->armed) {
        size_t index = a->seen++;
        if (a->fault >= 0 && index >= (size_t)a->fault) {
            a->denied++;
            return NULL;
        }
    }
    size_t old = a->pointers[slot].size;
    int fresh = ptr == NULL;
    void *out = realloc(ptr, size);
    if (out) {
        a->bytes = a->bytes - old + size;
        a->live += fresh;
        a->pointers[slot].ptr = out;
        a->pointers[slot].size = size;
    }
    return out;
}
static mcs_value_t begin_window(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)vm;
    (void)self;
    (void)argc;
    (void)argv;
    allocator.seen = allocator.denied = 0;
    allocator.armed = 1;
    return mcs_null();
}
static mcs_value_t end_window(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)vm;
    (void)self;
    (void)argc;
    (void)argv;
    allocator.armed = 0;
    return mcs_null();
}
static mcs_value_t resolve_target(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)self;
    (void)argc;
    (void)argv;
    return mcs_get_global(vm, target); /* Nested public lookup while source guard is active. */
}
static mcs_value_t nested_read(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)self;
    (void)argc;
    (void)argv;
    nested_calls++;
    mcs_value_t before = mcs_get_global(vm, "BeforeSibling");
    mcs_value_t after = mcs_get_global(vm, "AfterSibling");
    if (!IS_KIND(before, MCS_O_CLASS) || !IS_KIND(after, MCS_O_CLASS))
        return mcs_int(-1);
    mcs_result_t r = mcs_exec_source(vm, "nested-sibling.cs",
                                     "if (BeforeSibling.Mark != 31 || AfterSibling.Mark != 39) "
                                     "throw new Exception(\"sibling order\");");
    return mcs_int(r == MCS_OK ? 70 : -1);
}
#define SIMPLE_FN(name, number)                                                                    \
    static mcs_value_t name(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {         \
        (void)vm;                                                                                  \
        (void)self;                                                                                \
        (void)argc;                                                                                \
        (void)argv;                                                                                \
        return mcs_int(number);                                                                    \
    }
SIMPLE_FN(first_pick, 110)
SIMPLE_FN(last_pick, 220)
SIMPLE_FN(extra, 33)
static mcs_value_t pick_arg(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)self;
    (void)argc;
    return mcs_int(mcs_to_int(vm, argv[0]) + 100);
}
static const mcs_reg_t first_regs[] = {MCS_FN("Pick", first_pick, 0), MCS_FN("Pick", pick_arg, 1),
                                       MCS_REG_END};
static const mcs_reg_t second_regs[] = {MCS_FN("Pick", last_pick, 0), MCS_FN("Extra", extra, 0),
                                        MCS_REG_END};
static const mcs_const_t first_consts[] = {MCS_CONST("Alpha", 7), MCS_CONST("Order", 11),
                                           MCS_CONST_END};
static const mcs_const_t second_consts[] = {MCS_CONST("Beta", 9), MCS_CONST("Order", 22),
                                            MCS_CONST("C", 3), MCS_CONST("D", 4), MCS_CONST_END};
static const mcs_const_t third_consts[] = {MCS_CONST("Gamma", 5), MCS_CONST("Order", 33),
                                           MCS_CONST_END};
static const mcs_const_t before_consts[] = {MCS_CONST("Mark", 31), MCS_CONST_END};
static const mcs_const_t after_consts[] = {MCS_CONST("Mark", 39), MCS_CONST_END};

typedef struct {
    uintptr_t ids[MAX_PENDING];
    size_t count;
} pending_t;
static pending_t pending_snapshot(mcs_vm_t *vm) {
    pending_t p = {{0}, 0};
    for (mcs_lazy_t *z = vm->lazy; z; z = z->next) {
        CHECK(p.count < MAX_PENDING);
        if (p.count == MAX_PENDING)
            break;
        p.ids[p.count++] = (uintptr_t)z;
    }
    return p;
}
static void pending_preserved(mcs_vm_t *vm, const pending_t *before) {
    size_t cursor = 0, siblings = 0;
    for (mcs_lazy_t *z = vm->lazy; z; z = z->next) {
        while (cursor < before->count && before->ids[cursor] != (uintptr_t)z)
            cursor++;
        CHECK(cursor < before->count); /* Remaining nodes retain their original relative order. */
        if (cursor < before->count)
            cursor++;
        if (!strcmp(z->name, "BeforeSibling") || !strcmp(z->name, "AfterSibling"))
            siblings++;
    }
#if MCS_LAZY_CLASSES
    CHECK(siblings == 2); /* Neither unrelated pending constant table was resolved or lost. */
#else
    CHECK(siblings == 0);
#endif
}
static void check_inventory(mcs_vm_t *vm, int module) {
    for (mcs_lazy_t *z = vm->lazy; z; z = z->next)
        CHECK(strcmp(z->name, target) != 0);
    mcs_value_t value = mcs_get_global(vm, target);
    CHECK(IS_KIND(value, MCS_O_CLASS));
    if (!IS_KIND(value, MCS_O_CLASS))
        return;
    mcs_class_t *cls = AS_CLASS(value);
    unsigned first = 0, second = 0, total = 0;
    for (mcs_rom_t *r = cls->rom; r; r = r->next) {
        total++;
        first += r->regs == first_regs;
        second += r->regs == second_regs;
    }
#if MCS_LAZY_REGS
    CHECK(total == (module ? 2u : 0u));
    CHECK(first == (module ? 1u : 0u));
    CHECK(second == (module ? 1u : 0u));
#else
    CHECK(total == 0);
    CHECK(first == 0);
    CHECK(second == 0);
#endif
    if (module) {
        mcs_string_t *name = mcs_find_interned(vm, "Pick", 4);
        mcs_value_t pick = mcs_null();
        CHECK(name && mcs_table_get_s(&cls->statics, name, &pick));
        CHECK(IS_KIND(pick, MCS_O_OVERLOADS));
        if (IS_KIND(pick, MCS_O_OVERLOADS))
            CHECK(AS_OVL(pick)->count == 2);
    }
}
static void check_source(mcs_vm_t *vm, const char *name, const char *source) {
    mcs_result_t result = mcs_exec_source(vm, name, source);
    if (result != MCS_OK)
        printf("RESULT %s fault=%d retry=%d result=%d error=%s\n", scenario, fault_index,
               retry_round, result, mcs_last_error(vm));
    CHECK(result == MCS_OK);
}
static void physical_finish(mcs_vm_t *vm) {
    mcs_free(vm);
    if (allocator.live) {
        printf("OUTSTANDING %s fault=%d pointers=%zu bytes=%zu\n", scenario, fault_index,
               allocator.live, allocator.bytes);
        for (size_t i = 0; i < MAX_POINTERS; i++)
            if (allocator.pointers[i].ptr)
                printf("  ptr=%p physical_bytes=%zu\n", allocator.pointers[i].ptr,
                       allocator.pointers[i].size);
    }
    CHECK(allocator.live == 0);
    CHECK(allocator.bytes == 0);
    /* Harness cleanup AFTER reporting/asserting every leak; no VM is reused here. */
    for (size_t i = 0; i < MAX_POINTERS; i++)
        free(allocator.pointers[i].ptr);
    memset(&allocator, 0, sizeof allocator);
}
static size_t run_case(int module, int nested, int fault, int expect_oom) {
    memset(&allocator, 0, sizeof allocator);
    allocator.fault = fault;
    fault_index = fault;
    retry_round = 0;
    nested_calls = 0;
    target = module ? "RecoveryModule" : "RecoveryConsts";
    scenario = module ? (nested ? "module-nested" : "module-source")
                      : (nested ? "const-nested" : "const-source");
    mcs_config_t cfg;
    mcs_config_default(&cfg);
    cfg.realloc_fn = tracked_realloc;
    cfg.alloc_ud = &allocator;
    cfg.write_fn = quiet;
    cfg.error_fn = quiet;
    cfg.heap_limit = 262144;
    cfg.stack_slots = 512;
    mcs_vm_t *vm = mcs_new(&cfg);
    CHECK(vm != NULL);
    if (!vm)
        return 0;
    mcs_register_function(vm, "BeginWindow", begin_window, 0);
    mcs_register_function(vm, "EndWindow", end_window, 0);
    mcs_register_function(vm, "ResolveTarget", resolve_target, 0);
    mcs_register_function(vm, "NestedRead", nested_read, 0);
    mcs_register_consts(vm, "BeforeSibling", before_consts);
    if (module)
        mcs_register_module(vm, target, first_regs);
    mcs_register_consts(vm, target, first_consts);
    mcs_register_consts(vm, "AfterSibling", after_consts);
    if (module)
        mcs_register_module(vm, target, second_regs);
    mcs_register_consts(vm, target, second_consts);
    if (!module)
        mcs_register_consts(vm, target, third_consts);
    check_source(vm, "existing-state.cs",
                 "int saved = 99; var kept = new int[2]; kept[0]=37; kept[1]=41;");
    pending_t before = pending_snapshot(vm);
    const char *operation =
        nested ? "BeginWindow(); var touched = ResolveTarget(); EndWindow();"
               : (module ? "BeginWindow(); var touched = RecoveryModule; EndWindow();"
                         : "BeginWindow(); var touched = RecoveryConsts; EndWindow();");
    mcs_result_t result = mcs_exec_source(vm, "registration-window.cs", operation);
    size_t seen = allocator.seen, denied = allocator.denied;
    allocator.armed = 0; /* Restore in C before any retry, diagnostics or GC. */
    printf("WINDOW %s fault=%d seen=%zu denied=%zu result=%d\n", scenario, fault, seen, denied,
           result);
    if (expect_oom) {
        CHECK(fault >= 0);
        CHECK(seen > (size_t)fault);
        CHECK(denied > 0);
        CHECK(result == MCS_ERR_MEMORY);
        if (result != MCS_OK)
            printf("ORIGINAL %s fault=%d error=%s\n", scenario, fault, mcs_last_error(vm));
    } else {
        CHECK(result == MCS_OK);
        CHECK(denied == 0);
        if (fault >= 0) {
            CHECK(seen <= (size_t)fault);
            printf("UNHIT %s fault=%d seen=%zu (sentinel, expected success)\n", scenario, fault,
                   seen);
        }
    }
    pending_preserved(vm, &before);
    check_source(vm, "siblings-nested.cs",
                 "if (NestedRead()!=70) throw new Exception(\"nested sibling resolution\");");
    CHECK(nested_calls == 1);
    for (retry_round = 1; retry_round <= 3; retry_round++) {
        const char *verify =
            module ? "if (saved!=99 || kept[0]!=37 || kept[1]!=41 || RecoveryModule.Alpha!=7 || "
                     "RecoveryModule.Beta!=9 || RecoveryModule.Order!=22 || RecoveryModule.C!=3 || "
                     "RecoveryModule.D!=4 || RecoveryModule.Pick()!=220 || "
                     "RecoveryModule.Pick(5)!=105 || RecoveryModule.Extra()!=33) throw new "
                     "Exception(\"module recovery/state/precedence\");"
                   : "if (saved!=99 || kept[0]!=37 || kept[1]!=41 || RecoveryConsts.Alpha!=7 || "
                     "RecoveryConsts.Beta!=9 || RecoveryConsts.Gamma!=5 || "
                     "RecoveryConsts.Order!=33 || RecoveryConsts.C!=3 || RecoveryConsts.D!=4) "
                     "throw new Exception(\"constant recovery/state/precedence\");";
        check_source(vm, "retry-same-vm.cs", verify);
        check_inventory(vm, module);
        mcs_gc(vm);
        check_source(vm, "state-after-gc.cs",
                     "if (saved!=99 || kept[0]!=37 || kept[1]!=41 || BeforeSibling.Mark!=31 || "
                     "AfterSibling.Mark!=39) throw new Exception(\"state after GC\");");
    }
    physical_finish(vm);
    return seen;
}
int main(void) {
    printf("FLAGS lazy-classes=%d lazy-regs=%d gc-stress=%d compact=%d int64=%d\n",
           MCS_LAZY_CLASSES, MCS_LAZY_REGS, MCS_GC_STRESS, MCS_COMPACT_VALUES, MCS_INT64);
    for (int module = 0; module <= 1; module++)
        for (int nested = 0; nested <= 1; nested++) {
            size_t count = run_case(module, nested, -1, 0);
            CHECK(count <= MAX_WINDOW);
            int lazy = module ? MCS_LAZY_CLASSES && MCS_LAZY_REGS : MCS_LAZY_CLASSES;
            if (lazy && count <= MAX_WINDOW)
                for (size_t fault = 0; fault < count; fault++)
                    run_case(module, nested, (int)fault, 1);
            else
                printf("EAGER semantics only; no equivalent first-use lazy allocation coverage "
                       "claimed\n");
            if (count <= MAX_WINDOW)
                run_case(module, nested, (int)count, 0);
        }
    printf("%u/%u lazy module recovery checks passed; finite trusted operation, not general OOM "
           "safety\n",
           checks - failures, checks);
    return failures != 0;
}
