/* Classic CAN: one API for native controllers and an external MCP2515 on any
 * SPI/GPIO port. Timed waits are cooperative; callbacks never run in an ISR. */
#include "mcs_hal_internal.h"
#include "mcs_can_timing.h"
#if MCS_ENABLE_HAL
#include <string.h>
#include <stdio.h>
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
static int number(mcs_vm_t* vm, mcs_value_t v, mcs_int_t min, mcs_int_t max, const char* what) {
    mcs_int_t n = mcs_to_int(vm, v);
    if (mcs_has_exception(vm)) return -1;
    if (n < min || n > max) { mcs__range_fail(vm, what); return -1; }
    return (int)n;
}
static int bus_arg(mcs_vm_t* vm, mcs_value_t v) { return number(vm, v, 0, MCS_HAL_MAX_BUSES - 1, "CAN bus"); }
static bool arity(mcs_vm_t* vm, int argc, int min, int max) {
    if (argc >= min && argc <= max) return true;
    mcs_raise(vm, "ArgumentException", "CAN: expected %d..%d arguments", min, max); return false;
}
static bool main_vm(mcs_vm_t* vm) {
    if (!ST()->no_events) return true;
    mcs__hal_fail(vm, "CAN: use the main VM, not a worker", MCS_HAL_EBUSY); return false;
}
static int prepare(mcs_vm_t* vm, mcs__can_bus_t* b) {
    int bus = b->mcp.spi_bus;
    if (!mcs__dma_idle(vm, MCS__DMA_SPI, bus)) return MCS_HAL_EBUSY;
    const mcs_spi_cfg_t cfg = { 1000000, 0, 8, 0, 0 };
    return mcs_hal_spi_config(vm, bus, &cfg);
}
static int close_bus(mcs_vm_t* vm, int bus) {
    mcs__can_bus_t* b = &ST()->can[bus];
    int r = 0;
    if (b->backend == 2) { r = prepare(vm, b); if (!r) r = mcs_mcp2515_close(&b->mcp); }
    else if (b->backend == 1) r = HAL()->can_close ? HAL()->can_close(CTX(), bus) : MCS_HAL_ENOTSUP;
    if (r < 0) return r;
    b->backend = b->ready = b->notified = 0;
    return 0;
}
void mcs__can_close_all(mcs_vm_t* vm) {
    if (!ST() || !HAL()) return;
    for (int i = 0; i < MCS_HAL_MAX_BUSES; i++) (void)close_bus(vm, i);
}
static int try_recv(mcs_vm_t* vm, int bus, mcs_can_frame_t* f) {
    mcs__can_bus_t* b = &ST()->can[bus];
    if (b->ready) { *f = b->pending; b->ready = b->notified = 0; return 0; }
    int r = MCS_HAL_ENODEV;
    if (b->backend == 2) { r = prepare(vm, b); if (!r) r = mcs_mcp2515_try_recv(&b->mcp, f); }
    else if (b->backend == 1 && HAL()->can_recv) r = HAL()->can_recv(CTX(), bus, f, 0);
    if (r >= 0) {
        if (f->len > 8 || f->id > (f->extended ? 0x1fffffffu : 0x7ffu)) return MCS_HAL_EINVAL;
        if (f->rtr) memset(f->data, 0, sizeof f->data);
    }
    return r;
}
void mcs__can_subscribe(mcs_vm_t* vm, int bus, bool enabled) {
    ST()->can[bus].subscribed = enabled; ST()->can[bus].notified = 0;
}
int mcs__can_event(mcs_vm_t* vm, int bus, mcs_hal_event_t* ev) {
    mcs__can_bus_t* b = &ST()->can[bus];
    if (!b->subscribed || !b->backend || b->notified) return 0;
    if (!b->ready) {
        int r = try_recv(vm, bus, &b->pending);
        if (mcs_has_exception(vm)) return -1;
        if (r == MCS_HAL_ETIMEOUT) return 0;
        if (r < 0) { mcs__hal_fail(vm, "CAN.OnReceive", r); return -1; }
        b->ready = 1;
    }
    b->notified = 1;
    ev->type = MCS_HAL_EV_CAN; ev->reserved = 0; ev->source = (uint16_t)bus; ev->value = 1;
    return 1;                       /* pending is a lower bound: drain Receive until null */
}

