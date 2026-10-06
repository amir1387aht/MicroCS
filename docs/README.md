# MicroCS documentation

| | Guide | What you will find |
|---|---|---|
| 🚀 | [Getting started](GETTING_STARTED.md) | build, first script, simulator, precompiling, embedding in 5 minutes |
| 🧩 | [Language](LANGUAGE.md) | the supported C# subset, feature by feature, and every known difference from .NET |
| 📚 | [Standard library](STDLIB.md) | generated reference of every class and member a script can call |
| 🔧 | [Embedding](EMBEDDING.md) | the C API: VMs, running code, C bindings, calling scripts, GC rules |
| 🔌 | [HAL](HAL.md) | GPIO / UART / I²C / SPI / ADC / PWM from C#, writing a board table |
| 💾 | [Filesystem](FILESYSTEM.md) | VFS mounts, RAM / POSIX / LittleFS backends, `File` / `Directory` / `Path` |
| ⏱️ | [Scheduler](SCHEDULER.md) | startup / once / periodic jobs, `jobs.cfg` |
| 📟 | [Standalone runtime](STANDALONE.md) | boot sequence and the UART script-manager protocol |
| 🧭 | [Porting](PORTING.md) | host hooks per RTOS, Cortex-M reference port, SF32LB525 + RT-Thread outline |
| 🏗️ | [Architecture](ARCHITECTURE.md) | pipeline, VM, GC, modules, invariants for contributors |
| 🧱 | [Bytecode](BYTECODE.md) | `.mcsb` image format, loader validation, full instruction set |
| 📊 | [Performance](PERFORMANCE.md) | benchmarks, Cortex-M footprint, memory findings — with commands to reproduce |
| 🛡️ | [Security](SECURITY.md) | what the sandbox guarantees and what it does not |
| 🧪 | [Testing](TESTING.md) | test matrix, fuzzing, sanitizers, .NET parity, adding tests |

Project history: [CHANGELOG](../CHANGELOG.md) · [Phase 1 handoff](HANDOFF_PHASE1.md) ·
[Phase 2 handoff](HANDOFF_PHASE2.md) · [Phase 2 roadmap & design notes](PHASE2_ROADMAP.md) ·
[Contributing](../CONTRIBUTING.md)
