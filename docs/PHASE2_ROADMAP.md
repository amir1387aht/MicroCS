# MicroCS — Phase 2 Roadmap

## Baseline (Phase 1, measured before any Phase 2 change)

```
Build:            PASS, gcc 11.5 -std=gnu99 -Wall -Wextra, no warnings
Tests:            15/15 (6 programs x source+image, 3 diagnostics)
GC stress build:  6/6
Flag builds:      8/8 compile cleanly (FLOAT_DOUBLE=0, FLOAT=0, COMPILER=0, no DICT/LIST,
                  LAZY_REGS=0, COMPUTED_GOTO=0, no DISASM/LINES/SAVE, INT64=1)
Firmware demo:    runs (pool heap peak 79 KB of 96 KB)
Code size (x86-64 -Os): full 201.6 KB, runtime-only 138.9 KB, minimal 114.0 KB
Pre-existing gaps: Makefile references docs/PORTING.md (missing); no LICENSE/.gitignore;
                  asan target unusable (no libasan in sandbox)
```

Phase 1 already provides *both* execution modes required by the spec (source
interpretation through an on-device compiler, and precompiled `.mcsb` images run from
flash). Phase 2 therefore extends the platform around the existing core instead of
introducing a new compiler/VM.

## Architectural decision: core + modules

```
            C# source ──► lexer ► parser ► compiler ─┐         (MCS_ENABLE_COMPILER)
            .mcsb image ─────────────────────────────┴► VM + GC + stdlib   = core (src/)
                                                          │ public API: include/mcs.h
      ┌──────────────┬──────────────┬─────────────────────┼───────────────┐
  modules/fs     modules/hal    modules/sched        modules/shell     your natives
  (VFS + File)   (GPIO, UART…)  (jobs)               (standalone runtime, script mgmt)
      │              │                                    │
  backends:      board ops table                       transport (UART first)
  RAM, POSIX,    (sim, cortex-m demo, user HAL)
  LittleFS
```

Modules use **only the public API** (`mcs.h`) plus one small new core facility
(per-VM extension slots). That keeps them optional (don't link them, or set the
`MCS_ENABLE_*` flag to 0), testable on the host, and independent of storage or MCU
vendor. Small targets drop modules; they are never removed from MicroCS.

## Milestones (status at end of the Phase 2 session)

| # | Milestone | Delivered | Status |
|---|---|---|---|
| 2.1 | Core architecture | extension slots, `mcs_features`, `mcs_exec_auto`, version 1.1.0, build profiles (`include/profiles/`) | done |
| 2.2 | Language | `ref`/`out`/`in` (+ `TryParse`, `TryGetValue`), patterns + `when` in `case` labels | done (copy-in/copy-out, see LANGUAGE.md) |
| 2.3 | Compiled execution | Phase 1 `.mcsb`; measured source vs image on host and Cortex-M | done |
| 2.4 | Runtime API | `mcs_exec_file`, `mcs_exec_auto`, limits API, features API | done |
| 2.5 | Memory | compile OOM leak fixed, compiler-state recycling, token pre-sizing; per-target RAM measured | partial: ROM class metadata and streaming compile planned |
| 2.6 | Filesystem | VFS + RAM/POSIX/LittleFS backends, C# File/Directory/Path | done (LittleFS experimental) |
| 2.7 | Scheduling | startup/once/periodic jobs, restart policy, `jobs.cfg`, C# `Scheduler` | done; cron/RTC planned |
| 2.8 | HAL | board ops table, NULL = absent, simulator | done (no real-board backend) |
| 2.9 | Peripheral API | GPIO, UART, I2C, SPI, ADC, PWM, `Hal.Has` | done; CAN/I2S/RTC/display/BLE planned |
| 2.10 | MCU ports | bare-metal Cortex-M0/M4/M33 firmware, emulator-validated | experimental; RT-Thread/SF32LB525 planned |
| 2.11 | Security | time/step limits, RDONLY/NOEXEC mounts, path confinement, atomic uploads | done; signing/auth planned |
| 2.12 | Diagnostics | abort reasons, steps used, mem stats, shell `mem`/`info` | partial; debugger planned |
| 2.13 | Performance | `make bench`, `make cm-check`, `tools/map_sizes.py` | measured; VM unchanged (no profile-driven need yet) |
| 2.14 | Docs/examples/bench | README + 12 docs, Cortex-M demo, .NET cross-check tool | done |

## Research notes that drove the decisions

* **MicroPython**: VFS with mountable block-device filesystems (FAT, LittleFS), `boot.py`
  then `main.py`, raw REPL / paste mode over UART for `mpremote`. MicroCS mirrors the
  mount model, the boot-script convention (`/boot.cs`, `/main.cs`) and a line protocol
  with length-prefixed uploads (binary-safe, no escaping on slow UARTs).
* **LittleFS** is the de-facto power-loss-safe flash filesystem on MCUs (MicroPython,
  Zephyr, Mbed). MicroCS adapts it rather than inventing a flash format.
* **Lua / Wren**: tiny core, everything else is a library loaded by the host; host owns
  the loop. MicroCS keeps the host-driven loop: the scheduler is *polled*
  (`mcs_sched_poll`), so it works the same on bare metal and in an RTOS task.
* **nanoFramework**: CLR on top of PAL/HAL with per-board feature selection. MicroCS
  uses a single ops table per board with capability bits rather than a large PAL.
* **Stack vs register VM**: Lua 5 moved to registers for speed; Wren, clox, CPython and
  MicroPython stay stack-based for code density and simple compilers. The Phase 1
  stack VM is already competitive with CPython (see README). Rewriting it would risk
  P0 behaviour for an unmeasured gain, so it is kept; dispatch is already computed-goto.
* **Execution limits** follow Lua's `debug.sethook(count)` approach: check budgets at the
  existing safepoints (backward jumps/calls), so straight-line code adds no overhead.

## What is explicitly *not* claimed

* No real-hardware port is validated. The Cortex-M build is compiled with
  arm-none-eabi-gcc and executed in a CPU emulator (Unicorn); that is "experimental".
* The 2 KB SRAM / 16 KB flash class cannot run the current VM: the smallest measured
  Cortex-M0 runtime is far above 16 KB of flash (see docs/PERFORMANCE.md). A "Tiny"
  profile needs a different, much smaller VM design — planned, not implemented.
