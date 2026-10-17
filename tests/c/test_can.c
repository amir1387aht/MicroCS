/* Classic CAN API plus MCP2515 register-level transport model. No hardware needed. */
#include "mcs_hal_internal.h"
#include "mcs_can_timing.h"
#include <stdio.h>
#include <string.h>
static int failures, checks;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static mcs_hal_t hal;
static mcs_hal_sim_t sim;
static uint8_t regfile[256];
static int selected, gpio_high = 1, short_spi, fail_spi, no_device, transfers, opens, mode_changes;
static int spi_open(void* ctx, int bus, const mcs_spi_cfg_t* c) {
    (void)ctx; opens++;
    CHECK(bus == 0 && c->freq_hz == 1000000 && c->mode == 0 && c->bits == 8 && !c->lsb_first); return 0;
}
static int gpio_mode(void* ctx, int pin, int mode) { (void)ctx; CHECK(pin == 5 && mode == MCS_GPIO_OUTPUT); return 0; }
static int gpio_write(void* ctx, int pin, int high) { (void)ctx; CHECK(pin == 5); gpio_high = high; selected = !high; return 0; }
static void delay_us(void* ctx, uint32_t us) { (void)ctx; (void)us; }
static int spi_transfer(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n) {
    (void)ctx; transfers++; CHECK(selected && bus == 0 && tx && rx && n);
    if (fail_spi) return MCS_HAL_ERR;
    if (short_spi) return (int)n - 1;
    memset(rx, 0, n);
    if (no_device) { memset(rx, 255, n); return (int)n; }
    if (tx[0] == 0xc0) { memset(regfile, 0, sizeof regfile); regfile[0x0e] = regfile[0x0f] = 0x80; }
    else if (tx[0] == 0x03) { for (size_t i = 2; i < n; i++) rx[i] = regfile[(uint8_t)(tx[1] + i - 2)]; }
    else if (tx[0] == 0x02) { for (size_t i = 2; i < n; i++) regfile[(uint8_t)(tx[1] + i - 2)] = tx[i]; }
    else if (tx[0] == 0x05) {
        regfile[tx[1]] = (uint8_t)((regfile[tx[1]] & ~tx[2]) | (tx[3] & tx[2]));
        if (tx[1] == 0x0f) { regfile[0x0e] = regfile[0x0f] & 0xe0; mode_changes++; }
    } else if ((tx[0] & 0xf8) == 0x80) {
        for (int i = 0; i < 3; i++) if (tx[0] & (1 << i)) {
            int base = 0x30 + i * 16;
            if ((regfile[0x0f] & 0xe0) == 0x40) {
                int buf = regfile[0x2c] & 1 ? 1 : 0, rb = 0x60 + buf * 16;
                memcpy(regfile + rb + 1, regfile + base + 1, 13);
                regfile[rb] = (uint8_t)(0x60 | (regfile[base + 5] & 0x40 ? 8 : 0));
                regfile[0x2c] |= (uint8_t)(1 << buf);
            } else regfile[base] |= 8;
        }
    } else CHECK(0);
    return (int)n;
}
static void run(mcs_vm_t* vm, const char* code) {
    mcs_result_t rc = mcs_exec_source(vm, "can.cs", code); CHECK(rc == MCS_OK);
    if (rc != MCS_OK) printf("  %s\n", mcs_last_error(vm));
}
static int abort_wait(mcs_vm_t* vm, void* ud) {
    if (++*(int*)ud == 3) mcs_request_abort(vm);
    return 0;
}
static void cancellation_test(mcs_vm_t* vm, const char* code) {
    void* saved_ud; mcs_idle_fn saved = mcs_get_idle(vm, &saved_ud); int polls = 0;
    mcs_set_idle(vm, abort_wait, &polls);
    CHECK(mcs_exec_source(vm, "cancel_can.cs", code) == MCS_ERR_ABORTED);
    CHECK(polls == 3);
    mcs_set_idle(vm, saved, saved_ud);
}
static void timing_tests(void) {
    mcs_can_timing_t t = { 0 };
    const uint32_t clocks[] = { 8000000, 16000000, 20000000, 42000000, 48000000, 80000000 };
    const uint32_t rates[] = { 125000, 250000, 500000, 1000000 };
    for (unsigned c = 0; c < sizeof clocks / sizeof *clocks; c++) for (unsigned r = 0; r < sizeof rates / sizeof *rates; r++) {
        CHECK(mcs_can_timing(clocks[c], rates[r], 1024, 16, 8, 1, &t));
        CHECK(clocks[c] == rates[r] * t.prescaler * (1u + t.seg1 + t.seg2));
        CHECK(t.seg1 >= 2 && t.seg1 <= 16 && t.seg2 >= 1 && t.seg2 <= 8);
    }
    CHECK(!mcs_can_timing(42000000, 123457, 1024, 16, 8, 1, &t));
    CHECK(!mcs_can_timing(0, 500000, 1024, 16, 8, 1, &t));
    CHECK(!mcs_can_timing(42000000, 0, 1024, 16, 8, 1, &t));
}
int main(void) {
    timing_tests();
    mcs_config_t cfg; mcs_config_default(&cfg);
    mcs_vm_t* vm = mcs_new(&cfg); CHECK(vm != NULL);
    mcs_hal_sim_init(&hal, &sim); mcs_hal_open_lib(vm, &hal);
    CHECK(mcs_capability_has(vm, "CAN")); CHECK(mcs_capability_has(vm, "CAN.Configure"));
    run(vm, "try { CAN.Receive(0); throw new Exception(); } catch (IOException e) {} "
            "CAN.Open(0, new CanConfig { Bitrate = 250000, Mode = CAN.Loopback }); "
            "CAN.Send(0, 0x7ff, new byte[] {1,2,3}); var f = CAN.Receive(0); "
            "if (f.Id != 0x7ff || f.Data.Length != 3 || f.Remote) throw new Exception(); "
            "CAN.Send(0, new CanFrame(0x1fffffff, new byte[8], true, true)); f = CAN.Receive(0); "
            "if (!f.Remote || !f.Extended || f.Length != 8 || f.Data.Length != 0) throw new Exception();");
    cancellation_test(vm, "CAN.Receive(0, 60000);");
#if MCS_INT64
    run(vm, "try { CAN.Open(4294967296); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
            "try { var f = new CanFrame(4294967297, null); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
            "try { CAN.OpenMcp2515(0, 0, 4294967301); throw new Exception(); } catch (ArgumentOutOfRangeException e) {}");
#endif
    run(vm, "try { Power.LightSleep(1); throw new Exception(); } catch (IOException e) {} ");
    run(vm, "int received = 0; CAN.OnReceive(0, (int n) => { CanFrame f; while ((f = CAN.Receive(0)) != null) received++; }); "
            "CAN.Send(0, 123, new byte[0]); Hal.Poll(); if (received != 1) throw new Exception(); "
            "CAN.OnReceive(0, null); CAN.Close(0); CAN.Close(0);");
    run(vm, "CAN.Open(0, new CanConfig { Mode = CAN.ListenOnly }); "
            "try { CAN.Send(0, 1, null); throw new Exception(); } catch (NotSupportedException e) {} CAN.Close(0);");
    run(vm, "try { CAN.Open(); throw new Exception(); } catch (ArgumentException e) {} "
            "try { CAN.Receive(); throw new Exception(); } catch (ArgumentException e) {} "
            "try { CAN.Send(0); throw new Exception(); } catch (ArgumentException e) {} "
            "try { CAN.Open(-1); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
            "try { CAN.Open(0, 0); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
            "try { CAN.Receive(0, -1); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
            "try { var f = new CanFrame(0x800, null); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
            "try { var f = new CanFrame(0x20000000, null, true); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
            "try { var f = new CanFrame(1, new byte[9]); throw new Exception(); } catch (ArgumentOutOfRangeException e) {}");
    mcs__hal_state_t* state = mcs_get_ext(vm, MCS_EXT_HAL); state->no_events = 1;
    run(vm, "try { CAN.Open(0); throw new Exception(); } catch (IOException e) {}"); state->no_events = 0;
    mcs_hal_close_lib(vm);
    /* MCU without an on-chip controller: keep native CAN false but expose MCP. */
    memset(&hal, 0, sizeof hal);
    hal.spi_open = spi_open; hal.spi_transfer = spi_transfer; hal.gpio_mode = gpio_mode; hal.gpio_write = gpio_write; hal.delay_us = delay_us;
    mcs_hal_open_lib(vm, &hal);
    CHECK(!mcs_capability_has(vm, "CAN")); CHECK(mcs_capability_has(vm, "CAN.Mcp2515"));
    int before = transfers;
    for (int i = 0; i < 10; i++) (void)mcs_capability_has(vm, "CAN.Mcp2515");
    CHECK(before == transfers);
    run(vm, "try { CAN.Open(0); throw new Exception(); } catch (NotSupportedException e) {} "
            "try { CAN.OpenMcp2515(0, 0, 5, 12000000); throw new Exception(); } catch (ArgumentOutOfRangeException e) {} "
            "CAN.OpenMcp2515(0, 0, 5, 8000000, 500000, CAN.Loopback);");
    CHECK(regfile[0x2a] == 0 && regfile[0x29] == 0x91 && regfile[0x28] == 1); /* 8 TQ @ 500k */
    run(vm, "CAN.Send(0, 0x123, new byte[] {0x12,0xab,0xff}, false, 0); var a = CAN.Receive(0); "
            "if (a.Id != 0x123 || a.Length != 3 || a.Data[1] != 0xab || a.Extended) throw new Exception(); "
            "CAN.Send(0, new CanFrame(0x1fffffff, new byte[8], true, true), 0); var b = CAN.Receive(0); "
            "if (b.Id != 0x1fffffff || !b.Remote || !b.Extended || b.Length != 8 || b.Data.Length != 0) throw new Exception(); "
            "if (CAN.Receive(0, 2) != null) throw new Exception();");
    run(vm, "int seen = 0; CAN.OnReceive(0, (int n) => { while (CAN.Receive(0) != null) seen++; }); "
            "CAN.Send(0, 4, new byte[8]); Hal.Poll(); if (seen != 1) throw new Exception(); CAN.OnReceive(0, null);");
    CHECK(gpio_high == 1);
    run(vm, "try { CAN.OpenMcp2515(1, 0, 5); throw new Exception(); } catch (IOException e) {} "
            "CAN.Close(0); CAN.OpenMcp2515(0, 0, 5, 16000000, 500000, CAN.ListenOnly); "
            "try { CAN.Send(0, 1, null); throw new Exception(); } catch (NotSupportedException e) {} "
            "CAN.Close(0); CAN.OpenMcp2515(0, 0, 5); "
            "CAN.Send(0, 1, null); CAN.Send(0, 2, null); CAN.Send(0, 3, null); "
            "try { CAN.Send(0, 4, null, false, 0); throw new Exception(); } catch (TimeoutException e) {} CAN.Close(0);");
    run(vm, "CAN.OpenMcp2515(0, 0, 5); CAN.Send(0, 1, null); CAN.Send(0, 2, null); CAN.Send(0, 3, null);");
    cancellation_test(vm, "CAN.Send(0, 4, null, false, 60000);");
    run(vm, "CAN.Close(0);");
    short_spi = 1;
    run(vm, "try { CAN.OpenMcp2515(0, 0, 5); throw new Exception(); } catch (IOException e) {}"); CHECK(gpio_high == 1); short_spi = 0;
    fail_spi = 1;
    run(vm, "try { CAN.OpenMcp2515(0, 0, 5); throw new Exception(); } catch (IOException e) {}"); CHECK(gpio_high == 1); fail_spi = 0;
    no_device = 1;
    run(vm, "try { CAN.OpenMcp2515(0, 0, 5); throw new Exception(); } catch (IOException e) {}"); no_device = 0;
    run(vm, "CAN.OpenMcp2515(0, 0, 5, 20000000, 250000, CAN.Loopback);");
    mcs_hal_close_lib(vm); CHECK(regfile[0x0e] == 0x20 && gpio_high == 1);
    CHECK(opens > 0 && mode_changes > 0);
    mcs_free(vm);
    printf("%d/%d CAN checks passed\n", checks - failures, checks); return failures != 0;
}
