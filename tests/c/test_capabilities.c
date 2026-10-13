/* Capability discovery must not allocate, start radios or infer missing roles. */
#include "mcs.h"
#include "mcs_hal.h"
#include "mcs_ble.h"
#include "mcs_net.h"
#include <stdio.h>
#include <string.h>
static int failures, checks;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
#if MCS_ENABLE_BLE
static int starts, writes, stop_fails;
static int radio(void* ctx, int on) { (void)ctx; if (!on && stop_fails) return MCS_HAL_EBUSY; starts += on; return 0; }
static int setting(void* ctx, int opt, int32_t value) { (void)ctx; (void)value; writes++; return opt == MCS_BLE_OPT_PASSKEY ? MCS_HAL_ENOTSUP : 0; }
#endif
int main(void) {
    mcs_config_t config; mcs_config_default(&config); mcs_vm_t* vm = mcs_new(&config);
    CHECK(vm != NULL);
    CHECK(!mcs_global_exists(vm, "DoesNotExist"));
    mcs_set_global(vm, "DefinedNull", mcs_null());
    CHECK(mcs_global_exists(vm, "DefinedNull"));
    CHECK(!mcs_capability_has(vm, "Imaginary"));
    CHECK(!mcs_capability_has(NULL, "GPIO"));
    CHECK(!mcs_capability_has(vm, NULL));
#if MCS_ENABLE_NET
    mcs_net_set(NULL);
#endif
#if MCS_ENABLE_HAL
    mcs_hal_t hal; memset(&hal, 0, sizeof hal); mcs_hal_open_lib(vm, &hal);
    CHECK(mcs_capability_has(vm, "Board"));
    CHECK(!mcs_capability_has(vm, "GPIO"));
    CHECK(!mcs_capability_has(vm, "GPIO.IRQ"));
    CHECK(!mcs_capability_has(vm, "Wifi"));
    CHECK(!mcs_capability_has(vm, "EspNow"));
    CHECK(mcs_exec_source(vm, "guard.cs", "if (!Hal.Has(\"Wifi\")) return; Wifi.Connect(\"demo\", \"demo\");") == MCS_OK);
    CHECK(mcs_exec_source(vm, "guard.cs", "if (!Hal.Has(\"Ble\")) return; var svc = new BleService(\"180F\");") == MCS_OK);
#endif
#if MCS_ENABLE_BLE
    mcs_ble_t ble; memset(&ble, 0, sizeof ble); ble.enable = radio; ble.opt_set = setting;
    ble.writable_options = MCS_BLE_OPTION(MCS_BLE_OPT_MTU);
    mcs_ble_set(&ble); mcs_ble_open_lib(vm, &ble);
    CHECK(mcs_ble_has(&ble, "Ble")); CHECK(!mcs_ble_has(&ble, "Ble.Scan"));
    CHECK(!mcs_ble_has(&ble, "Ble.Security")); CHECK(!mcs_ble_has(&ble, "Ble.Central"));
    CHECK(mcs_exec_source(vm, "options.cs", "if (!Ble.CanSetOption(\"Mtu\") || Ble.CanSetOption(\"Passkey\") || Ble.CanReadOption(\"Mtu\") || Ble.CanSetOption(\"unknown\") || Ble.Enabled) throw new Exception(\"bad option report\");") == MCS_OK);
    CHECK(starts == 0 && writes == 0);
    CHECK(mcs_exec_source(vm, "passkey.cs", "try { Ble.Passkey = 123456; throw new Exception(\"must fail\"); } catch (NotSupportedException e) {} if (Ble.Passkey != -1) throw new Exception(\"cached unsupported key\"); Ble.Reset();") == MCS_OK);
    CHECK(writes == 1 && starts == 1);
    CHECK(mcs_exec_source(vm, "enable.cs", "Ble.Enable();") == MCS_OK);
    stop_fails = 1;
    CHECK(mcs_exec_source(vm, "stop.cs", "try { Ble.Reset(); throw new Exception(\"must fail\"); } catch (IOException e) {} if (!Ble.Enabled) throw new Exception(\"must remain on\");") == MCS_OK);
    stop_fails = 0;
    CHECK(mcs_exec_source(vm, "stop.cs", "Ble.Reset(); if (Ble.Enabled) throw new Exception(\"still on\");") == MCS_OK);
#endif
    mcs_mem_stats_t before, after; mcs_mem_stats(vm, &before);
    size_t count; const char* const* names = mcs_capability_names(&count);
    CHECK(count > 80);
    for (int repeat = 0; repeat < 50; repeat++) {
        for (size_t i = 0; i < count; i++) (void)mcs_capability_has(vm, names[i]);
        CHECK(!mcs_global_exists(vm, "UninternedMissingName"));
    }
    mcs_mem_stats(vm, &after);
    CHECK(before.bytes_in_use == after.bytes_in_use && before.objects == after.objects && before.collections == after.collections);
#if MCS_ENABLE_BLE
    CHECK(starts == 2 && writes == 1);
    mcs_ble_set(NULL);
#endif
    mcs_free(vm);
    printf("%d/%d capability checks passed\n", checks - failures, checks);
    return failures != 0;
}
