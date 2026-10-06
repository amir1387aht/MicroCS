# Porting and build integration

MicroCS is plain C99 with no dependencies. Wherever your C compiles, MicroCS compiles.
This page explains how to add it to a project (any build system), which port to use, and how
to support a new chip.

## Requirements

| | Full (compiler on device) | Runtime only (precompiled images) | Low-RAM profile |
|---|---|---|---|
| Flash | ~213 KB | ~168 KB | ~144 KB |
| VM heap after `mcs_new` | ~25 KB | ~25 KB | ~13 KB |
| Heap while compiling a 2.5 KB script | ~90 KB peak | — | — |
| C stack for the VM task | 3–8 KB | 3–8 KB | 3 KB |

Flash numbers are Cortex-M, `-Os`, including newlib + libm (~49 KB). Details:
[PERFORMANCE.md](PERFORMANCE.md) · small parts: [LOW_RESOURCE.md](LOW_RESOURCE.md).

## 1. Add MicroCS to your build

Sources: `src/*.c` (core) + `modules/*/*.c` (optional: fs, hal, sched, shell, runtime) +
`ports/<port>/mcs_port_<port>.c` (optional). Include paths: `include/` and `ports/<port>/`.
Modules that cannot build on a target compile to nothing (e.g. the POSIX filesystem on an MCU).

### Make (any Makefile project, incl. CubeMX "Makefile" projects)
```make
MICROCS_DIR  := third_party/MicroCS
MICROCS_PORT := stm32                 # optional
include $(MICROCS_DIR)/microcs.mk
C_SOURCES    += $(MICROCS_SRCS)
C_INCLUDES   += $(MICROCS_INCS)
C_DEFS       += $(MICROCS_DEFS)       # set MICROCS_PROFILE := lowram to use a profile
```
`MICROCS_CORE_ONLY := 1` builds only the language core.

### CMake (any CMake project, incl. CubeMX/CubeIDE CMake, pico-sdk)
```cmake
set(MICROCS_PORT stm32)               # stm32 | rp2 | esp32 | template | "" (none)
set(MICROCS_PROFILE embedded)         # tiny | mcu | embedded | lowram | linux | "" (default)
add_subdirectory(third_party/MicroCS)
target_link_libraries(${PROJECT_NAME} microcs)
```
With `MICROCS_PORT stm32` the library links the generated `stm32cubemx` target, so it sees
your HAL configuration; with `rp2` it links the `hardware_*` libraries of the pico-sdk.
`MICROCS_CONFIG=path/to/my_config.h` uses your own config header; `MICROCS_MODULES=OFF`
builds only the core. On the host it also builds the `mcs` CLI.

### ESP-IDF
Clone into `components/MicroCS` (or add it with the IDF component manager — `idf_component.yml`
is included). The same `CMakeLists.txt` detects ESP-IDF and registers a component with the
ESP32 port. In your `main/CMakeLists.txt` nothing is needed (IDF links all components) — see
[ports/esp32/example](../ports/esp32/example).

### Zephyr
Add the repo as a module (`west.yml` project or `-DZEPHYR_EXTRA_MODULES=path/to/MicroCS`)
and enable it in `prj.conf`:
```
CONFIG_MICROCS=y
```
`zephyr/module.yml`, `zephyr/CMakeLists.txt` and `zephyr/Kconfig` do the rest. See
[ports/zephyr/example](../ports/zephyr/example).

### PlatformIO
```ini
[env:board]
lib_deps = https://github.com/amir1387aht/MicroCS
```
`library.json` + `tools/pio_build.py` select the port from the framework: `espidf` → ESP32
port, `arduino` → Arduino port, `stm32cube` → STM32 port, `zephyr` → Zephyr port.

### Arduino IDE
```sh
python3 tools/make_arduino.py          # → dist/arduino/MicroCS/ and dist/arduino/MicroCS-1.4.0.zip
```
Sketch → Include Library → Add .ZIP Library, then open *File → Examples → MicroCS*.

### STM32CubeIDE, Keil MDK, IAR, SEGGER Embedded Studio, MPLAB, CCS…
Add the folders `src`, `modules`, `ports/<port>` as source folders and `include`,
`ports/<port>` as include paths. No special compiler flags, no extensions; GCC, Clang,
ARMCC 6, IAR and MSVC (host) are fine. Recommended: `-Os` (or `-O2` for speed).

### Configuration
All options are `#define`s with defaults in [`include/mcs_config.h`](../include/mcs_config.h).
Override with `-D…` or point `MCS_USER_CONFIG_FILE` at a header. Ready-made profiles in
`include/profiles/`: `tiny`, `mcu`, `lowram`, `embedded`, `linux`. Example:
`-DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_embedded.h"'`.

## 2. Use a port (vendor SDK)

A port maps the C# peripheral API to the vendor's own SDK. It includes the **SDK's headers
from your project** — CubeMX handles, ESP-IDF drivers, pico-sdk `hardware_*`, Zephyr device
drivers — so it follows your chip variant, pin muxing and clocks. MicroCS never pokes
registers behind the SDK's back.