typedef struct { mcs_can_frame_t f; } canframe_t;
static const mcs_class_def_t canframe_def;
static bool frame_from(mcs_vm_t* vm, mcs_can_frame_t* f, mcs_value_t id, mcs_value_t data, bool ext) {
    memset(f, 0, sizeof *f);
    int i = number(vm, id, 0, ext ? 0x1fffffff : 0x7ff, "CAN identifier");
    if (i < 0) return false;
    uint8_t buf[MCS_HAL_MAX_XFER]; size_t n;
    if (!mcs__to_bytes(vm, data, buf, &n)) return false;
    if (n > 8) { mcs__range_fail(vm, "CAN payload (max 8 bytes)"); return false; }
    f->id = (uint32_t)i; f->extended = ext; f->len = (uint8_t)n;
    memcpy(f->data, buf, n); return true;
}
static void canframe_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    if (!arity(vm, argc, 2, 4)) return;
    canframe_t* c = (canframe_t*)mcs_userdata(self);
    if (frame_from(vm, &c->f, argv[0], argv[1], argc > 2 && mcs_truthy(argv[2]))) {
        c->f.rtr = argc > 3 && mcs_truthy(argv[3]);
        if (c->f.rtr) memset(c->f.data, 0, sizeof c->f.data);
    }
}
#define CF() canframe_t* c = (canframe_t*)mcs_check_userdata(vm, self, &canframe_def); if (!c) return mcs_null()
NATIVE(cf_id) { CF(); return mcs_int((mcs_int_t)c->f.id); }
NATIVE(cf_ext) { CF(); return mcs_bool(c->f.extended); }
NATIVE(cf_rtr) { CF(); return mcs_bool(c->f.rtr); }
NATIVE(cf_len) { CF(); return mcs_int(c->f.len); }
NATIVE(cf_data) { CF(); return mcs__bytes_val(vm, c->f.data, c->f.rtr ? 0 : c->f.len); }
NATIVE(cf_tostring) {
    CF(); char s[52];
    int k = snprintf(s, sizeof s, c->f.extended ? "%08X [%u]" : "%03X [%u]", (unsigned)c->f.id, (unsigned)c->f.len);
    if (c->f.rtr) snprintf(s + k, sizeof s - (size_t)k, " RTR");
    else for (int i = 0; i < c->f.len && k < (int)sizeof s - 4; i++) k += snprintf(s + k, sizeof s - (size_t)k, " %02X", c->f.data[i]);
    return mcs_string(vm, s);
}
static const mcs_reg_t canframe_members[] = {
    MCS_GET("Id", cf_id), MCS_GET("Extended", cf_ext), MCS_GET("Remote", cf_rtr), MCS_GET("Length", cf_len),
    MCS_GET("Data", cf_data), MCS_FN("ToString", cf_tostring, 0), MCS_REG_END
};
static const mcs_class_def_t canframe_def = { "CanFrame", sizeof(canframe_t), canframe_ctor, NULL, canframe_members, NULL };
NATIVE(can_open) {
    if (!arity(vm, argc, 1, 2) || !main_vm(vm)) return mcs_null();
    int bus = bus_arg(vm, argv[0]); if (bus < 0) return mcs_null();
    mcs_can_config_t cfg; mcs_can_config_init(&cfg, 500000);
    const mcs_can_config_t* given = argc == 2 ? (const mcs_can_config_t*)mcs__cfg_get(argv[1], MCS__CFG_CAN) : NULL;
    if (given) cfg = *given;
    else if (argc == 2) { int rate = number(vm, argv[1], 1, 1000000, "CAN bitrate"); if (rate < 0) return mcs_null(); cfg.bitrate = (uint32_t)rate; }
    if (!HAL()->can_configure && !HAL()->can_open) return mcs__hal_fail(vm, "CAN.Open: no native controller; use OpenMcp2515", MCS_HAL_ENOTSUP);
    if (!HAL()->can_send || !HAL()->can_recv) return mcs__hal_fail(vm, "CAN.Open", MCS_HAL_ENOTSUP);
    if (!HAL()->can_configure && (cfg.tx != -1 || cfg.rx != -1 || cfg.mode || cfg.opts.n)) return mcs__hal_fail(vm, "CAN.Open: settings", MCS_HAL_ENOTSUP);
    RC("CAN.Open: close previous controller", close_bus(vm, bus));
    int r = HAL()->can_configure ? HAL()->can_configure(CTX(), bus, &cfg) : HAL()->can_open(CTX(), bus, cfg.bitrate);
    RC("CAN.Open", r);
    ST()->can[bus].backend = 1; ST()->can[bus].mode = cfg.mode;
    return mcs_null();
}
NATIVE(can_mcp_open) {
    if (!arity(vm, argc, 3, 6) || !main_vm(vm)) return mcs_null();
    int bus = bus_arg(vm, argv[0]); int spi = number(vm, argv[1], 0, MCS_HAL_MAX_BUSES - 1, "SPI bus");
    int cs = mcs_is_string(argv[2]) ? mcs__pin_arg(vm, argv[2]) : number(vm, argv[2], 0, 32767, "MCP2515 CS pin");
    int osc = argc > 3 ? number(vm, argv[3], 8000000, 20000000, "MCP2515 oscillator") : 16000000;
    int rate = argc > 4 ? number(vm, argv[4], 1, 1000000, "CAN bitrate") : 500000;
    int mode = argc > 5 ? number(vm, argv[5], 0, MCS_CAN_LOOPBACK, "CAN mode") : MCS_CAN_NORMAL;
    if (mcs_has_exception(vm)) return mcs_null();
    if (cs < 0 || cs > 32767) return mcs__range_fail(vm, "MCP2515 CS pin");
    mcs_can_timing_t timing = { 0 };
    if ((osc != 8000000 && osc != 16000000 && osc != 20000000) || !mcs_can_timing((uint32_t)osc / 2, (uint32_t)rate, 64, 16, 8, 2, &timing)) return mcs__range_fail(vm, "MCP2515 oscillator / exact bitrate");
    if (!mcs_capability_has(vm, "CAN.Mcp2515")) return mcs__hal_fail(vm, "CAN.OpenMcp2515", MCS_HAL_ENOTSUP);
    for (int i = 0; i < MCS_HAL_MAX_BUSES; i++)
        if (i != bus && ST()->can[i].backend == 2 && ST()->can[i].mcp.cs == cs)
            return mcs__hal_fail(vm, "CAN.OpenMcp2515: CS already in use", MCS_HAL_EBUSY);
    RC("CAN.OpenMcp2515: close previous controller", close_bus(vm, bus));
    mcs__can_bus_t* b = &ST()->can[bus];
    b->mcp.spi_bus = (int16_t)spi;
    RC("CAN.OpenMcp2515: SPI", prepare(vm, b));
    RC("CAN.OpenMcp2515", mcs_mcp2515_open(&b->mcp, HAL(), spi, cs, (uint32_t)osc, (uint32_t)rate, mode));
    b->backend = 2; b->mode = (uint8_t)mode;
    return mcs_null();
}
NATIVE(can_close) {
    if (!main_vm(vm)) return mcs_null();
    int bus = bus_arg(vm, argv[0]); if (bus < 0) return mcs_null();
    RC("CAN.Close", close_bus(vm, bus));
    mcs_value_t args[2] = { mcs_int(bus), mcs_null() };
    return mcs__can_onreceive(vm, self, 2, args);
}
NATIVE(can_send) {
    if (!arity(vm, argc, 2, 5) || !main_vm(vm)) return mcs_null();
    int bus = bus_arg(vm, argv[0]); if (bus < 0) return mcs_null();
    mcs_can_frame_t f; int timeout = 100;
    canframe_t* c = (canframe_t*)mcs_userdata_of(argv[1], &canframe_def);
    if (c) {
        if (!arity(vm, argc, 2, 3)) return mcs_null();
        f = c->f; if (argc == 3) timeout = number(vm, argv[2], 0, 2147483647, "CAN timeout");
    } else {
        if (!arity(vm, argc, 3, 5) || !frame_from(vm, &f, argv[1], argv[2], argc > 3 && mcs_truthy(argv[3]))) return mcs_null();
        if (argc == 5) timeout = number(vm, argv[4], 0, 2147483647, "CAN timeout");
    }
    if (mcs_has_exception(vm)) return mcs_null();
    uint32_t t0 = mcs_ticks(vm);
    for (;;) {
        mcs__can_bus_t* b = &ST()->can[bus];
        if (!b->backend) return mcs__hal_fail(vm, "CAN.Send: open the bus first", MCS_HAL_ENODEV);
        if (b->mode == MCS_CAN_LISTEN_ONLY) return mcs__hal_fail(vm, "CAN.Send: listen-only", MCS_HAL_ENOTSUP);
        int r;
        if (b->backend == 2) { r = prepare(vm, b); if (!r) r = mcs_mcp2515_try_send(&b->mcp, &f); }
        else r = HAL()->can_send(CTX(), bus, &f, 0);
        if (r >= 0) return mcs_null();
        if (mcs_has_exception(vm)) return mcs_null();
        if (r != MCS_HAL_ETIMEOUT && r != MCS_HAL_EBUSY) return mcs__hal_fail(vm, "CAN.Send", r);
        if ((uint32_t)(mcs_ticks(vm) - t0) >= (uint32_t)timeout) return mcs__hal_fail(vm, "CAN.Send", MCS_HAL_ETIMEOUT);
        mcs_sleep(vm, 1); if (mcs_safepoint(vm)) return mcs_null();
    }
}
NATIVE(can_recv) {
    if (!arity(vm, argc, 1, 2) || !main_vm(vm)) return mcs_null();
    int bus = bus_arg(vm, argv[0]); int timeout = argc > 1 ? number(vm, argv[1], 0, 2147483647, "CAN timeout") : 0;
    if (mcs_has_exception(vm)) return mcs_null();
    uint32_t t0 = mcs_ticks(vm); mcs_can_frame_t f;
    for (;;) {
        int r = try_recv(vm, bus, &f);
        if (mcs_has_exception(vm)) return mcs_null();
        if (r >= 0) break;
        if (r != MCS_HAL_ETIMEOUT) return mcs__hal_fail(vm, "CAN.Receive", r);
        if ((uint32_t)(mcs_ticks(vm) - t0) >= (uint32_t)timeout) return mcs_null();
        mcs_sleep(vm, 1); if (mcs_safepoint(vm)) return mcs_null();
    }
    mcs_value_t obj, args[2] = { mcs_int(0), mcs_null() };
    if (mcs_new_object(vm, "CanFrame", 2, args, &obj) != MCS_OK) return mcs_null();
    ((canframe_t*)mcs_userdata(obj))->f = f; return obj;
}
static const mcs_reg_t can_fns[] = {
    MCS_FN("Open", can_open, -1), MCS_FN("OpenMcp2515", can_mcp_open, -1), MCS_FN("Close", can_close, 1),
    MCS_FN("Send", can_send, -1), MCS_FN("Receive", can_recv, -1), MCS_FN("OnReceive", mcs__can_onreceive, 2), MCS_REG_END
};
static const mcs_const_t can_consts[] = {
    MCS_CONST("Normal", MCS_CAN_NORMAL), MCS_CONST("ListenOnly", MCS_CAN_LISTEN_ONLY), MCS_CONST("Loopback", MCS_CAN_LOOPBACK), MCS_CONST_END
};
void mcs__can_open(mcs_vm_t* vm) {
    if (mcs_capability_has(vm, "CAN") || mcs_capability_has(vm, "CAN.Mcp2515")) {
        mcs_register_module(vm, "CAN", can_fns); mcs_register_consts(vm, "CAN", can_consts); mcs_register_class(vm, &canframe_def);
    }
}
#endif
