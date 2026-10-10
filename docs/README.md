# MicroCS documentation

**New here?** Read the [main README](../README.md) first: it shows the two ways to use
MicroCS (a library inside your firmware, or the whole firmware as a C# REPL).

| | Guide | What you will find |
|---|---|---|
| 🚀 | [Getting started](GETTING_STARTED.md) | build, first script, simulator, REPL, precompiling, embedding — in 10 minutes |
| 🔌 | [Hardware API](HAL.md) | GPIO, UART, I²C, SPI, ADC, DAC, PWM, timers, I²S, QSPI, CAN, watchdog, RTC; interrupts; writing a board table |
| 🧩 | [Device drivers](DRIVERS.md) | the driver registry, the built-in `ws2812` (`LedStrip`) and `servo` (`Servo`) drivers and their backends, writing your own, turning drivers off |
| ⚙️ | [Configuration](CONFIGURATION.md) | the project config header `mcs_user_config.h`, `-D` options, profiles, precedence, per build system |
| 🧭 | [Porting & build systems](PORTING.md) | Make, CMake, ESP-IDF, Zephyr, PlatformIO, Arduino, CubeIDE/Keil/IAR; the vendor ports; new chips |
| 📟 | [Standalone firmware](STANDALONE.md) | `mcs_runtime`, the REPL, boot sequence, upload protocol, `mcs_remote.py` |
| 🔧 | [Embedding](EMBEDDING.md) | the C API: VMs, running code, C bindings, calling scripts, limits, GC rules |
| 🧩 | [Language](LANGUAGE.md) | the supported C# subset, feature by feature, and every known difference from .NET |
| 📚 | [Standard library](STDLIB.md) | generated reference of every class and member a script can call |
| 💾 | [Filesystem](FILESYSTEM.md) | VFS mounts, RAM / POSIX / TinyFS / LittleFS / YAFFS2 backends, flash ports, `File` / `Directory` / `Path` |
| ⏱️ | [Scheduler](SCHEDULER.md) | startup / once / periodic jobs, `jobs.cfg` |
| 🪫 | [Small MCUs](LOW_RESOURCE.md) | running in 24–64 KB of RAM: profiles, execute-in-place images, GC tuning, measured results |
| 📊 | [Performance](PERFORMANCE.md) | benchmarks, Cortex-M footprint, memory findings — with commands to reproduce |
| 🏗️ | [Architecture](ARCHITECTURE.md) | pipeline, VM, GC, modules, invariants for contributors |
| 🧱 | [Bytecode](BYTECODE.md) | `.mcsb` image format, loader validation, full instruction set |
| 🛡️ | [Security](SECURITY.md) | what the sandbox guarantees and what it does not |
| 🧪 | [Testing](TESTING.md) | test matrix, port checks, fuzzing, sanitizers, .NET parity, adding tests |

Port guides: [STM32](../ports/stm32/README.md) · [ESP32](../ports/esp32/README.md) ·
[RP2040/RP2350](../ports/rp2/README.md) · [Zephyr](../ports/zephyr/README.md) ·
[Arduino](../ports/arduino/README.md) · examples: [examples/](../examples/README.md)

Project history: [CHANGELOG](../CHANGELOG.md) · [Contributing](../CONTRIBUTING.md)
