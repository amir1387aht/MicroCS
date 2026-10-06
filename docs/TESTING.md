# Testing and verification

MicroCS treats tests as the specification: a `.out` file defines behaviour, and every
number or "supported" claim in the docs points at a test or a measurement.

## Test matrix

| Command | What it runs | Needs |
|---|---|---|
| `make test` | 52 script runs (16 programs as **source**, as a copied **bytecode image** and as an image **executed in place**, plus 4 diagnostics tests), 142 C unit checks (incl. `test_runtime`), 42 flash-driver checks (`test_flash`: SPI NOR/NAND drivers on simulated chips), 16 shell-protocol checks, the examples (`tour.cs` byte-exact, every `examples/hardware/*.cs` script on the simulator, `examples/quickstart_embed.c`, `examples/lowram` built with the lowram profile) | C compiler, python3 |
| `make check` | `make test` + GC-stress build (collect at every safepoint) running every `t*.cs` + 29 feature-flag / profile builds with `-Werror` (incl. the `min` and `auto` profiles) + the whole script suite under 8 alternative configurations (`ALT_CONFIGS`: compact values, no XIP, big tables, no computed goto / field cache, tiny GC threshold + 16-byte pool alignment, eager classes, eager registrations, built-in tiny printf) | same |
| `make lfs-test` | LittleFS backend on a RAM block device and on simulated SPI NOR / SPI NAND chips (bad blocks): mount, remount, ENOSPC, atomic upload, churn | curl (downloads littlefs 2.9.3) |
| `make yaffs-test` | YAFFS2 backend on simulated SPI NAND (in-band and spare tags, a block wearing out) and SPI NOR | curl (downloads a pinned yaffs2 revision, GPLv2 - test binary only) |
| `make cm-check` | builds 8 Cortex-M firmwares and executes 7 of them instruction by instruction (`m0-runtime`, `m0-lowram`, `m0-node`, `m0-16k` = 16 KB RAM, `m0-64k` = min profile in 64 KB flash / 16 KB RAM, `m4-full`, `m33-full`; output must equal the host byte-for-byte), 7 UART protocol checks on the M33 shell build | `arm-none-eabi-gcc`, `pip3 install unicorn` |
| `tools/check_ports.sh stm32 <family> <device> <cpu>` | compiles the STM32 port and its example with `-Werror` against the official STM32Cube HAL headers (downloaded into `build/sdk`) | `arm-none-eabi-gcc`, git |
| CI `pico` job | builds `ports/rp2/example` for `pico` (RP2040) and `pico2` (RP2350) with the pico-sdk | — |
| CI `esp-idf` job | builds `ports/esp32/example` for ESP32, ESP32-S3, ESP32-C3, ESP32-C6 in the `espressif/idf:v5.3.2` container | — |
| CI `cmake` job | configures and builds the library + CLI with CMake and runs a script | cmake |
| `tools/verify_dotnet.sh` | compiles `t02 t05 t10 t11 t13 t14` and `examples/tour.cs` with **.NET 8** and diffs the output against MicroCS's `.out` | `dotnet` 8+ |
| `make asan-test` | the whole suite under AddressSanitizer + UBSan | gcc/clang with sanitizers |
| `python3 tools/fuzz.py` | mutation fuzzer (source or images), see below | a sanitizer build |
| `make bench` | startup / compile / image load / run timings | — |

## Script tests (`tests/`)

