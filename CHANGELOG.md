# Changelog

All notable changes. Versions follow `MCS_VERSION_*` in `include/mcs.h`.

## Unreleased

* **MicroCS Studio** (`tools/studio/`, online at https://amir1387aht.github.io/MicroCS/): a
  browser IDE over Web Serial — device file manager (upload/download/rename/delete, drag & drop,
  free space), C# editor with Visual Studio 2022 Dark colours, API completion and 40+ snippets
  that saves and runs on the device, 85 templates and examples (boot scripts, `jobs.cfg`,
  scheduler patterns, every HAL peripheral, files, C# features) and a REPL / shell console.
  Connecting recovers an ESP32 left in its ROM bootloader by the port's DTR/RTS lines. Tested
  in CI against the host shell (`mcs --repl --echo`, new `--echo` flag); every template is
  compiled and run on the simulated board.
* Docs: the I2S direction constants are `I2S.Transmit` / `Receive` / `Duplex` (HAL.md said `Tx`/`Rx`).
* **ESP32: scripts on flash** — the example mounts LittleFS on the `storage` partition
  (`mcs_esp32_littlefs()`), so uploaded files survive resets and power cycles.
* **Free space**: C# `DriveInfo` (`new DriveInfo("/").AvailableFreeSpace`, `TotalSize`,
  `DriveFormat`, `DriveInfo.GetDrives()`), shell/`mcs_remote.py` command `df`, and
  `mcs_vfs_statfs()` with an optional `statfs` backend op (RAM, POSIX, LittleFS, YAFFS2,
  ESP32 LittleFS).

## 1.6.0 — fast bytecode images

Focus: a precompiled image is now clearly the fast path. Up to 1.5 an image held the same
bytecode the on-device compiler emits, so it only saved the compile step (a few ms) and was
larger than the source. Numbers: [PERFORMANCE.md](docs/PERFORMANCE.md#bytecode-images).

### Speed (emulated Cortex-M4F instructions, image run, 1.5 → 1.6)
- `fib` 2.43 → 1.43 M (1.7×), `loop` 15.0 → 6.70 M (2.2×), `objects` 5.64 → 3.19 M (1.8×),
  `sensor` 0.87 → 0.68 M (1.3×), `strings` 0.63 → 0.49 M (1.3×); `demo.cs` image 3.44 → 2.18 M
  on M4F and 4.54 → 2.96 M on M0. Host: fib(30) 73 → 41 ms, 10 M loop 322 → 95 ms.
- Images now run 1.1–2.0× faster than the same script from source and need about half the RAM
  peak (no compiler state): e.g. `fib` 31.4 → 15.1 KB, `sensor` 40.7 → 17.0 KB.

### Bytecode optimizer (`src/mcs_opt.c`)
- 71 **superinstructions** (opcodes 86–156, image version 2+): local/immediate/constant
  arithmetic with optional store, compare-and-branch on locals/immediates/constants,
  accumulate (`x += e`), `a[i]` / `a[i] = v` and `obj.f` / `obj.f = v` on locals,
  `return local`. Jump threading, loop rotation (one branch per iteration), dead-code removal.
- `mcs -c` / `mcs -C` / `mcs_compile_image*` optimize by default; `mcs -O0` and
  `MCS_IMAGE_NO_OPT` produce plain bytecode. Source run on the device is unchanged unless
  `MCS_OPTIMIZE_SOURCE=1`.
- New switches `MCS_ENABLE_SUPEROPS` (default 1; **0 in `mcs_profile_min.h`**, which also turns
  `MCS_FIELD_CACHE` off) and
  `MCS_ENABLE_OPTIMIZER`. A VM without superinstructions rejects optimized images with a clear
  error; `examples/lowram/node_image.h` is now built with `-O0` so it runs on every profile.

### VM
- Stack pointer kept in a register; in-place int fast paths for arithmetic, shifts, compares,
  `a[i]`, `a[i] = v`; `array.Length` / `List.Count` / `string.Length` without lookup.
- `INVOKE` method inline cache per call site, constructor cache for `new C(...)`, direct
  closure entry, cheaper `RETURN`; caches are invalidated by a class epoch.

### Image format v3
- Varints, delta-coded line tables, every string stored once, f32 constants when exact.
  Images are 25–45 % smaller (host `demo.cs` 2763 → 1942 B; `bench/mcu/loop.cs` image 166 B for
  232 B of source). v1 and v2 images still load.

### Fixes
- Typed top-level variables used inside functions declared before them had the wrong type
  (`double ema = 0; void F() { ema = ema * 0.5; }` did integer arithmetic).
- Calls of hoisted top-level functions keep the declared return type (no extra conversion).
- `foreach (var p in list) acc = …` no longer emitted a stray tuple-names call.
- An interpolation format spec longer than 95 characters overflowed a compiler buffer (found by
  `tools/fuzz.py`; present since 1.0).
- Image loader: `FIELD`'s default-value operand is range-checked (found by `tools/fuzz.py --image`).
- CLI builds with `MCS_ENABLE_COMPILER=0` / `MCS_ENABLE_BYTECODE_SAVE=0` compile warning-free.

### Tooling
- `make mcu-bench` (`tools/mcu_bench.sh`, `bench/mcu/*.cs`, `ports/cortex-m/bench.c`):
  image vs `-O0` image vs source on emulated M4F/M0, outputs checked against the host.
- `make check`: +3 flag builds and +3 full-suite configurations (`MCS_ENABLE_SUPEROPS=0`,
  `MCS_OPTIMIZE_SOURCE=1`, switch dispatch); new `tests/t17_optimizer.cs`.

### Costs
- Flash: +8–11 KB on Cortex-M with superinstructions (`m0-runtime` 208.5 KB, `m4-full`
  253.1 KB); the `min` build (superinstructions and inline caches off) stays at 60.6 KB of 64 KB.
- Compiling an image on the PC takes longer (optimizer): host `demo.cs` 83 → 120 µs.

## 1.5.0 — 16 KB RAM / 64 KB flash, per-MCU configuration, flash filesystems

Focus: run on the smallest 32-bit parts (the same 16 KB RAM floor as MicroPython, but in
64 KB of flash), configure every port from the chip's resources, and put real filesystems
on external NOR and NAND flash. Nothing was removed: every reduction is a compile-time switch.

### Small targets
- **Lazy class tables** (`MCS_LAZY_CLASSES=1`, default): built-in and module classes are created
  the first time they are named; constants live in ROM (`mcs_const_t`, `MCS_CONST`,
  `mcs_register_consts`). A VM now starts in **1.7–8 KB** of heap instead of 13–25 KB
  (Cortex-M0: `m0-node` 13.0 → 1.7 KB, `m0-lowram` 19.9 → 5.0 KB, `m0-runtime` 25.1 → 7.8 KB).
- **`profiles/mcs_profile_min.h`**: a complete firmware in **64 KB of flash and 16 KB of RAM**
  (`m0-64k`: 60.9 KB, run by `make cm-check`). New switches, all default on:
  `MCS_ENABLE_STRING_EXTRA`, `MCS_ENABLE_ARRAY_EXTRA`, `MCS_ENABLE_STACK_QUEUE`,
  `MCS_ENABLE_CONVERT`, `MCS_ENABLE_DIAGNOSTICS`, `MCS_ENABLE_STDIO`, `MCS_ENABLE_MALLOC`;
  `MCS_TINY_PRINTF=1` uses the new built-in `snprintf` (`src/mcs_fmt.c`) instead of libc's.
  Flash cost of each switch: [LOW_RESOURCE.md](docs/LOW_RESOURCE.md#8a-fitting-64-kb-of-flash-profilesmcs_profile_minh).
- `examples/lowram` runs in a **16 KB** part (`m0-16k`: 12 KB pool, 11.2 KB peak); the firmware
  no longer needs `printf` and can report its C-stack high-water mark.
- `lowram` profile: 128 stack slots, 24 frames, 12 handlers, 16 roots.
- All profile headers are overridable (`#ifndef` guards), so `-D` flags win over a profile.
- `cfg.alloc_overhead` (+ `MCS_POOL_OVERHEAD`): the GC threshold and `heap_limit` count the
  allocator's per-block headers (set automatically for the built-in pool).

### Per-MCU configuration
- **`profiles/mcs_profile_auto.h`** picks min / tiny / lowram / mcu / embedded / default from
  `MCS_TARGET_RAM_KB` / `MCS_TARGET_FLASH_KB` (CMake `MICROCS_RAM_KB` / `MICROCS_FLASH_KB`),
  Zephyr `CONFIG_SRAM_SIZE` / `CONFIG_FLASH_SIZE`, or the STM32 / RP2040 / RP2350 / nRF52 /
  SAMD device macro. Board ports (`MCS_PORT_HAL=1`) keep the HAL classes on parts with
  ≥ 128 KB of flash. Default for CMake `MICROCS_PORT=stm32` and for Zephyr (new
  `MICROCS_PROFILE_*` Kconfig choice); ESP-IDF gets a menuconfig choice.
- **STM32 parts below 16 KB RAM or 64 KB flash are no longer supported**: their device macros
  (C011/C031/C051, F030x4–x8, F031, F038, F042, F048, F051, F058, F070x6, F100, F101x4–xB,
  F102, F103x4/x6, F301x6, F302x6, F303x6/x8, F328, F334, G030/G031/G041, L010x4–x8,
  L011–L063, L100/L151/L152 small variants, U031) stop the build
  (`include/profiles/mcs_target_stm32.h`; `MCS_ALLOW_SMALL_TARGET=1` overrides).
- CI compiles every STM32 family twice (default and auto profile, `PORT_CFLAGS`).

### ESP32-C2
- ESP32-C2 / ESP8684 support: embedded profile by default (`sdkconfig.defaults.esp32c2`),
  ROM nano-printf disabled (with a `#warning` if it is re-enabled with floats), LEDC clock
  from the 60 MHz PLL divider, hardware timer count clamped to the chip, adaptive VM heap in
  the example (128 KB on the C2, clamped to the largest free block), C2 pin map, 26 MHz
  crystal note. Built in CI.

### Flash filesystems (`modules/fs`)
- **`include/mcs_flash.h`** (`MCS_ENABLE_FLASH`): a NOR/NAND device description plus generic
  **SPI NOR** (JEDEC id, 3-/4-byte addressing, 4/64 KB erase, power-up unlock) and **SPI NAND**
  (W25N-style command set, on-die ECC status, bad-block markers, cached page) drivers, and
  `mcs_flash_hal_xfer` to run them over a MicroCS HAL SPI bus + GPIO chip select. ~2.6 KB.
- **LittleFS** on NOR and NAND: `mcs_lfs_flash_config()` fills an `lfs_config` for a
  partition; NAND bad blocks → `LFS_ERR_CORRUPT` (relocation), failing blocks get marked.
- **YAFFS2** backend `mcs_yaffs_ops` (`MCS_ENABLE_YAFFS`) + `mcs_yaffs_flash_dev()`: in-band
  tags on NOR and (by default) NAND, spare-area tags optional, full bad-block handling;
  optional single-threaded OS glue (`MCS_YAFFS_OSGLUE=1`). YAFFS2 is GPLv2 — not bundled.
- Tests on simulated chips with real command sets (`tests/c/flash_sim.h`): `make test`
  (drivers, 42 checks), `make lfs-test` (now also SPI NOR + SPI NAND with bad blocks),
  new `make yaffs-test` (downloads a pinned yaffs2 revision; NAND in-band + spare tags with a
  block wearing out, NOR). CI job "Flash filesystems".

### Fixes
- GC: the string intern table no longer doubles while dead strings are waiting for the next
  collection (it grows from the live count and defers growth near a collection), and it
  shrinks after a collection; this removed the collection storm on `m0-lowram`
  (now 3 collections) and made the 16 KB targets possible.
- GC: when the live data was close to ¾ of `heap_limit`, the next collection threshold
  landed at or below the bytes already allocated and the VM collected at every safepoint;
  it is now placed halfway to the limit. Allocator block headers were not counted, so a
  small pool could run out before the GC threshold was reached (`cfg.alloc_overhead`).
- Images: constant tables are allocated at their exact size when loading.
- `{0:F<huge>}` format precision could overflow an `int`; exponent formatting no longer uses
  `sprintf` (and cannot truncate).
- The compiler treated not-yet-created built-in classes as unknown names / interfaces.

### Docs
- New/updated: LOW_RESOURCE.md (16 KB / 64 KB, switch costs, auto profile), FILESYSTEM.md
  (flash layer, LittleFS vs YAFFS2), STM32 supported-parts list, ESP32-C2 section, footprint
  tables re-measured.

## 1.4.0 — hardware, ports and the REPL

Focus: run MicroCS on real boards with every peripheral, from any build system — either as a
library inside existing firmware or as a complete C# firmware with a REPL.

### Hardware API v2 (`modules/hal`)
- New classes: `Pin`, `I2cDevice`, `SpiDevice`, `DAC`, `Timer`, `I2S`, `QSPI`, `CAN` + `CanFrame`,
  `Watchdog`, `RTC`; many new members (`GPIO.OnChange/Off/PulseIn/Pin`, `UART.Open(port, baud,
  bits, parity, stop)`, `ReadLine`, `WriteLine`, `OnReceive`, `I2C.Scan/ReadRegister(s)/WriteRegister`,
  `SPI.Open(bus, hz, mode)`, `ADC.ReadMillivolts/ReadVoltage/ReadAverage`, `PWM.Servo/Tone/SetPulse/Stop`,
  `Hal.Poll/Run/Micros/DelayMicroseconds/Reset/UniqueId/CpuHz/OnEvent/Post/DroppedEvents`).
- Interrupts and timers: ISR-safe event ring (`mcs_hal_post`) or driver queue (`poll_event`);
  callbacks run in script context and adapt to their parameter count.
- Pin names (`"PA5"`, `"GPIO21"`, `"P0.13"`, `"LED"`) via `pin_lookup` / `mcs_hal_parse_pin`.
- `Encoding.UTF8`/`ASCII` and `BitConverter` helpers for packet work.
- Fixed: `GPIO.Write(pin, 0)` drove the pin high (an `int` 0 was treated as truthy).
- The simulator board gained interrupts, timers, DAC, I²S, QSPI flash, CAN and an MPU-6050-style
  register device. v1 board tables compile unchanged.

### Ports
- `ports/stm32` — every STM32Cube family (C0, F0–F7, G0, G4, H5, H7, L0–L5, U5, WB, WL), using
  CubeMX handles; compile-checked against 21 family/device combinations.
- `ports/esp32` — ESP-IDF 5.x for ESP32, S2, S3, C2, C3, C5, C6, H2, P4 (no Wi-Fi/BLE needed).
- `ports/rp2` — RP2040 and RP2350 with the pico-sdk; example firmware for Pico and Pico 2.
- `ports/zephyr` — devicetree-driven port + Zephyr module (beta).
- `ports/arduino` — any 32-bit Arduino core, Arduino library packager (beta).
- `ports/template` rewritten: every HAL member, both entry points.

### Firmware runtime and REPL
- `mcs_runtime` (`modules/runtime`): a complete firmware in one call — REPL, filesystem, boot
  scripts, scheduler, callbacks, upload protocol; or `mcs_runtime_start/step` from your own loop.
- Shell REPL: multi-line input, expression printing, Ctrl-C, Ctrl-E paste mode, Ctrl-A machine
  mode, dot commands. CLI: `mcs --repl`; `tools/mcs_remote.py repl` terminal.
- Core: `mcs_arity`, `mcs_ticks`, `mcs_sleep` (dispatches callbacks while sleeping),
  `mcs_safepoint`, `mcs_set_idle`; `Thread.Sleep` is abortable.

### Build integration
- `CMakeLists.txt` (plain CMake, pico-sdk, CubeMX CMake, ESP-IDF component), `microcs.mk`,
  `idf_component.yml`, `library.json` (PlatformIO), `zephyr/` module, `include/MicroCS.h`.
- CI: CMake host build, Pico/Pico 2 firmware, ESP-IDF builds (ESP32/S3/C3/C6), STM32 port checks.

### Docs and examples
- New README (two ways to use MicroCS, build-system matrix, ports, comparison with MicroPython and
  nanoFramework), rewritten HAL, PORTING and STANDALONE guides, a README per port.
- 16 commented hardware examples (`examples/hardware/`) run by `make test`; `examples/quickstart_embed.c`.

## 1.3.0 — small-MCU release

Focus: RAM and flash on weak microcontrollers, without changing script behaviour.
Guide: [docs/LOW_RESOURCE.md](docs/LOW_RESOURCE.md).

### Memory & speed (all measured, emulated Cortex-M unless noted)
- **`MCS_COMPACT_VALUES`** (default on for 32-bit targets): values packed to 4-byte alignment,
  12 instead of 16 bytes with double/int64 payloads. On Cortex-M0 + newlib-nano this removed a
  `memcpy` call per value copy: the demo image runs in **4.56 M instead of 18.2 M instructions**.
- **Execute-in-place images**: `mcs_exec_image_xip()` runs bytecode from the image buffer
  (flash) instead of copying it to the heap; `mcs --xip`; `MCS_ENABLE_XIP`.
- VM baseline after `mcs_new` on Cortex-M: **50 136 B → 25 688 B** (M0, full stdlib). Interned-string
  set stores keys only, a global's slot lives in its name string (no global index table),
  built-in exception classes share one field layout, hash tables start at `MCS_TABLE_MIN_CAP` (4).
- **Compact `Dictionary`/`HashSet` index**: 1-, 2- or 4-byte position slots instead of full
  key/value entries; `HashSet` stores no values array; `Remove` no longer rehashes. Host:
  700-string `HashSet` 104 → 58 KB, 100-entry `Dictionary<int,int>` 12.1 → 4.3 KB.
- Compiler RAM: smaller AST nodes and tokens (24 bytes on 32-bit), token array grows by the
  observed density instead of doubling. On-device compile + run of the demo on the M4:
  pool peak **131 528 B → 90 480 B**, compile instructions −12 %.
- Lexer keyword lookup by length + first character, cheaper primitive-type check.
- Host (`make bench`, same machine, same session as 1.2.0): run times unchanged, `mcs_new`
  43 → 30 µs, demo compile 201 → 140 µs, demo heap peak 116 438 → 87 510 B.

### New options and profiles
- `profiles/mcs_profile_lowram.h` for 32–64 KB RAM parts (images only, single-precision
  floats → 8-byte values, small VM limits, 4 KB first GC, 4-byte pool alignment).
- `MCS_ENABLE_LINQ` (LINQ operators + `Enumerable`, ~12 KB flash on M0), `MCS_POOL_ALIGN`,
  `MCS_TABLE_MIN_CAP`, `MCS_ERROR_SIZE`, `MCS_MAX_PINS`, `MCS_COMPACT_VALUES`, `MCS_ENABLE_XIP`.
- Cortex-M targets `m0-lowram` (64 KB RAM, 40 KB pool) and `m0-node` (the new
  `examples/lowram` firmware, 48 KB RAM, 32 KB pool) in `make cm-check`.
- `tools/cm_emu.py --profile N` (hottest functions) and `--callers SYMBOL`.

### Library
- LINQ operators on `Dictionary`, `HashSet`, `Stack` and `Queue` (`Where`, `Select`, `Skip`,
  `Take`, `Last`, `Average`, `GroupBy`, … — previously only lists/arrays had the full set).
- `HashSet`, `Stack` and `Queue` are accepted wherever a sequence is: `string.Join`,
  `string.Concat`, `new List<T>(…)`, `AddRange`, `Concat`, `Zip`, `SequenceEqual`, `new Stack/Queue(…)`
  (previously `string.Join(",", set)` printed the type name).
- `HashSet.IsSupersetOf`, `SetEquals`, `SymmetricExceptWith`.

### Fixes
- `base(msg)` into a lazily registered native constructor (e.g. `class E : IOException`) failed
  with "does not contain .ctor".
- `class X : IOException` was treated as an interface by the `I`+uppercase heuristic.

### Docs, examples, tests
- New: [docs/LOW_RESOURCE.md](docs/LOW_RESOURCE.md), [examples/lowram/](examples/lowram/).
- New test `t14_low_resource.cs` (exception subclasses of built-ins, globals, string churn,
  dictionary index widths, collection interop) — byte-identical to .NET 8.
- Every script test now also runs as an XIP image (46 runs); 104 C unit checks (XIP images,
  300 globals, intern churn, shared layouts, dictionary index); `make check` adds the lowram
  profile and five alternative configurations that run the whole suite.

## 1.2.0

### Language
- **Tuples**: literals `(1, "a")`, named elements `(x: 1, y: 2)`, tuple types `(int x, int y)` in
  locals, fields, parameters, `out` parameters, return types and generic arguments
  (`Dictionary<(int, int), string>`); `Item1..Item7`; structural `==`/`!=`, hashing and `ToString()`.
- Tuple element **name projection** (`var t = (a, b); t.a`) and names that survive later
  assignments, `out` parameters, tuple-typed fields/properties and object initializers.
- **Deconstruction**: `var (a, b) = t;`, `(int a, var b) = t;`, swaps `(a, b) = (b, a)` without
  allocation, `_` discards, `foreach (var (k, v) in dict)`.
- **Index from end and ranges**: `x[^1]`, `x[1..^1]`, `x[..3]`, `x[2..]` on strings, arrays and lists.
- **Patterns in `is`**: relational, `and`, `or`, `not` (`t is < 0 or > 100`); `and` also in switch
  arms and `case` labels.
- `sizeof(builtin)`, `checked(...)` / `unchecked(...)` expressions, `static` local functions with
  tuple return types; `.HasValue` / `.Value` / `.GetValueOrDefault()` on nullable values.
- Declarations used as embedded statements (`while (c) int i = 0;`) are now a compile error (as in C#).

### Library
- LINQ `Zip` (tuple and selector forms) and `Chunk`; `Stack.TryPop` / `TryPeek`,
  `Queue.TryDequeue` / `TryPeek`.
- Generated API reference `docs/STDLIB.md` (`tools/gen_stdlib_doc.py`).

### Performance
- Image format **v2** with superinstructions: `SET_LOCAL_POP`, `SET_GLOBAL_POP` and fused
  compare-and-branch `JF_EQ/NE/LT/LE/GT/GE`. v1 images still load.
- Per-site inline cache for field access (`MCS_FIELD_CACHE`), exact-arity call fast path,
  integer `/` `%` fast path.
- Token array freed right after parsing. Host: fib −20 %, loop −22 %; Cortex-M4 demo −17 %
  instructions; on-device compile peak 163 KB → 132 KB.

### Fixes
- Unbounded growth of the string intern table with many temporary strings (eventually OOM).
- Stack-slot leak when an unbraced loop/if body introduced locals (`out var`, deconstruction temps).
- Lexer: `1..3` is a range, not `1.` followed by `.3`.

### Hardening & tooling
- Image loader validates local/upvalue operands, closure captures and that every branch lands
  on an instruction boundary.
- `mcs -d file.mcsb` disassembles images (`mcs_disassemble_image`).
- `tools/fuzz.py` mutation fuzzer (source and `--image` modes), clean under ASan/UBSan;
  `make asan-test` runs the whole suite with sanitizers; GitHub Actions CI.
- `.NET 8` parity check extended to `t11`, `t13`, `examples/tour.cs`.
- New examples (`blink.cs`, `sensor_logger.cs`, `tour.cs`) run by `make test`.

## 1.1.0 — Phase 2
- Virtual filesystem (RAM, POSIX, LittleFS) with C# `File` / `Directory` / `Path`.
- HAL with C# `GPIO`, `UART`, `I2C`, `SPI`, `ADC`, `PWM` + simulator board.
- Job scheduler, standalone shell with UART upload protocol, execution limits.
- `ref` / `out` / `in`, `case` patterns with `when`.
- Cortex-M0/M4/M33 reference port verified in an emulator. Memory fixes in the compiler.

## 1.0.0 — Phase 1
- Lexer, parser, single-pass compiler, stack VM with computed goto, precise GC, stdlib,
  bytecode images, CLI/REPL.
