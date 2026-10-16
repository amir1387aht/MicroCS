/* Power policy tests: advertised support, validation, state and unsafe entry. */
#include "mcs_hal_internal.h"
#include <stdio.h>
#include <string.h>
static int failures, checks;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static mcs_hal_t hal;
static mcs_hal_sim_t sim;
static int rejected(void* ctx, int mode, uint32_t ms, int pin, int high) {
    (void)ctx; (void)mode; (void)ms; (void)pin; (void)high;
    return MCS_HAL_EBUSY;
}
static void run(mcs_vm_t* vm, const char* code) {
    mcs_result_t rc = mcs_exec_source(vm, "power.cs", code);
    CHECK(rc == MCS_OK);
    if (rc != MCS_OK) printf("  %s\n", mcs_last_error(vm));
}
int main(void) {
    mcs_config_t cfg; mcs_config_default(&cfg);
    mcs_vm_t* vm = mcs_new(&cfg); CHECK(vm != NULL);
    mcs_hal_sim_init(&hal, &sim); mcs_hal_open_lib(vm, &hal);
    CHECK(mcs_capability_has(vm, "Power.Sleep"));
    CHECK(mcs_capability_has(vm, "Power.LightSleep.Timer"));
    CHECK(mcs_capability_has(vm, "Power.DeepSleep.Pin"));
    CHECK(mcs_capability_has(vm, "Power.Shutdown"));
    run(vm, "if (Power.WakePin != -1 || Power.WakeHigh) throw new Exception(); Power.Sleep(0);");
    run(vm, "try { Power.Sleep(-1); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
            "try { Power.LightSleep(-1); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
            "try { Power.LightSleep(1, 2); throw new Exception(); } catch (ArgumentException e) {} "
            "try { Power.LightSleep(); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
            "try { Power.DeepSleep(); throw new Exception(); } catch (ArgumentOutOfRangeException e) {}");
    CHECK(sim.power_calls == 0);
    run(vm, "Power.SetWakePin(2, true); if (Power.WakePin != 2 || !Power.WakeHigh) throw new Exception(); "
            "try { Power.SetWakePin(64, false); throw new Exception(); } catch (ArgumentException e) {} "
            "if (Power.WakePin != 2 || !Power.WakeHigh) throw new Exception();");
    CHECK(sim.power_calls == 0);
    run(vm, "GPIO.OnChange(2, GPIO.Both, (p, level) => {}); "
            "try { Power.LightSleep(1); throw new Exception(); } catch (IOException e) {} GPIO.Off(2);");
    CHECK(sim.power_calls == 0);
    run(vm, "Power.LightSleep(123);");
    CHECK(sim.power_calls == 1 && sim.power_ms == 123 && sim.power_pin == 2 && sim.power_high == 1);
    run(vm, "Power.DeepSleep();");
    CHECK(sim.power_mode == MCS_POWER_DEEP && sim.power_ms == 0 && sim.power_pin == 2);
    run(vm, "Power.ClearWakePin(); Power.DeepSleep(456);");
    CHECK(sim.power_ms == 456 && sim.power_pin == -1);
    run(vm, "Power.Shutdown();");
    CHECK(sim.power_mode == MCS_POWER_SHUTDOWN);
    unsigned calls = sim.power_calls;
    hal.power_timer_modes = 0;
    CHECK(!mcs_capability_has(vm, "Power.LightSleep.Timer"));
    run(vm, "try { Power.LightSleep(1); throw new Exception(); } catch (NotSupportedException e) {}");
    hal.power_timer_modes = 7;
    hal.power_pin_modes = MCS_POWER_LIGHT;
    run(vm, "Power.SetWakePin(2, false); try { Power.DeepSleep(1); throw new Exception(); } catch (NotSupportedException e) {} Power.ClearWakePin();");
    hal.power_modes = MCS_POWER_LIGHT;
    CHECK(!mcs_capability_has(vm, "Power.DeepSleep"));
    run(vm, "try { Power.DeepSleep(1); throw new Exception(); } catch (NotSupportedException e) {}");
    hal.power_validate = rejected;
    run(vm, "try { Power.SetWakePin(1, true); throw new Exception(); } catch (IOException e) {} "
            "if (Power.WakePin != -1) throw new Exception(); "
            "try { Power.LightSleep(1); throw new Exception(); } catch (IOException e) {}");
    mcs_hal_sim_init(&hal, &sim); calls = 0;
    mcs__hal_state_t* state = mcs_get_ext(vm, MCS_EXT_HAL);
    state->dma[0].kind = MCS__DMA_MEM;
    run(vm, "try { Power.LightSleep(1); throw new Exception(); } catch (IOException e) {}");
    state->dma[0].kind = MCS__DMA_FREE;
    state->no_events = 1;
    run(vm, "try { Power.LightSleep(1); throw new Exception(); } catch (IOException e) {} Power.Sleep(0);");
    state->no_events = 0;
    CHECK(sim.power_calls == calls);
    memset(&hal, 0, sizeof hal);
    CHECK(!mcs_capability_has(vm, "Power.LightSleep"));
    run(vm, "try { Power.SetWakePin(2, true); throw new Exception(); } catch (NotSupportedException e) {} "
            "try { Power.Shutdown(); throw new Exception(); } catch (NotSupportedException e) {} Power.Sleep(0);");
    mcs_free(vm);
    printf("%d/%d power policy checks passed\n", checks - failures, checks);
    return failures != 0;
}
