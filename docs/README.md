# MicroCS documentation

**New here?** Read the [main README](../README.md) first: it shows the two ways to use
MicroCS (a library inside your firmware, or the whole firmware as a C# REPL).

| | Guide | What you will find |
|---|---|---|
| 🧭 | [Step-by-step guides](guides/README.md) | from nothing to C# on your board: PC, ESP32, ESP32-C3 + OLED, Raspberry Pi Pico, STM32, Arduino, Zephyr |
| 🚀 | [Getting started](GETTING_STARTED.md) | build, first script, simulator, REPL, precompiling, embedding — in 10 minutes |
| 🔌 | [Hardware API](HAL.md) | GPIO, UART, I²C, SPI, ADC, DAC, PWM, timers, I²S, QSPI, CAN, watchdog, RTC; interrupts; writing a board table |
| 🎚️ | [Rotary encoders](ENCODER.md) | hardware-first PCNT / TIM / PIO counting, GPIO fallback, wiring, button handling and backend capabilities |
| 🚍 | [CAN bus](CAN.md) | classic CAN, native controllers and MCP2515, wiring, capabilities and per-port setup |
| 💤 | [Power management](POWER.md) | cooperative sleep, wake-input selection, light/deep sleep, shutdown; per-port capabilities and Studio/recovery precautions |
| 🎛️ | [Peripheral configuration](PERIPHERALS.md) | `GpioConfig`, `SpiConfig`, `UartConfig` …: pins, pulls, drive strength, filters, board options; what each board supports; implementing it in a port |
| ⚡ | [DMA](DMA.md) | `DmaBuffer`, `SPI.WriteAsync` & co., `DmaTransfer`, memory DMA; ordering and lifetime rules; per-board support; implementing it in a port |
| 📶 | [Wi-Fi & networking](NETWORK.md) | `Wifi` (station, soft AP, scan, events, static IP, saved networks), ESP-NOW, NTP, mDNS, ping, `TcpClient` / `TcpListener` / `UdpClient`, `Http` + HTTPS, `Json`; ESP32 setup; the `--sim` radio |
| 🔵 | [Bluetooth LE](BLE.md) | opt-in `Ble`: advertising and beacons, scanning, GATT server (`BleService`) and client (`BleConnection`), pairing / bonding, `BleUart`; ESP32 NimBLE, Pico W BTstack, Zephyr, ArduinoBLE; the `--sim` radio |
| 🖥️ | [U8g2 displays](U8G2.md) | optional C# `U8g2` / `U8x8` on olikraus' u8g2: turning it on in each build, choosing displays and fonts, fonts from files, the whole API, the `--oled` simulator |
| 🖼️ | [LVGL](LVGL.md) | optional C# widgets on the host's existing LVGL 8.x/9.x; explicit attachment, version policy, ownership, events and API coverage; [native-call reference](LVGL_NATIVE_API.md) |
| 🧩 | [Device drivers](DRIVERS.md) | the driver registry, the built-in `ws2812` (`LedStrip`) and `servo` (`Servo`) drivers and their backends, writing your own, turning drivers off |
| ⚙️ | [Configuration](CONFIGURATION.md) | the project config header `mcs_user_config.h`, `-D` options, profiles, precedence, per build system |
| 🧭 | [Porting & build systems](PORTING.md) | Make, CMake, ESP-IDF, Zephyr, PlatformIO, Arduino, CubeIDE/Keil/IAR; the vendor ports; new chips |
| 📟 | [Standalone firmware](STANDALONE.md) | `mcs_runtime`, the REPL, boot sequence, upload protocol, `mcs_remote.py` |
| 🔧 | [Embedding](EMBEDDING.md) | the C API: VMs, running code, C bindings, calling scripts, limits, GC rules |
| 🧩 | [Language](LANGUAGE.md) | the supported C# subset, feature by feature, and every known difference from .NET |
| 📄 | [JSON](JSON.md) | Parse/serialize nested data, fields and computed properties, `TryParse`, HTTP JSON APIs, memory and precision limits |
| 📚 | [Standard library](STDLIB.md) | generated reference of every class and member a script can call |
| 💾 | [Filesystem](FILESYSTEM.md) | VFS mounts, RAM / POSIX / TinyFS / LittleFS / YAFFS2 backends, flash ports, `File` / `Directory` / `Path`, bounded-memory `FileStream` byte reads/writes and seeking |
| ⏱️ | [Scheduler](SCHEDULER.md) | startup / once / periodic jobs, `jobs.cfg` |
| 🧵 | [Threads](THREADS.md) | optional real OS threads: FreeRTOS / Zephyr / POSIX, second core, `Channel` |
| 🪫 | [Small MCUs](LOW_RESOURCE.md) | running in 24–64 KB of RAM: profiles, execute-in-place images, GC tuning, measured results |
| 🧮 | [Memory budgets](MEMORY.md) | VM vs device heap counters, reserves, application replacement and reproducible low-RAM tests |
| 📊 | [Performance](PERFORMANCE.md) | benchmarks, Cortex-M footprint, memory findings — with commands to reproduce |
| 🏗️ | [Architecture](ARCHITECTURE.md) | pipeline, VM, GC, modules, invariants for contributors |
| 🧱 | [Bytecode](BYTECODE.md) | `.mcsb` image format, loader validation, full instruction set |
| 🛡️ | [Security](SECURITY.md) | what the sandbox guarantees and what it does not |
| 🧪 | [Testing](TESTING.md) | test matrix, port checks, fuzzing, sanitizers, .NET parity, adding tests |

Port guides: [STM32](../ports/stm32/README.md) · [ESP32](../ports/esp32/README.md) ·
[RP2040/RP2350](../ports/rp2/README.md) · [Zephyr](../ports/zephyr/README.md) ·
[Arduino](../ports/arduino/README.md) · [STM32 firmware](../ports/stm32/firmware/README.md)

Tools and examples: [Studio](../tools/studio/README.md) ·
[Template authoring](../tools/studio/templates/README.md) ·
[Examples](../examples/README.md) · [Low-RAM firmware](../examples/lowram/README.md) ·
[Runtime benchmark comparison](../bench/compare/README.md) ·
[Repeat-upload OLED regression](../tests/projects/oled_repeat/README.md)

The same pages are on the [wiki](https://github.com/amir1387aht/MicroCS/wiki) (published from
the repository on every documentation push to `main` by `tools/wiki_sync.py`;
edit the source files, not generated wiki pages). See [wiki publishing](WIKI.md)
for the full coverage check, preview generation and publishing workflow.

Project: [Changelog](../CHANGELOG.md) · [Contributing](../CONTRIBUTING.md) ·
[Code of conduct](../CODE_OF_CONDUCT.md) · [Security policy](../SECURITY.md) ·
[Pull request checklist](../.github/PULL_REQUEST_TEMPLATE.md)
