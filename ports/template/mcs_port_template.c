/*
 * MicroCS port template - copy this file, replace every TODO with your SDK's
 * calls, delete what your chip does not have (NULL entries simply hide the
 * C# class), and pick one of the two entry points at the bottom:
 *
 *   app_script_start()   Option 1: run C# inside your existing firmware
 *   app_firmware_main()  Option 2: the whole firmware is a C# REPL (mcs_runtime)
 *
 * Time / OS mapping:
 *   Bare metal  ticks -> SysTick ms counter, delay -> busy wait / WFI
 *   FreeRTOS    ticks -> xTaskGetTickCount() * portTICK_PERIOD_MS, delay -> vTaskDelay
 *   Zephyr      ticks -> k_uptime_get_32(), delay -> k_msleep (or use ports/zephyr)
 */
#include <string.h>
#include "mcs.h"
#include "mcs_bind.h"
#include "mcs_hal.h"
#include "mcs_runtime.h"

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

/* ============================================================ time + console */
static uint32_t port_ticks(void* ud) { return 0; /* TODO: millisecond counter */ }
static void port_delay(void* ud, uint32_t ms) { /* TODO: sleep ms (yield to the RTOS) */ }

static void console_write(void* ud, const char* s, size_t n) {
    /* TODO: uart_write(CONSOLE_UART, s, n); */
}
/* read up to n bytes, wait at most timeout_ms; return count, 0 on timeout, <0 if closed */
static int console_read(void* ud, uint8_t* buf, size_t n, uint32_t timeout_ms) {
    /* TODO: return uart_read(CONSOLE_UART, buf, n, timeout_ms); */
    port_delay(ud, timeout_ms);
    return 0;
}

/* ============================================================ peripherals
 * Return >= 0 on success or MCS_HAL_ENOTSUP / ETIMEOUT / ENODEV (NACK) / EINVAL / ERR. */
static int gpio_mode(void* ctx, int pin, int mode) {
    /* mode: MCS_GPIO_INPUT, OUTPUT, INPUT_PULLUP, INPUT_PULLDOWN, OPEN_DRAIN, ANALOG */
    return 0;   /* TODO */
}
static int gpio_write(void* ctx, int pin, int v) { return 0; /* TODO */ }
static int gpio_read(void* ctx, int pin) { return 0; /* TODO: 0 or 1 */ }
static int gpio_irq(void* ctx, int pin, int edge) {
    /* edge: MCS_GPIO_EDGE_NONE (disable), RISING, FALLING, BOTH. In the ISR call
     *     mcs_hal_post(MCS_HAL_EV_GPIO, pin, level);
     * the C# callback then runs on the VM thread. */
    return MCS_HAL_ENOTSUP;   /* TODO */
}

static int uart_config(void* ctx, int port, const mcs_uart_cfg_t* c) { return MCS_HAL_ENOTSUP; /* TODO: baud, bits, parity, stop */ }
static int uart_write(void* ctx, int port, const uint8_t* d, size_t n) { return (int)n; /* TODO */ }
static int uart_read(void* ctx, int port, uint8_t* b, size_t n, uint32_t timeout_ms) { return 0; /* TODO: bytes read */ }
static int uart_available(void* ctx, int port) { return 0; /* TODO: bytes buffered */ }

static int i2c_open(void* ctx, int bus, uint32_t hz) { return 0; /* TODO */ }
static int i2c_write(void* ctx, int bus, int addr, const uint8_t* d, size_t n) { return MCS_HAL_ENODEV; /* TODO */ }
static int i2c_read(void* ctx, int bus, int addr, uint8_t* b, size_t n) { return MCS_HAL_ENODEV; /* TODO */ }

static int spi_open(void* ctx, int bus, const mcs_spi_cfg_t* c) { return 0; /* TODO: c->freq_hz, c->mode */ }
static int spi_transfer(void* ctx, int bus, const uint8_t* tx, uint8_t* rx, size_t n) {
    memcpy(rx, tx, n);   /* TODO: full duplex; tx is never NULL */
    return 0;
}

