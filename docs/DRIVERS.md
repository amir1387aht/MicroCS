# Device drivers (`mcs_driver.h`, `modules/drivers`)

A **driver** adds C# classes for one kind of device — an LED strip, a display, a
sensor, a motor controller — on top of the board. MicroCS ships one built-in driver
(`ws2812`, the C# `LedStrip` class); you can add your own without touching MicroCS,
or swap the backend of a built-in one.

Every driver has two halves:

| Half | What it is | Who writes it |
|---|---|---|
| **front end** | the chip-independent C# API: argument checks, buffers, the classes. It is the driver's `open(vm, drv)` function, run for every VM the HAL library is opened on (`mcs_hal_open_lib`, `mcs_runtime`) | the driver author, once |
| **backend** | the MCU-specific part: a table of C functions (`ops`) plus a context pointer | the port (one per MCU) or your firmware |

```c
typedef struct mcs_driver {
    const char* name;      /* registry key, lower case: "ws2812"  -> Drivers.Has / Drivers.List */
    const char* classes;   /* C# names it adds, space separated: "LedStrip" -> Hal.Has */
    void (*open)(mcs_vm_t* vm, const mcs_driver_t* drv);   /* register the C# API */
    const void* ops;       /* backend function table - its type is defined by the driver */
    void* ctx;             /* backend context, handed back to the ops */
} mcs_driver_t;
```

Drivers live in a small global table (`MCS_MAX_DRIVERS`, default 8) filled before
the VM starts:

| Call | |
|---|---|
| `mcs_driver_register(&drv)` | add, or replace the driver with the same name |
| `mcs_driver_register_default(&drv)` | add unless that name is registered — the ports use this, so a driver you registered first wins |
| `mcs_driver_unregister("ws2812")` | remove one (e.g. a built-in you do not want) |
| `mcs_driver_find(name)`, `mcs_driver_count()`, `mcs_driver_at(i)`, `mcs_driver_provides(name)` | look-ups |
| `mcs_drivers_open(vm)` | done by `mcs_hal_open_lib`; call it yourself only on a VM without the HAL library |

Helpers for front ends (`mcs_hal.h`): `mcs_hal_pin_arg(vm, value)` (a pin number or a
board name such as `"GP16"`, `"PA5"`, `"NEOPIXEL"`) and `mcs_hal_raise(vm, "Op", rc)`
(the C# exception for a negative `MCS_HAL_E*` code — `NotSupportedException`,
`TimeoutException`, `ArgumentException`, `IOException`).

C# side:

```csharp
Console.WriteLine(string.Join(", ", Drivers.List));   // ws2812, buzzer
if (Drivers.Has("ws2812")) { var s = new LedStrip("NEOPIXEL", 1); s[0] = 0x00FF00; s.Show(); }
Hal.Has("LedStrip");                                  // also true for the classes a driver adds
```

## Build options

| | Option | Default |
|---|---|---|
| all drivers | `MCS_ENABLE_DRIVERS` | = `MCS_ENABLE_HAL` (off in the `tiny` / `min` profiles) |
| `ws2812` (C# `LedStrip`) | `MCS_ENABLE_WS2812` | = `MCS_ENABLE_DRIVERS` |

How to turn `ws2812` off in each build:

| Build | |
|---|---|
| CMake (pico-sdk, STM32CubeMX, host) | `-DMICROCS_WS2812=OFF` (on RP2 it also drops the `hardware_pio` link) |
| ESP-IDF | menuconfig → MicroCS → *WS2812 / NeoPixel driver* (`CONFIG_MICROCS_WS2812=n`) |
| Zephyr | `CONFIG_MICROCS_WS2812=n` (it is only registered when `CONFIG_LED_STRIP=y` and the `led-strip` alias exists) |
| Arduino IDE | `python3 tools/make_arduino.py --no-ws2812` (Arduino has no `-D` for libraries; the option is written into the packaged `mcs_config.h`) |
| PlatformIO | `build_flags = -DMCS_ENABLE_WS2812=0` |
| Make (`microcs.mk`) / anything else | `MICROCS_WS2812 := 0` (+ `C_DEFS += $(MICROCS_DEFS)`), or `-DMCS_ENABLE_WS2812=0` |

Without it, `LedStrip` is not registered and `Hal.Has("LedStrip")` / `Drivers.Has("ws2812")`
are false; the rest of the firmware is unchanged (3–5 KB of flash saved with the backend).

## The built-in `ws2812` driver

Front end: `modules/drivers/mcs_drv_ws2812.c` (C# API in [HAL.md](HAL.md#ledstrip-ws2812--neopixel)).
It keeps `0xWWRRGGBB` pixels, applies `Brightness`, puts the bytes in the strip's wire
order and calls the backend's one function:

```c
typedef struct {
    /* send n bytes MSB first on `pin` (T0H 0.4 us, T1H 0.8 us, 1.25 us per bit), then
     * hold the line low >= 280 us; order = MCS_LED_GRB / _RGB / _GRBW. 0 or MCS_HAL_E*. */
    int (*write)(void* ctx, int pin, const uint8_t* data, size_t n, int order);
} mcs_ws2812_ops_t;
```

Backends per MCU:

| Port | Backend | Where |
|---|---|---|
| RP2040 / RP2350 (pico-sdk, Arduino-Pico) | PIO state machine per pin | `include/mcs_ws2812_backends.h` (`MCS_WS2812_RP2_PIO`) |
| ESP32 family with RMT (ESP-IDF ≥ 5, Arduino-ESP32 3.x) | RMT TX channel per pin | `include/mcs_ws2812_backends.h` (`MCS_WS2812_ESP32_RMT`) |
| STM32 (all families) | cycle-timed bit-bang on SysTick | `ports/stm32/mcs_port_stm32.c` |
| Zephyr | `led_strip` API on the `led-strip` alias | `ports/zephyr/mcs_port_zephyr.c` |
| Other Arduino cores | Adafruit_NeoPixel (when the sketch includes it) | `ports/arduino/mcs_port_arduino.cpp` |
| Simulator (`mcs --sim`) | logs the frames (`--sim-log`) | `modules/hal/mcs_hal_sim.c` |
| a new MCU | fill in `ws2812_write` | `ports/template/mcs_port_template.c` |

Replace the backend from your firmware — e.g. SPI MOSI at 2.4 MHz with DMA instead of
the bit-bang loop on STM32 — by registering before the port's HAL init (or after it with
`mcs_driver_register`, which replaces):

```c
static int spi_ws2812(void* ctx, int pin, const uint8_t* d, size_t n, int order) { /* ... */ return 0; }
static const mcs_ws2812_ops_t my_ops = { spi_ws2812 };
static const mcs_driver_t my_ws2812 = MCS_WS2812_DRIVER(&my_ops, &hspi2);
mcs_driver_register(&my_ws2812);        /* LedStrip.Show() now calls spi_ws2812 */
```

## Writing your own driver

A complete driver — C# `Buzzer.Beep(hz[, ms])` — with the backend split out so another
board only provides a different `buzzer_ops_t` (the same code is in
`ports/template/mcs_port_template.c`):

```c
#include "mcs_driver.h"

/* ---- the backend interface (one per driver, your choice) */
typedef struct { int (*tone)(void* ctx, uint32_t hz, uint32_t ms); } buzzer_ops_t;

/* ---- front end: the C# API */
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

/* ---- backend for this board */
static int my_tone(void* ctx, uint32_t hz, uint32_t ms) { /* PWM on the buzzer pin */ return 0; }
static const buzzer_ops_t my_buzzer = { my_tone };
static const mcs_driver_t buzzer = { "buzzer", "Buzzer", buzzer_open, &my_buzzer, NULL };

void board_init(void) {
    mcs_driver_register(&buzzer);       /* before mcs_runtime_run / mcs_hal_open_lib */
}
```

Object classes work the same way: register an `mcs_class_def_t` in `open` (see
`mcs_drv_ws2812.c` for a class with a constructor, finalizer, indexer and constants).
Keep descriptors `static const` (they must outlive every VM), allocate per-object memory
with `mcs_mem_realloc(vm, ...)` and free it in the class finalizer.

To ship a driver inside MicroCS, put the front end in `modules/drivers/mcs_drv_<name>.c`
behind its own `MCS_ENABLE_<NAME>` switch (default it in `include/mcs_config.h`), the
backends in the ports, a test in `tests/` (simulator backend) and a row in this file.
`modules/*/*.c` is picked up by every build system (Make, CMake, ESP-IDF, Zephyr,
PlatformIO, Arduino packaging).