| File | Covers |
|---|---|
| `t01_basics` | literals, operators, strings, formatting, control flow |
| `t02_oop` | classes, interfaces, inheritance, properties, operators, generics *(.NET-identical)* |
| `t03_collections` | arrays, `List`, `Dictionary`, `HashSet`, `Stack`, `Queue`, LINQ |
| `t04_exceptions` | try/catch/finally, filters, custom exceptions, stack overflow |
| `t05_functional` | lambdas, closures, delegates, events, local functions *(.NET-identical)* |
| `t06_gc_stress` | allocation churn in a small heap |
| `t07_filesystem` | `File`, `Directory`, `Path` on a RAM filesystem |
| `t08_hal` | GPIO/UART/I2C/SPI/ADC/PWM against the simulator (HAL v1 API, kept for compatibility) |
| `t09_scheduler` | periodic / one-shot / file jobs |
| `t10_lang_phase2` | `ref`/`out`/`in`, `case` patterns *(.NET-identical)* |
| `t11_tuples_ranges` | tuples, deconstruction, `^` and ranges, `sizeof`, nullable members *(.NET-identical)* |
| `t12_memory_churn` | 300 k temporary strings in a 128 KB heap (intern-table regression) |
| `t13_stdlib_more` | `Zip`, `Chunk`, `TryPop`/`TryDequeue`, `is` patterns, tuple names *(.NET-identical)* |
| `t14_low_resource` | exceptions derived from built-ins (`: IOException` with fields and `base(msg)`), many globals, string churn, dictionary index widths and removal order, LINQ / sequence arguments on `HashSet` `Stack` `Queue` `Dictionary` *(.NET-identical)* |
| `t15_hal_v2` | HAL v2: `Pin`, pin names, interrupts (1- and 2-argument handlers), timers, user events, UART frames/`ReadLine`/`OnReceive`, `I2cDevice` registers, `SpiDevice`, ADC/DAC, PWM servo/tone, I²S, QSPI flash, CAN frames, watchdog, RTC, `Hal.*` |
| `t16_bytes` | `Encoding.UTF8`/`ASCII`, `BitConverter` |
| `err_*` | compile errors, runtime errors, member errors, limits — exact diagnostics |

A test can pass CLI options on its first line: `// args: --heap 131072`.
`tests/run_tests.sh` runs each file from source, then compiles it to `.mcsb` and runs the
image twice — copied (`mcs_exec_image`) and executed in place (`mcs --xip`,
`mcs_exec_image_xip`); all three outputs must match the `.out` file.

## Instruction-level profiling (Cortex-M)

`tools/cm_emu.py ELF --cpu m0 --profile 20` prints the 20 functions that executed the most
instructions; `--callers memcpy` lists who calls a symbol. Use it after `make cm` to find
hot spots that only exist on a given core (the 1.3 `memcpy` value-copy finding on Cortex-M0
came from this).

## Fuzzing

```sh
make asan                                   # or build build/mcs_asan by hand
python3 tools/fuzz.py ./mcs 3000 1          # source mode: [binary] [iterations] [seed]
python3 tools/fuzz.py ./mcs 3000 1 --image  # image mode: mutate compiled .mcsb files
```

* **Source mode** mutates the test corpus (token insertion, deletion, line duplication,
  splicing between files) and runs each variant with a step limit, time limit, 256 KB heap,
  RAM filesystem and simulator board.
* **Image mode** compiles every test to an image, flips/inserts/deletes bytes and runs
  `mcs -d` on the result — exercising the loader's validation and the disassembler.
* A finding is any signal, sanitizer report, `internal error` or hang; inputs are saved as
  `build/fuzz/crash_<seed>_<iter>.*` for reproduction.

Bugs found by the fuzzer and fixed in 1.2: unbounded intern-table growth with temporary
strings (OOM), and a stack-slot leak from declarations used as loop bodies
(`while (c) int i = 0;` — now a compile error, as in C#). Image mode found that running
mutated images can crash (stack balance is not verified — documented in SECURITY.md), which
is why it loads and disassembles instead of executing. Final 1.2 runs under ASan/UBSan:
2 500 source + 1 500 image iterations, no findings. 1.3 runs under ASan/UBSan: 1 500 source
(seed 7) + 1 500 image (seed 8) iterations, no findings.

## Adding a test

1. Write `tests/tNN_topic.cs`, run `./mcs tests/tNN_topic.cs > tests/tNN_topic.out` and
   **read the output** — it becomes the specification.
2. If it is pure C#, run `tools/verify_dotnet.sh tests/tNN_topic.cs`; when it prints `SAME`,
   add it to the default list in the script.
3. `make check` must stay green.