| Port | SDK | Chips | Console helper | Docs |
|---|---|---|---|---|
| `ports/stm32` | STM32Cube HAL (CubeMX) | C0, F0–F7, G0, G4, H5, H7, L0–L5, U5, WB, WL | `mcs_stm32_console(&board, uart)` | [README](../ports/stm32/README.md) |
| `ports/esp32` | ESP-IDF 5.0+ | ESP32, S2, S3, C3, C6, H2, P4 | `mcs_esp32_console_usb()` / `_uart(n, baud)` | [README](../ports/esp32/README.md) |
| `ports/rp2` | pico-sdk 1.5 / 2.x | RP2040, RP2350 (Arm and RISC-V) | `mcs_rp2_console_stdio()` | [README](../ports/rp2/README.md) |
| `ports/zephyr` | Zephyr 3.4+ | every board with devicetree support | `mcs_zephyr_console()` | [README](../ports/zephyr/README.md) |
| `ports/arduino` | Arduino API | ESP32, RP2040, SAMD, nRF52, STM32duino, Teensy, Renesas | `mcs_arduino_console(&Serial)` | [README](../ports/arduino/README.md) |
| `ports/cortex-m` | none (bare metal) | Cortex-M0/M4/M33 reference | UART registers | below |
| `ports/unix` | POSIX | Linux, macOS | stdin/stdout | the `mcs` CLI |

Every port provides `mcs_<port>_hal_init(&hal, &cfg)`, `mcs_<port>_ticks`, `mcs_<port>_delay`
and a console transport, which is all `mcs_runtime` needs.

## 3. Host hooks (library use without a port)

| Hook | Bare metal | FreeRTOS | Zephyr | ESP-IDF | pico-sdk |
|---|---|---|---|---|---|
| `realloc_fn` | `mcs_pool_realloc` on a static buffer (recommended everywhere) | same | same | same | same |
| `write_fn` | UART TX | UART driver | `printk` / UART API | `fwrite(stdout)` / `uart_write_bytes` | `stdio_put_string` |
| `ticks_fn` | SysTick ms | `xTaskGetTickCount() * portTICK_PERIOD_MS` | `k_uptime_get_32()` | `esp_timer_get_time() / 1000` | `to_ms_since_boot(get_absolute_time())` |
| `delay_fn` | busy wait / `WFI` | `vTaskDelay(pdMS_TO_TICKS(ms))` | `k_msleep(ms)` | `vTaskDelay` | `sleep_ms` |
| `hook_fn` | feed watchdog, poll break flag | `taskYIELD()` | `k_yield()` | `taskYIELD()` | — |

Run each VM in one task/thread — a VM is not thread-safe, but several VMs can run in
different tasks. `mcs_request_abort(vm)` may be called from an ISR or another task to stop a
script at the next safe point. Peripheral events from ISRs go through `mcs_hal_post()`.

## 4. Support a new chip

1. Copy [`ports/template/mcs_port_template.c`](../ports/template/mcs_port_template.c).
2. Fill the console (`read`/`write`), `ticks`, `delay` and the `mcs_hal_t` members your chip
   has (GPIO first — everything else is optional; see [HAL.md](HAL.md#writing-a-board-table)).
3. Post interrupts with `mcs_hal_post(MCS_HAL_EV_GPIO, pin, level)` and timer ticks with
   `MCS_HAL_EV_TIMER`.
4. Run `examples/hardware/*.cs` on the board — they are the acceptance tests for a port.
5. Send a pull request: add a `tools/check_ports.sh`-style compile check if the SDK headers are
   freely downloadable.

## 5. Bare-metal Cortex-M reference (`ports/cortex-m/`)

A minimal firmware without any SDK: `startup.c` (vector table, `.data`/`.bss`, stack painting,
FPU enable), `cm.ld`, `board.c` (UART + tick drivers for a minimal SoC + newlib stubs) and
`main.c` (pool heap, VM, RAM FS, scheduler, demo image, optional shell). Targets (`make cm`):
`m0-runtime`, `m0-lowram`, `m0-node` (48 KB RAM), `m4-full`, `m33-full`, `m33-shell`.
`make cm-check` executes them instruction by instruction and requires the output to be
byte-identical to the host interpreter — this is how the code generation, ABI, alignment,
soft/hard float paths and the memory budgets are checked for each core. To use it on a real
chip, replace `board.c` with your UART/SysTick code and adapt `cm.ld` (or use one of the
vendor ports above, which already do this).

## 6. Compile checks for the ports

`tools/check_ports.sh` compiles a port against the real vendor headers without hardware:

```sh
tools/check_ports.sh stm32 f4 STM32F446xx cortex-m4     # downloads the Cube HAL headers into build/sdk
tools/check_ports.sh stm32 h7 STM32H743xx cortex-m7
```

CI runs it for 12 STM32 families, builds the RP2 example for `pico` and `pico2`, and builds
the ESP-IDF example for ESP32, S3, C3 and C6 (`.github/workflows/ci.yml`).
