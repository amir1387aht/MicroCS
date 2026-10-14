/* CAN FD common HAL and VM contracts. Ordinary source/trusted compiler images
 * only; no physical bus traffic, handcrafted images or controller simulation. */
#include "../../src/mcs_internal.h"
#include "mcs_hal.h"
#include "mcs_hal_internal.h"
#include "mcs_ble.h"
#include <limits.h>
#include <stddef.h>
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
static size_t live;
typedef union {
    size_t bytes;
    long double alignment;
    void *pointer;
} block_t;
static void *tracked(void *ud, void *ptr, size_t old, size_t n) {
    (void)ud;
    (void)old;
    block_t *block = ptr ? (block_t *)ptr - 1 : NULL;
    size_t previous = block ? block->bytes : 0;
    if (!n) {
        live -= previous;
        free(block);
        return NULL;
    }
    block_t *next = (block_t *)realloc(block, sizeof(*next) + n);
    if (!next)
        return NULL;
    next->bytes = n;
    live = live - previous + n;
    return next + 1;
}
static char output[4096];
static size_t used;
static void capture(void *ud, const char *data, size_t n) {
    (void)ud;
    if (used + n < sizeof output) {
        memcpy(output + used, data, n);
        used += n;
        output[used] = 0;
    }
}
static mcs_vm_t *vm_new(void) {
    mcs_config_t cfg;
    mcs_config_default(&cfg);
    /* Host semantic/profile variants, not a minimum-device-RAM claim. Compiler
     * source and fixture compilation require additional host test workspace. */
    cfg.heap_limit = 256 * 1024;
    cfg.stack_slots = 512;
    cfg.realloc_fn = tracked;
    cfg.write_fn = cfg.error_fn = capture;
    used = 0;
    output[0] = 0;
    mcs_vm_t *vm = mcs_new(&cfg);
    CHECK(vm != NULL);
    return vm;
}
static void vm_done(mcs_vm_t *vm) {
    mcs_free(vm);
    CHECK(live == 0);
}
static void test_layout_and_dlc(void) {
    CHECK(MCS_HAL_EV_CAN_FD != MCS_HAL_EV_BLE);
    CHECK(sizeof(mcs_can_frame_t) == 16);
    CHECK(offsetof(mcs_can_frame_t, data) == 7);
    CHECK(offsetof(mcs_hal_t, can_fd_caps) ==
          offsetof(mcs_hal_t, qspi_command_async) + sizeof(((mcs_hal_t *)0)->qspi_command_async));
    const unsigned lengths[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    for (unsigned dlc = 0; dlc < 16; dlc++) {
        CHECK(mcs_can_fd_dlc_to_len(dlc) == lengths[dlc]);
        CHECK(mcs_can_fd_len_to_dlc(lengths[dlc]) == dlc);
    }
    for (unsigned n = 0; n <= 64; n++) {
        unsigned dlc = mcs_can_fd_len_to_dlc(n), canonical = mcs_can_fd_dlc_to_len(dlc);
        CHECK(canonical >= n && canonical <= 64);
        CHECK(dlc == 0 || mcs_can_fd_dlc_to_len(dlc - 1) < n);
    }
    CHECK(mcs_can_fd_len_to_dlc(65) == 255);
    CHECK(mcs_can_fd_len_to_dlc(UINT_MAX) == 255);
    CHECK(mcs_can_fd_dlc_to_len(16) == 255);
    mcs_hal_t legacy = {.board = "legacy", .can_send = NULL};
    CHECK(!mcs_hal_can_fd_available(&legacy));
    printf("SIZES hal=%zu simulator=%zu classic=%zu fd=%zu\n", sizeof(mcs_hal_t),
           sizeof(mcs_hal_sim_t), sizeof(mcs_can_frame_t), sizeof(mcs_can_fd_frame_t));
}
#if MCS_ENABLE_HAL
#if MCS_ENABLE_COMPILER
static int opens, closes, sends, received_mode, fail_open, fail_close;
static int caps_mask = MCS_CAN_FD_SUPPORTED | MCS_CAN_FD_SUPPORTS_BRS;
static uint32_t last_timeout;
static int spy_caps(void *ctx, int bus) {
    (void)ctx;
    return bus == 0 ? caps_mask : MCS_HAL_ENOTSUP;
}
static int spy_open(void *ctx, int bus, uint32_t nominal, uint32_t data) {
    (void)ctx;
    (void)bus;
    (void)nominal;
    (void)data;
    opens++;
    return fail_open ? MCS_HAL_EBUSY : 0;
}
static int spy_send(void *ctx, int bus, const mcs_can_fd_frame_t *f, uint32_t timeout) {
    (void)ctx;
    (void)bus;
    sends++;
    last_timeout = timeout;
    CHECK(mcs_can_fd_frame_valid(f, true));
    return 0;
}
static int spy_recv(void *ctx, int bus, mcs_can_fd_frame_t *f, uint32_t timeout) {
    (void)ctx;
    (void)bus;
    last_timeout = timeout;
    memset(f, 0, sizeof *f);
    f->id = 0x1FFFFFFF;
    f->extended = 1;
    f->len = 64;
    f->flags = MCS_CAN_FD_BRS | MCS_CAN_FD_ESI;
    for (unsigned i = 0; i < 64; i++)
        f->data[i] = (uint8_t)i;
    if (received_mode == 1)
        f->len = 65;
    if (received_mode == 2)
        f->len = 9;
    if (received_mode == 3)
        f->flags = 4;
    if (received_mode == 4)
        f->id = UINT32_MAX;
    if (received_mode == 5)
        f->extended = 2;
    if (received_mode == 6)
        f->reserved = 1;
    return 0;
}
static int spy_close(void *ctx, int bus) {
    (void)ctx;
    (void)bus;
    closes++;
    return fail_close ? MCS_HAL_EBUSY : 0;
}
static int legacy_event_calls;
static int legacy_event(mcs_vm_t *vm, const mcs_hal_event_t *event) {
    (void)vm;
    (void)event;
    legacy_event_calls++;
    return 1;
}
static int callback_count;
static mcs_value_t count_callback(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)vm;
    (void)self;
    (void)argc;
    (void)argv;
    callback_count++;
    return mcs_null();
}
static mcs_hal_t spy_hal(void) {
    mcs_hal_t h = {.board = "FD test backend",
                   .can_fd_caps = spy_caps,
                   .can_fd_open = spy_open,
                   .can_fd_send = spy_send,
                   .can_fd_recv = spy_recv,
                   .can_fd_close = spy_close};
    opens = closes = sends = received_mode = fail_open = fail_close = 0;
    caps_mask = MCS_CAN_FD_SUPPORTED | MCS_CAN_FD_SUPPORTS_BRS;
    callback_count = 0;
    return h;
}
#endif /* compiler-enabled backend spies */
#if MCS_HAL_SIM_CAN_FD_QUEUE > 0
static void test_native_loopback(void) {
    mcs_hal_t h;
    mcs_hal_sim_t sim;
    memset(&sim, 0, sizeof sim);
    mcs_hal_sim_init(&h, &sim);
    CHECK(mcs_hal_can_fd_available(&h));
    CHECK(h.can_fd_caps(h.ctx, 0) == (MCS_CAN_FD_SUPPORTED | MCS_CAN_FD_SUPPORTS_BRS));
    CHECK(h.can_fd_caps(h.ctx, 1) == MCS_HAL_ENOTSUP);
    CHECK(h.can_fd_open(h.ctx, 1, 500000, 2000000) == MCS_HAL_ENOTSUP);
    CHECK(h.can_fd_open(h.ctx, 0, 0, 2000000) == MCS_HAL_EINVAL);
    CHECK(h.can_fd_open(h.ctx, 0, 500000, 100000) == MCS_HAL_EINVAL);
    CHECK(h.can_fd_open(h.ctx, 0, 500000, 2000000) == 0);
    CHECK(h.can_fd_open(h.ctx, 0, 500000, 2000000) == MCS_HAL_EBUSY);
    mcs_can_fd_frame_t frame = {0}, result;
    frame.id = 0x1FFFFFFF;
    frame.extended = 1;
    frame.len = 64;
    frame.flags = MCS_CAN_FD_BRS;
    for (unsigned i = 0; i < 64; i++)
        frame.data[i] = (uint8_t)(255 - i);
    CHECK(h.can_fd_send(h.ctx, 0, &frame, 0) == 0);
    memset(frame.data, 0, 64); /* queue owns an actual copy */
    CHECK(h.can_fd_recv(h.ctx, 0, &result, 0) == 0);
    CHECK(result.id == 0x1FFFFFFF && result.extended && result.len == 64 &&
          result.flags == MCS_CAN_FD_BRS);
    for (unsigned i = 0; i < 64; i++)
        CHECK(result.data[i] == (uint8_t)(255 - i));
    CHECK(h.can_fd_recv(h.ctx, 0, &result, 99) == MCS_HAL_ETIMEOUT);
    frame.flags = MCS_CAN_FD_ESI;
    CHECK(h.can_fd_send(h.ctx, 0, &frame, 0) == MCS_HAL_EINVAL);
    frame.flags = 4;
    CHECK(h.can_fd_send(h.ctx, 0, &frame, 0) == MCS_HAL_EINVAL);
    frame.flags = 0;
    frame.len = 9;
    CHECK(h.can_fd_send(h.ctx, 0, &frame, 0) == MCS_HAL_EINVAL);
    frame.len = 64;
    for (unsigned i = 0; i < MCS_HAL_SIM_CAN_FD_QUEUE; i++) {
        frame.id = i;
        CHECK(h.can_fd_send(h.ctx, 0, &frame, 0) == 0);
    }
    CHECK(h.can_fd_send(h.ctx, 0, &frame, 0) == MCS_HAL_EBUSY);
    CHECK(sim.can_fd_count == MCS_HAL_SIM_CAN_FD_QUEUE);
    for (unsigned i = 0; i < MCS_HAL_SIM_CAN_FD_QUEUE; i++) {
        CHECK(h.can_fd_recv(h.ctx, 0, &result, 0) == 0);
        CHECK(result.id == i);
    }
    CHECK(h.can_fd_send(h.ctx, 0, &frame, 0) == 0);
    CHECK(h.can_fd_close(h.ctx, 0) == 0);
    CHECK(!sim.can_fd_opened && !sim.can_fd_count);
    CHECK(h.can_fd_recv(h.ctx, 0, &result, 0) == MCS_HAL_ENODEV);
    CHECK(h.can_fd_send(h.ctx, 0, &frame, 0) == MCS_HAL_ENODEV);
    CHECK(h.can_fd_open(h.ctx, 0, 500000, 500000) == 0);
    CHECK(h.can_fd_recv(h.ctx, 0, &result, 0) == MCS_HAL_ETIMEOUT);
    CHECK(h.can_fd_close(h.ctx, 0) == 0);
    /* Classic queue is independent and unchanged. */
    mcs_can_frame_t classic = {.id = 0x123, .len = 3, .data = {1, 2, 3}}, cr;
    CHECK(h.can_open(h.ctx, 0, 500000) == 0);
    CHECK(h.can_send(h.ctx, 0, &classic, 0) == 0);
    CHECK(h.can_recv(h.ctx, 0, &cr, 0) == 0);
    CHECK(cr.id == classic.id && cr.len == 3 && !memcmp(cr.data, classic.data, 3));
}
#endif
#if MCS_ENABLE_COMPILER
static mcs_result_t source(mcs_vm_t *vm, const char *script) {
    used = 0;
    output[0] = 0;
    return mcs_exec_source(vm, "can-fd-contract.cs", script);
}
static void test_strict_vm_and_backend(void) {
    mcs_hal_t h = spy_hal();
    mcs_vm_t *vm = vm_new();
    mcs_hal_open_lib(vm, &h);
    CHECK(mcs_capability_has(vm, "CAN.FD"));
    legacy_event_calls = 0;
    mcs_hal_set_event_handler(MCS_HAL_EV_BLE, legacy_event);
    CHECK(mcs_hal_post(MCS_HAL_EV_BLE, 0, 0));
    CHECK(mcs_hal_poll(vm) == 1 && legacy_event_calls == 1);
    mcs_hal_set_event_handler(MCS_HAL_EV_BLE, NULL);
    CHECK(
        source(
            vm,
            "Console.WriteLine(CANFD.IsSupported(0)); Console.WriteLine(CANFD.IsSupported(1));") ==
        MCS_OK);
    CHECK(!strcmp(output, "True\nFalse\n"));
    CHECK(source(vm, "CANFD.Open(0); CANFD.Send(0, new CanFdFrame(0x123, new int[] {0,255}, false, "
                     "true), 7);") == MCS_OK);
    CHECK(opens == 1 && sends == 1 && last_timeout == 7);
    mcs_register_function(vm, "CountFdCallback", count_callback, 0);
    CHECK(source(vm, "CANFD.OnReceive(0, (int n) => { CountFdCallback(); });") == MCS_OK);
    CHECK(
        source(
            vm,
            "try { CANFD.Open(0); } catch (InvalidOperationException) { Console.WriteLine(1); }") ==
        MCS_OK);
    CHECK(opens == 1 && !strcmp(output, "1\n"));
    CHECK(mcs_hal_post(MCS_HAL_EV_CAN_FD, 0, 1));
    CHECK(mcs_hal_poll(vm) == 1 && callback_count == 1);
    const char *invalid[] = {"new CanFdFrame(2048, null);",
                             "new CanFdFrame(-1, null);",
                             "new CanFdFrame(536870912, null, true);",
                             "new CanFdFrame(1, new int[] {-1});",
                             "new CanFdFrame(1, new int[] {256});",
                             "new CanFdFrame(1, new int[65]);",
                             "new CanFdFrame(1, \"bytes\");",
                             "new CanFdFrame(1, null, 1);",
                             "new CanFdFrame(1, null, false, 1);",
                             "CANFD.Open(0, 0, 2000000);",
                             "CANFD.Open(0, 500000, 100000);",
                             "CANFD.Send(1, new CanFdFrame(1, null));",
                             "CANFD.Receive(1);",
                             "CANFD.Send(0, new CanFdFrame(1, null), -1);",
                             "CANFD.Receive(0, -1);",
                             "CANFD.Send(0, new CanFrame(1, null));"};
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        char script[512];
        snprintf(script, sizeof script, "try { %s } catch (Exception) { Console.WriteLine(1); }",
                 invalid[i]);
        CHECK(source(vm, script) == MCS_OK);
        CHECK(!strcmp(output, "1\n"));
        CHECK(sends == 1);
    }
