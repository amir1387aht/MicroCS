/* ROM globals are optional on very small targets, but stay enabled by default. */
#include "mcs.h"
#include <stdio.h>

static mcs_value_t answer(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    (void)vm; (void)self; (void)argc; (void)argv;
    return mcs_int(42);
}
static const mcs_reg_t fns[] = { MCS_FN("RomAnswer", answer, 0) };
static const mcs_const_t constants[] = { MCS_CONST("RomConstant", 7) };
static const mcs_rom_globals_t table = { fns, 1, constants, 1, NULL, 0 };
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL ROM globals: %s (line %d)\n", #x, __LINE__); return 1; } } while (0)

int main(void) {
    mcs_config_t cfg; mcs_config_default(&cfg);
    mcs_vm_t* vm = mcs_new(&cfg); CHECK(vm != NULL);
#if MCS_ROM_GLOBALS_MAX > 0
    CHECK(mcs_register_rom_globals(vm, &table));
    CHECK(mcs_register_rom_globals(vm, &table)); /* duplicate registration is harmless */
    CHECK(mcs_global_exists(vm, "RomAnswer"));
    CHECK(mcs_global_exists(vm, "RomConstant"));
    CHECK(mcs_exec_source(vm, "rom.cs", "if (RomAnswer() != 42 || RomConstant != 7) throw new Exception(\"ROM\");") == MCS_OK);
#else
    CHECK(!mcs_register_rom_globals(vm, &table));
    CHECK(!mcs_global_exists(vm, "RomAnswer"));
    CHECK(!mcs_global_exists(vm, "RomConstant"));
#endif
    /* The existing registration and lazy class paths still work either way. */
    mcs_register_function(vm, "OrdinaryAnswer", answer, 0);
    CHECK(mcs_exec_source(vm, "ordinary.cs", "if (OrdinaryAnswer() != 42) throw new Exception(\"ordinary\");") == MCS_OK);
    mcs_free(vm);
    printf("PASS ROM globals (max=%d)\n", MCS_ROM_GLOBALS_MAX);
    return 0;
}
