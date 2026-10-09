/*
 * MicroCS port template - copy this file, replace every TODO with your SDK's
 * calls, delete what your chip does not have (NULL entries simply hide the
 * C# class), and pick one of the two entry points at the bottom:
 *
 *   app_script_start()   Option 1: run C# inside your existing firmware
 *   app_firmware_main()  Option 2: the whole firmware is a C# REPL (mcs_runtime)
 *
 * Also here: a backend for the built-in "ws2812" driver (C# LedStrip), a driver
 * of your own (C# Buzzer, include/mcs_driver.h, docs/DRIVERS.md) and the
 * internal-flash driver that gives LittleFS / YAFFS2 a place to live
 * (mcs_flash_t, include/mcs_flash.h, docs/FILESYSTEM.md).
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
#include "mcs_driver.h"
#include "mcs_flash.h"
#include "mcs_vfs.h"
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
     * delay_us, reset, unique_id, cpu_hz, poll_event. Addressable LEDs, displays, sensors ...
     * are drivers (below), not HAL members. */
};

/* ============================================================ drivers (mcs_driver.h)
 * 1. Backend for the built-in "ws2812" driver: C# LedStrip works once write() sends
 *    the bytes (already in wire order, brightness applied) at 800 kHz. Typical ways:
 *    a PIO / RMT-like peripheral, SPI MOSI at 2.4 MHz (3 SPI bits per LED bit), a timer
 *    + DMA into the GPIO set/reset register, or a cycle-counted loop with IRQs off. */
#if MCS_ENABLE_WS2812
static int ws2812_write(void* ctx, int pin, const uint8_t* data, size_t n, int order) {
    /* TODO: send n bytes MSB first on `pin`: 0 = 0.4 us high + 0.85 us low,
     * 1 = 0.8 us high + 0.45 us low; then hold the line low >= 280 us (latch). */
    return MCS_HAL_ENOTSUP;
}
static const mcs_ws2812_ops_t ws2812_ops = { ws2812_write };
static const mcs_driver_t ws2812_driver = MCS_WS2812_DRIVER(&ws2812_ops, NULL);
#endif

/* 2. A driver of your own: C# Buzzer.Beep(hz, ms) on a board buzzer. The front end
 *    (C# API) and the backend (buzzer_ops_t) are split so another board only swaps
 *    the ops. Register it with mcs_driver_register() before the VM starts. */
#if MCS_ENABLE_DRIVERS
typedef struct { int (*tone)(void* ctx, uint32_t hz, uint32_t ms); } buzzer_ops_t;
static int buzzer_tone(void* ctx, uint32_t hz, uint32_t ms) { return MCS_HAL_ENOTSUP; /* TODO: PWM on the buzzer pin */ }
static const buzzer_ops_t buzzer_ops = { buzzer_tone };
static mcs_value_t buzzer_beep(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    const mcs_driver_t* d = mcs_driver_find("buzzer");
    mcs_int_t hz = mcs_to_int(vm, argv[0]), ms = argc > 1 ? mcs_to_int(vm, argv[1]) : 100;
    if (mcs_has_exception(vm)) return mcs_null();
    if (hz < 1 || ms < 0) return mcs_hal_raise(vm, "Buzzer.Beep", MCS_HAL_EINVAL);
    int rc = ((const buzzer_ops_t*)d->ops)->tone(d->ctx, (uint32_t)hz, (uint32_t)ms);
    return rc < 0 ? mcs_hal_raise(vm, "Buzzer.Beep", rc) : mcs_null();
}
static const mcs_reg_t buzzer_fns[] = { MCS_FN("Beep", buzzer_beep, -1), MCS_REG_END };
static void buzzer_open(mcs_vm_t* vm, const mcs_driver_t* drv) { mcs_register_module(vm, "Buzzer", buzzer_fns); }
static const mcs_driver_t buzzer_driver = { "buzzer", "Buzzer", buzzer_open, &buzzer_ops, NULL };
#endif

static void register_drivers(void) {
#if MCS_ENABLE_WS2812
    mcs_driver_register_default(&ws2812_driver);
#endif
#if MCS_ENABLE_DRIVERS
    mcs_driver_register(&buzzer_driver);
#endif
}

/* ============================================================ internal flash -> files
 * A mcs_flash_t for the chip's own flash (or an external NOR / NAND: mcs_spinor_init /
 * mcs_spinand_init in mcs_flash.h need only a SPI transfer function). LittleFS and
 * YAFFS2 sit on it through mcs_flashfs_mount(); build with -DMICROCS_FS=littlefs or
 * yaffs2 (CMake) or compile the sources yourself with MCS_ENABLE_LFS / MCS_ENABLE_YAFFS.
 * Keep the region away from the firmware image (linker script). Used by Option 2 below. */
#define TEMPLATE_FLASH_FS (MCS_ENABLE_FLASH && (MCS_ENABLE_LFS || MCS_ENABLE_YAFFS) && MCS_ENABLE_RUNTIME)
#if TEMPLATE_FLASH_FS
#define FS_BASE   0x08080000u            /* TODO: first byte of the files region */
#define FS_SIZE   (256u * 1024u)         /* TODO: size, a multiple of the erase block */
#define FS_BLOCK  4096u                  /* TODO: erase unit (page / sector size) */
static int fl_read(mcs_flash_t* f, uint32_t addr, void* buf, uint32_t n) {
    memcpy(buf, (const void*)(uintptr_t)(FS_BASE + addr), n);   /* memory-mapped flash */
    return 0;
}
static int fl_prog(mcs_flash_t* f, uint32_t addr, const void* buf, uint32_t n) {
    /* TODO: unlock, program n bytes at FS_BASE + addr in the chip's write unit, lock.
     * Return MCS_FLASH_EPROG on a verify / status error. */
    return MCS_FLASH_EIO;
}
static int fl_erase(mcs_flash_t* f, uint32_t block) {
    /* TODO: erase FS_BASE + block * FS_BLOCK (must read back as 0xFF) */
    return MCS_FLASH_EIO;
}
static mcs_flash_t board_flash = {
    .type = MCS_FLASH_NOR, .page_size = 256, .block_size = FS_BLOCK, .block_count = FS_SIZE / FS_BLOCK,
    .read = fl_read, .prog = fl_prog, .erase = fl_erase,
    .write_size = 8,                     /* smallest program unit (bytes): 8 on STM32L4/G4, 16/32 on H7 */
};
#endif

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

    register_drivers();
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
    cfg.ramfs_size = 16 * 1024;             /* RAM disk unless the flash filesystem mounts: */
#if TEMPLATE_FLASH_FS
    static mcs_flashfs_t fs;
    if (mcs_flashfs_mount(&fs, &board_flash, 0, 0, MCS_FLASHFS_DEFAULT, MCS_FLASHFS_FORMAT_IF_NEEDED) == 0) {
        cfg.fs_ops = fs.ops;                /* /boot.cs, /main.cs, uploads survive resets */
        cfg.fs_ctx = fs.ctx;
    }
#endif
    cfg.console = (mcs_transport_t){ console_read, console_write, NULL };
    cfg.ticks = port_ticks;
    cfg.delay = port_delay;
    cfg.hal = &board;
    cfg.setup = setup_bindings;
    register_drivers();
    for (;;) mcs_runtime_run(&rt, &cfg);    /* REPL, boot.cs/main.cs, jobs, callbacks */
}
#endif