static int adc_read(void* ctx, int ch) { return 0; /* TODO: raw counts */ }
static int pwm_set16(void* ctx, int ch, uint32_t hz, uint16_t duty) { return MCS_HAL_ENOTSUP; /* TODO: duty 0..65535 */ }

static int timer_start(void* ctx, int id, uint32_t period_us, int periodic) {
    /* in the timer ISR: mcs_hal_post(MCS_HAL_EV_TIMER, id, count); */
    return MCS_HAL_ENOTSUP;   /* TODO */
}
static int timer_stop(void* ctx, int id) { return 0; }

static uint32_t micros(void* ctx) { return port_ticks(ctx) * 1000u; /* TODO: µs counter */ }

static const mcs_hal_t board = {
    .board = "my-board",
    .gpio_mode = gpio_mode, .gpio_write = gpio_write, .gpio_read = gpio_read, .gpio_irq = gpio_irq,
    .uart_config = uart_config, .uart_write = uart_write, .uart_read = uart_read, .uart_available = uart_available,
    .i2c_open = i2c_open, .i2c_write = i2c_write, .i2c_read = i2c_read,
    .spi_open = spi_open, .spi_transfer = spi_transfer,
    .adc_read = adc_read, .adc_bits = 12,
    .pwm_set16 = pwm_set16,
    .timer_start = timer_start, .timer_stop = timer_stop,
    .micros = micros,
    /* Optional members (see include/mcs_hal.h): pin_lookup, uart_close, i2c_write_read,
     * i2c_probe, adc_read_mv, adc_vref_mv, dac_write, dac_bits, pwm_stop, i2s_open/write/
     * read/close, qspi_open/command, can_open/send/recv, wdt_start/feed, rtc_get/set,
     * delay_us, reset, unique_id, cpu_hz, poll_event. */
};

/* ============================================================ your own C# API */
/* extern void board_led_set(long on);  MCS_WRAP_V_I(w_led_set, board_led_set) */
static const mcs_reg_t board_fns[] = {
    /* MCS_FN("Led", w_led_set, 1), */
    MCS_REG_END
};
static void setup_bindings(mcs_vm_t* vm, void* ud) { mcs_register_module(vm, "Board", board_fns); }

/* ============================================================ Option 1: library */
static uint8_t script_heap[64 * 1024] __attribute__((aligned(8)));
static mcs_pool_t script_pool;

int app_script_start(const char* source) {
    mcs_pool_init(&script_pool, script_heap, sizeof script_heap);
    mcs_config_t cfg;
    mcs_config_default(&cfg);
    cfg.realloc_fn = mcs_pool_realloc;
    cfg.alloc_ud = &script_pool;
    cfg.write_fn = console_write;
    cfg.ticks_fn = port_ticks;
    cfg.delay_fn = port_delay;

    mcs_vm_t* vm = mcs_new(&cfg);
    if (!vm) return -1;
    mcs_hal_open_lib(vm, &board);
    setup_bindings(vm, NULL);
    mcs_result_t r = mcs_exec_source(vm, "app.cs", source);
    /* or mcs_exec_image_xip(vm, app_image, sizeof app_image) for `mcs -C app.cs` output;
     * call back into C# later with mcs_call(vm, "OnTick", 0, NULL, NULL) and run
     * interrupt callbacks with mcs_hal_poll(vm) from your main loop. */
    mcs_free(vm);
    return r == MCS_OK ? 0 : -1;
}

/* ============================================================ Option 2: firmware */
#if MCS_ENABLE_RUNTIME
static uint8_t fw_heap[96 * 1024] __attribute__((aligned(8)));
static mcs_runtime_t rt;

void app_firmware_main(void) {
    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = fw_heap;
    cfg.heap_size = sizeof fw_heap;
    cfg.ramfs_size = 16 * 1024;             /* or cfg.fs_ops = &mcs_lfs_ops + cfg.fs_ctx */
    cfg.console = (mcs_transport_t){ console_read, console_write, NULL };
    cfg.ticks = port_ticks;
    cfg.delay = port_delay;
    cfg.hal = &board;
    cfg.setup = setup_bindings;
    for (;;) mcs_runtime_run(&rt, &cfg);    /* REPL, boot.cs/main.cs, jobs, callbacks */
}
#endif