#if MCS_INT64
    CHECK(source(vm, "try { new CanFdFrame(4294967297L, null); } catch (Exception) { "
                     "Console.WriteLine(1); }") == MCS_OK);
    CHECK(!strcmp(output, "1\n"));
    CHECK(source(vm, "try { CANFD.Send(0, new CanFdFrame(1, null), 4294967296L); } catch "
                     "(Exception) { Console.WriteLine(1); }") == MCS_OK);
    CHECK(!strcmp(output, "1\n") && sends == 1);
#endif
    CHECK(source(vm, "var f = CANFD.Receive(0, 5); Console.WriteLine(f.Length); "
                     "Console.WriteLine(f.Data[63]); Console.WriteLine(f.BitRateSwitch); "
                     "Console.WriteLine(f.ErrorStateIndicator); Console.WriteLine(f.Remote);") ==
          MCS_OK);
    CHECK(!strcmp(output, "64\n63\nTrue\nTrue\nFalse\n") && last_timeout == 5);
    for (received_mode = 1; received_mode <= 6; received_mode++) {
        CHECK(source(vm, "try { CANFD.Receive(0); } catch (InvalidOperationException) { "
                         "Console.WriteLine(1); }") == MCS_OK);
        CHECK(!strcmp(output, "1\n"));
    }
    received_mode = 0;
    CHECK(source(vm, "try { CANFD.Send(0, CANFD.Receive(0)); } catch (ArgumentException) { "
                     "Console.WriteLine(1); }") == MCS_OK);
    CHECK(!strcmp(output, "1\n") && sends == 1);
    fail_close = 1;
    CHECK(source(vm, "try { CANFD.Close(0); } catch (IOException) { Console.WriteLine(1); } "
                     "CANFD.Send(0, new CanFdFrame(1, null));") == MCS_OK);
    CHECK(sends == 2 && closes == 1);
    CHECK(mcs_hal_post(MCS_HAL_EV_CAN_FD, 0, 1));
    CHECK(mcs_hal_poll(vm) == 1 && callback_count == 2);
    fail_close = 0;
    CHECK(source(vm, "CANFD.Close(0); CANFD.Close(0);") == MCS_OK);
    CHECK(closes == 2);
    CHECK(mcs_hal_post(MCS_HAL_EV_CAN_FD, 0, 1));
    CHECK(mcs_hal_poll(vm) == 0 && callback_count == 2);
    mcs_hal_close_lib(vm);
    vm_done(vm);
    CHECK(closes == 2);
    /* Exactly once release across reattach/detach/free and collection of old
     * state; old userdata may not close a newly acquired resource. */
    h = spy_hal();
    vm = vm_new();
    mcs_hal_open_lib(vm, &h);
    CHECK(source(vm, "CANFD.Open(0);") == MCS_OK);
    mcs_hal_open_lib(vm, &h);
    CHECK(closes == 1);
    CHECK(source(vm, "CANFD.Open(0);") == MCS_OK);
    mcs_hal_close_lib(vm);
    CHECK(closes == 2);
    mcs_hal_open_lib(vm, &h);
    CHECK(source(vm, "CANFD.Open(0);") == MCS_OK);
    mcs_gc(vm);
    CHECK(closes == 2);
    vm_done(vm);
    CHECK(closes == 3 && opens == 3);
    /* Native values remain usable after detach, but device operations fail
     * cleanly without borrowing or dereferencing a removed HAL state. */
    h = spy_hal();
    vm = vm_new();
    mcs_hal_open_lib(vm, &h);
    CHECK(source(vm, "CANFD.Open(0);") == MCS_OK);
    mcs_hal_close_lib(vm);
    CHECK(source(vm, "Console.WriteLine(CANFD.IsSupported(0)); try { CANFD.Open(0); } "
                     "catch(NotSupportedException) { Console.WriteLine(1); } var v = new "
                     "CanFdFrame(1, null); Console.WriteLine(v.Length);") == MCS_OK);
    CHECK(!strcmp(output, "False\n1\n0\n") && opens == 1 && closes == 1);
    vm_done(vm);
    /* Ordinary script shadowing cannot make Receive treat a foreign instance
     * as native frame storage. It fails through the checked userdata contract. */
    h = spy_hal();
    vm = vm_new();
    mcs_hal_open_lib(vm, &h);
    CHECK(source(vm,
                 "class CanFdFrame { public CanFdFrame(int id, object data) {} } CANFD.Open(0); "
                 "try { CANFD.Receive(0); } catch(Exception) { Console.WriteLine(1); }") == MCS_OK);
    CHECK(!strcmp(output, "1\n"));
    vm_done(vm);
    CHECK(closes == 1);
    /* A complete FD backend may legitimately lack bitrate switching. */
    h = spy_hal();
    caps_mask = MCS_CAN_FD_SUPPORTED;
    vm = vm_new();
    mcs_hal_open_lib(vm, &h);
    CHECK(source(vm, "Console.WriteLine(CANFD.SupportsBitRateSwitch(0)); try { CANFD.Open(0); } "
                     "catch(NotSupportedException) { Console.WriteLine(1); } CANFD.Open(0, 500000, "
                     "500000); try { CANFD.Send(0,new CanFdFrame(1,null,false,true)); } "
                     "catch(NotSupportedException) { Console.WriteLine(2); }") == MCS_OK);
    CHECK(!strcmp(output, "False\n1\n2\n") && opens == 1 && sends == 0);
    CHECK(source(vm, "try { CANFD.Receive(0); } catch(InvalidOperationException) { "
                     "Console.WriteLine(1); }") == MCS_OK);
    CHECK(!strcmp(output, "1\n"));
    vm_done(vm);
    CHECK(closes == 1);
    /* A failure to acquire never owns/closes an embedding/other-VM resource. */
    h = spy_hal();
    fail_open = 1;
    vm = vm_new();
    mcs_hal_open_lib(vm, &h);
    CHECK(source(vm, "try { CANFD.Open(0); } catch (IOException) { Console.WriteLine(1); } "
                     "CANFD.Close(0);") == MCS_OK);
    mcs_hal_close_lib(vm);
    vm_done(vm);
    CHECK(opens == 1 && closes == 0);
    /* Every missing callback disables both capability and operations. */
    for (int missing = 0; missing < 5; missing++) {
        h = spy_hal();
        switch (missing) {
        case 0:
            h.can_fd_caps = NULL;
            break;
        case 1:
            h.can_fd_open = NULL;
            break;
        case 2:
            h.can_fd_send = NULL;
            break;
        case 3:
            h.can_fd_recv = NULL;
            break;
        default:
            h.can_fd_close = NULL;
            break;
        }
        vm = vm_new();
        mcs_hal_open_lib(vm, &h);
        CHECK(!mcs_capability_has(vm, "CAN.FD"));
        CHECK(source(vm, "Console.WriteLine(CANFD.IsSupported(0)); try { CANFD.Open(0); } catch "
                         "(NotSupportedException) { Console.WriteLine(1); }") == MCS_OK);
        CHECK(!strcmp(output, "False\n1\n") && opens == 0);
        vm_done(vm);
    }
}
#if MCS_HAL_SIM_CAN_FD_QUEUE > 0
static mcs_value_t stop_vm(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {
    (void)self;
    (void)argc;
    (void)argv;
    mcs_request_abort(vm);
    return mcs_null();
}
static void test_lifetime_and_events(void) {
    mcs_hal_t h;
    mcs_hal_sim_t sim;
    memset(&sim, 0, sizeof sim);
    mcs_hal_sim_init(&h, &sim);
    mcs_vm_t *vm = vm_new();
    mcs_hal_open_lib(vm, &h);
    CHECK(mcs_hal_poll(vm) >= 0); /* drain notifications from earlier independent fixtures */
    /* Backend owned by an embedding: a VM cannot steal, use or close it. */
    CHECK(h.can_fd_open(h.ctx, 0, 500000, 2000000) == 0);
    CHECK(source(vm, "try { CANFD.Open(0); } catch (IOException) { Console.WriteLine(1); } "
                     "CANFD.Close(0);") == MCS_OK);
    CHECK(sim.can_fd_opened);
    vm_done(vm);
    CHECK(sim.can_fd_opened);
    CHECK(h.can_fd_close(h.ctx, 0) == 0);
    vm = vm_new();
    mcs_hal_open_lib(vm, &h);
    CHECK(source(vm,
                 "CANFD.Open(0); int fdHits = 0; int classicHits = 0; CANFD.OnReceive(0, (int n) "
                 "=> { fdHits++; }); CAN.OnReceive(0, (int n) => { classicHits++; }); "
                 "CANFD.Send(0, new CanFdFrame(1, null)); CAN.Send(0, 1, null); CANFD.Close(0); "
                 "CANFD.Open(0); CANFD.OnReceive(0, (int n) => { fdHits += 100; }); Hal.Poll(); "
                 "Console.WriteLine(fdHits); Console.WriteLine(classicHits);") == MCS_OK);
    CHECK(!strcmp(output, "0\n1\n"));
    CHECK(source(vm, "CANFD.OnReceive(0, (int n) => { CANFD.Close(0); }); CANFD.Send(0, new "
                     "CanFdFrame(1, null)); Hal.Poll();") == MCS_OK);
    CHECK(!sim.can_fd_opened);
    CHECK(source(vm, "CANFD.Open(0); CANFD.OnReceive(0, null);") == MCS_OK);
    /* Remove only owned-bus FD entries from the deferred queue, preserving
     * user/classic/other-bus order. Then reopen for held-generation regression. */
    mcs__hal_state_t *preserve = (mcs__hal_state_t *)mcs_get_ext(vm, MCS_EXT_HAL);
    preserve->defer[0] = (mcs_hal_event_t){.type = MCS_HAL_EV_CAN_FD, .source = 0, .value = 1};
    preserve->defer[1] = (mcs_hal_event_t){.type = MCS_HAL_EV_USER, .source = 2, .value = 3};
    preserve->defer[2] = (mcs_hal_event_t){.type = MCS_HAL_EV_CAN_FD, .source = 1, .value = 4};
    preserve->defer[3] = (mcs_hal_event_t){.type = MCS_HAL_EV_CAN, .source = 0, .value = 5};
    preserve->ndefer = 4;
    CHECK(source(vm, "CANFD.Close(0); CANFD.Open(0);") == MCS_OK);
    CHECK(preserve->ndefer == 3);
    CHECK(preserve->defer[0].type == MCS_HAL_EV_USER && preserve->defer[1].source == 1 &&
          preserve->defer[2].type == MCS_HAL_EV_CAN);
    preserve->ndefer = 0;
    /* Deferred snapshots, including held entries inside a poll, must not replay
     * an old generation after a callback closes/reopens the bus. */
    mcs__hal_state_t *state = (mcs__hal_state_t *)mcs_get_ext(vm, MCS_EXT_HAL);
    state->defer[0] = (mcs_hal_event_t){.type = MCS_HAL_EV_CAN_FD, .source = 0, .value = 1};
    state->defer[1] = (mcs_hal_event_t){.type = MCS_HAL_EV_CAN_FD, .source = 0, .value = 2};
    state->ndefer = 2;
    CHECK(source(vm, "int heldHits = 0; CANFD.OnReceive(0, (int n) => { heldHits++; "
                     "CANFD.Close(0); CANFD.Open(0); CANFD.OnReceive(0, (int k) => { heldHits += "
                     "100; }); }); Hal.Poll(); Console.WriteLine(heldHits);") == MCS_OK);
    CHECK(!strcmp(output, "1\n"));
    mcs_register_function(vm, "StopFdTest", stop_vm, 0);
    CHECK(source(vm, "StopFdTest();") == MCS_ERR_ABORTED);
    CHECK(sim.can_fd_opened); /* persistent session; Stop is not foreground close */
    CHECK(source(vm, "throw new Exception(\"ordinary failure\");") == MCS_ERR_RUNTIME);
    CHECK(sim.can_fd_opened);
    CHECK(
        source(vm,
               "CANFD.Send(0, new CanFdFrame(1, null)); Console.WriteLine(CANFD.Receive(0).Id);") ==
        MCS_OK);
    CHECK(!strcmp(output, "1\n"));
    mcs_hal_close_lib(vm);
    CHECK(!sim.can_fd_opened);
    /* Reattach/acquire; collection of the old state cannot close this new one. */
    mcs_hal_open_lib(vm, &h);
    CHECK(source(vm, "CANFD.Open(0); GC.Collect(); CANFD.Send(0, new CanFdFrame(2, null));") ==
          MCS_OK);
    CHECK(sim.can_fd_opened);
    vm_done(vm);
    CHECK(!sim.can_fd_opened);
    /* Finalizer also handles raw mcs_free without an explicit detach. */
    vm = vm_new();
    mcs_hal_open_lib(vm, &h);
    CHECK(source(vm, "CANFD.Open(0);") == MCS_OK);
    mcs_vm_t *other = mcs_new(NULL);
    CHECK(other != NULL);
    mcs_hal_open_lib(other, &h);
    CHECK(mcs_exec_source(other, "other.cs",
                          "try { CANFD.Open(0); } catch (IOException) {} CANFD.Close(0);") ==
          MCS_OK);
    mcs_free(other);
    CHECK(sim.can_fd_opened);
    vm_done(vm);
    CHECK(!sim.can_fd_opened);
}
#endif
#endif
#endif

static const char fixture_source[] =
    "CANFD.Open(0); try { "
    "for (int n=0; n<=64; n++) { var d=new int[n]; for(int i=0;i<n;i++) d[i]=i; "
    "var f=new CanFdFrame(0x1fffffff,d,true,true); CANFD.Send(0,f); var r=CANFD.Receive(0); "
    "if(r==null || r.Length!=f.Length || r.Dlc!=f.Dlc || !r.Extended || !r.BitRateSwitch || "
    "r.ErrorStateIndicator || r.Remote) throw new Exception(\"frame\"); "
    "var bytes=r.Data; for(int i=0;i<n;i++) if(bytes[i]!=i || d[i]!=i) throw new "
    "Exception(\"data\"); "
    "for(int i=n;i<r.Length;i++) if(bytes[i]!=0) throw new Exception(\"padding\"); "
    "} Console.WriteLine(65); } finally { CANFD.Close(0); }";
#if MCS_ENABLE_COMPILER && MCS_ENABLE_BYTECODE_SAVE
static int emit(const char *directory) {
    for (unsigned mode = 0; mode < 2; mode++) {
        mcs_vm_t *vm = vm_new();
        uint8_t *image = NULL;
        size_t n = 0;
        CHECK(mcs_compile_image_ex(vm, "trusted-can-fd.cs", fixture_source,
                                   mode ? MCS_IMAGE_NO_OPT : 0, &image, &n) == MCS_OK);
        char name[1024];
        snprintf(name, sizeof name, "%s/%s.mcsb", directory, mode ? "noopt" : "opt");
        FILE *file = fopen(name, "wb");
        CHECK(file != NULL);
        if (file) {
            CHECK(fwrite(image, 1, n, file) == n);
            CHECK(!fclose(file));
        }
        mcs_free_image(vm, image);
        vm_done(vm);
    }
    return failures ? 1 : 0;
}
#endif
static void test_images(const char *directory) {
#if MCS_ENABLE_HAL && MCS_HAL_SIM_CAN_FD_QUEUE > 0 && MCS_ENABLE_BYTECODE_LOAD
    for (unsigned mode = 0; mode < 2; mode++) {
#if !MCS_ENABLE_SUPEROPS
        if (mode == 0) {
            /* mcs.h explicitly requires MCS_IMAGE_NO_OPT for this build. */
            printf("IMAGES: superinstructions disabled; execute compiler NO_OPT fixture only\n");
            continue;
        }
#endif
        char name[1024];
        snprintf(name, sizeof name, "%s/%s.mcsb", directory, mode ? "noopt" : "opt");
        FILE *file = fopen(name, "rb");
        CHECK(file != NULL);
        if (!file)
            continue;
        fseek(file, 0, SEEK_END);
        long n = ftell(file);
        rewind(file);
        CHECK(n > 0);
        uint8_t *image = (uint8_t *)malloc((size_t)n);
        CHECK(image != NULL);
        if (!image) {
            fclose(file);
            continue;
        }
        CHECK(fread(image, 1, (size_t)n, file) == (size_t)n);
        fclose(file);
        for (int xip = 0; xip < 2; xip++) {
            mcs_hal_t h;
            mcs_hal_sim_t sim;
            memset(&sim, 0, sizeof sim);
            mcs_hal_sim_init(&h, &sim);
            mcs_vm_t *vm = vm_new();
            mcs_hal_open_lib(vm, &h);
            CHECK((xip ? mcs_exec_image_xip(vm, image, (size_t)n)
                       : mcs_exec_image(vm, image, (size_t)n)) == MCS_OK);
            CHECK(!strcmp(output, "65\n"));
            CHECK(!sim.can_fd_opened && !sim.can_fd_count);
            vm_done(vm);
        }
        free(image); /* XIP backing lives until every executing VM is freed. */
    }
#else
    (void)directory;
#endif
}
int main(int argc, char **argv) {
    (void)fixture_source;
#if MCS_ENABLE_COMPILER && MCS_ENABLE_BYTECODE_SAVE
    if (argc == 3 && !strcmp(argv[1], "--emit"))
        return emit(argv[2]);
#endif
    printf("FLAGS compiler=%d hal=%d xip=%d gc-stress=%d compact=%d int64=%d superops=%d\n",
           MCS_ENABLE_COMPILER, MCS_ENABLE_HAL, MCS_ENABLE_XIP, MCS_GC_STRESS, MCS_COMPACT_VALUES,
           MCS_INT64, MCS_ENABLE_SUPEROPS);
    test_layout_and_dlc();
#if MCS_ENABLE_HAL
#if MCS_HAL_SIM_CAN_FD_QUEUE > 0
    test_native_loopback();
#endif
#if MCS_ENABLE_COMPILER
    test_strict_vm_and_backend();
#if MCS_HAL_SIM_CAN_FD_QUEUE > 0
    test_lifetime_and_events();
    /* Repeat trusted source to catch resource/queue/ownership accumulation. */
    for (int repeat = 0; repeat < 4; repeat++) {
        mcs_hal_t h;
        mcs_hal_sim_t sim;
        memset(&sim, 0, sizeof sim);
        mcs_hal_sim_init(&h, &sim);
        mcs_vm_t *vm = vm_new();
        mcs_hal_open_lib(vm, &h);
        CHECK(source(vm, fixture_source) == MCS_OK);
        CHECK(!strcmp(output, "65\n") && !sim.can_fd_opened);
        vm_done(vm);
    }
#else
    mcs_hal_t h;
    mcs_hal_sim_t sim;
    memset(&sim, 0, sizeof sim);
    mcs_hal_sim_init(&h, &sim);
    mcs_vm_t *vm = vm_new();
    mcs_hal_open_lib(vm, &h);
    CHECK(!mcs_capability_has(vm, "CAN.FD"));
    CHECK(source(vm, "Console.WriteLine(CANFD.IsSupported(0)); try { CANFD.Open(0); } "
                     "catch(NotSupportedException) { Console.WriteLine(1); }") == MCS_OK);
    CHECK(!strcmp(output, "False\n1\n"));
    vm_done(vm);
#endif
#endif
#else
    mcs_vm_t *vm = vm_new();
    CHECK(!mcs_capability_has(vm, "CAN.FD"));
    vm_done(vm);
#endif
    if (argc == 2)
        test_images(argv[1]);
    printf("%u CAN FD checks, %u failures; software/native only\n", checks, failures);
    return failures ? 1 : 0;
}
