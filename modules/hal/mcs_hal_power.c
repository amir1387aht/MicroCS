/* MicroCS power policy. Hardware sleep is deliberately distinct from VM delay. */
#include "mcs_hal_internal.h"
#include "mcs_threads.h"
#if MCS_ENABLE_HAL
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

static int duration(mcs_vm_t* vm, int argc, mcs_value_t* argv, uint32_t* ms) {
    if (argc > 1) { mcs_raise(vm, "ArgumentException", "Power: expected zero or one timeout"); return 0; }
    mcs_int_t n = argc ? mcs_to_int(vm, argv[0]) : 0;
    if (mcs_has_exception(vm)) return 0;
    if (n < 0 || (uint64_t)n > 2147483647u) {
        mcs__range_fail(vm, "Power: milliseconds must be 0..2147483647"); return 0;
    }
    *ms = (uint32_t)n;
    return 1;
}
NATIVE(power_sleep) {
    uint32_t ms;
    if (duration(vm, argc, argv, &ms)) mcs_sleep(vm, ms);
    return mcs_null();
}
NATIVE(power_set_pin) {
    int pin = mcs__pin_arg(vm, argv[0]);
    if (mcs_has_exception(vm)) return mcs_null();
    if (pin < 0) return mcs__range_fail(vm, "Power.SetWakePin: invalid pin");
    if (!HAL()) return mcs__hal_fail(vm, "Power.SetWakePin", MCS_HAL_ENOTSUP);
    NEED(power_validate, "Power.SetWakePin");
    if (!HAL()->power_enter || !(HAL()->power_modes & HAL()->power_pin_modes))
        return mcs__hal_fail(vm, "Power.SetWakePin", MCS_HAL_ENOTSUP);
    int high = mcs_truthy(argv[1]);
    RC("Power.SetWakePin", HAL()->power_validate(CTX(), 0, 0, pin, high));
    ST()->wake_pin = pin; ST()->wake_high = (uint8_t)high;
    return mcs_null();
}
NATIVE(power_clear_pin) { ST()->wake_pin = -1; ST()->wake_high = 0; return mcs_null(); }
NATIVE(power_pin) { return mcs_int(ST()->wake_pin); }
NATIVE(power_high) { return mcs_bool(ST()->wake_high != 0); }

static mcs_value_t enter(mcs_vm_t* vm, int mode, uint32_t ms) {
    const mcs_hal_t* h = HAL();
    mcs__hal_state_t* s = ST();
    if (!h || !h->power_enter || !h->power_validate || !(h->power_modes & (unsigned)mode))
        return mcs__hal_fail(vm, "Power", MCS_HAL_ENOTSUP);
    if (ms && !(h->power_timer_modes & (unsigned)mode))
        return mcs__hal_fail(vm, "Power: timer wake", MCS_HAL_ENOTSUP);
    if (s->wake_pin >= 0 && !(h->power_pin_modes & (unsigned)mode))
        return mcs__hal_fail(vm, "Power: GPIO wake", MCS_HAL_ENOTSUP);
    /* Light/deep requests must have a way to wake. Shutdown is explicitly reset-only
     * on boards without a configured wake source (e.g. Zephyr sys_poweroff). */
    if (mode != MCS_POWER_SHUTDOWN && !ms && s->wake_pin < 0)
        return mcs__range_fail(vm, "Power: select a wake pin or positive timeout");
    /* ESP GPIO wake programs the pad interrupt type. Do not destroy an existing
     * MicroCS IRQ registration (native application IRQs must be quiesced by it). */
    if (s->wake_pin >= 0 && mcs__hal_pin_has_irq(vm, s->wake_pin))
        return mcs__hal_fail(vm, "Power: wake pin has GPIO IRQ", MCS_HAL_EBUSY);
    if (s->no_events) return mcs__hal_fail(vm, "Power: worker VM", MCS_HAL_EBUSY);
#if MCS_ENABLE_THREADS
    if (mcs_threads_active()) return mcs__hal_fail(vm, "Power: active workers", MCS_HAL_EBUSY);
#endif
    for (int i = 0; i < MCS_HAL_MAX_DMA; i++)
        if (s->dma[i].kind != MCS__DMA_FREE)
            return mcs__hal_fail(vm, "Power: active DMA", MCS_HAL_EBUSY);
    for (int i = 0; i < MCS_HAL_MAX_BUSES; i++)
        if (s->can[i].backend) return mcs__hal_fail(vm, "Power: close CAN before hardware sleep", MCS_HAL_EBUSY);
    RC("Power", h->power_validate(h->ctx, mode, ms, s->wake_pin, s->wake_high));
    if (mcs_safepoint(vm)) return mcs_null();
    RC("Power", h->power_enter(h->ctx, mode, ms, s->wake_pin, s->wake_high));
    return mcs_null();
}
NATIVE(power_light) {
    uint32_t ms;
    if (!duration(vm, argc, argv, &ms)) return mcs_null();
    return enter(vm, MCS_POWER_LIGHT, ms);
}
NATIVE(power_deep) {
    uint32_t ms;
    if (!duration(vm, argc, argv, &ms)) return mcs_null();
    return enter(vm, MCS_POWER_DEEP, ms);
}
NATIVE(power_shutdown) { return enter(vm, MCS_POWER_SHUTDOWN, 0); }
static const mcs_reg_t power_fns[] = {
    MCS_FN("Sleep", power_sleep, 1), MCS_FN("SetWakePin", power_set_pin, 2),
    MCS_FN("ClearWakePin", power_clear_pin, 0),
    MCS_GET("WakePin", power_pin), MCS_GET("WakeHigh", power_high),
    MCS_FN("LightSleep", power_light, -1), MCS_FN("DeepSleep", power_deep, -1),
    MCS_FN("Shutdown", power_shutdown, 0), MCS_REG_END
};
void mcs__power_open(mcs_vm_t* vm) { mcs_register_module(vm, "Power", power_fns); }
#endif
