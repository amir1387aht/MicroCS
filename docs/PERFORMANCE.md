# Performance and footprint

> [!IMPORTANT]
> Unless marked **real hardware**, every number on this page was produced in this
> repository's sandbox with the command next to it; Cortex-M figures there are *instruction
> counts* in an emulator, not cycles. Real-hardware results:
> [ESP32-S3](#real-hardware--esp32-s3).

## Bytecode images

A precompiled image (`mcs -c`) is the fast path — it is optimized, the on-device compiler's
output is not:

* **Optimizer** (`src/mcs_opt.c`, run by `mcs -c` / `mcs_compile_image`): fuses common
  sequences into **superinstructions** — local ± immediate (`LI_ADD L1 - 1`), local ⊕ local /
  constant with optional store (`BIN_LLS`, `BIN_LKS`), compare-and-branch on locals and
  immediates (`JFLI_LT`, `JFLL_LT`, `JF_LK`), accumulate (`ACC_ADD x += …`), `a[i]` / `a[i] = v`
  on locals, `obj.f` on a local, `return local`, constant folding of `(double)1`. Then jump
  threading, loop rotation (the loop test moves to the bottom: one branch per iteration),
  dead-code removal and relayout. Full list: [BYTECODE.md](BYTECODE.md#superinstructions-v2).
* **VM**: stack pointer kept in a register, int fast paths that work on the stack in place,
  `array.Length` / `list.Count` / `string.Length` without a method lookup, a **method inline
  cache** for `INVOKE` (per call site, invalidated by a class epoch), a constructor cache for
  `new C()`, a direct closure-call path, cheaper returns.
* **Compact image format v3**: varints, delta-coded line tables, every string stored once
  (names, file names and constants are shared). Images are smaller than the source for all
  but tiny scripts.

`mcs -O0 -c` writes an unoptimized image (for VMs built with `MCS_ENABLE_SUPEROPS=0`); source
run on the device is not optimized unless `MCS_OPTIMIZE_SOURCE=1` (costs compile RAM/time).

### Images vs source on Cortex-M — `make mcu-bench`

`tools/mcu_bench.sh` runs `bench/mcu/*.cs` in one firmware as optimized image, `-O0` image and
from source (M4F: full build; M0: runtime only, no compiler) and checks every output against
the host. Emulated instructions for the whole run (load + execute; source = compile + run).
RAM peak = pool high-water mark.

| Cortex-M4F | Source | Image | From source | Image `-O0` | **Image** | Peak src / image |
|---|---:|---:|---:|---:|---:|---:|
| `fib` | 102 B | 146 B | 2.07 M | 2.00 M | **1.43 M** | 31.4 / 15.1 KB |
| `loop` | 232 B | 166 B | 13.16 M | 13.07 M | **6.70 M** | 31.4 / 15.1 KB |
| `objects` | 406 B | 356 B | 3.64 M | 3.50 M | **3.19 M** | 72.2 / 72.2 KB |
| `sensor` | 581 B | 501 B | 0.95 M | 0.71 M | **0.68 M** | 40.7 / 17.0 KB |
| `strings` | 309 B | 311 B | 0.63 M | 0.50 M | **0.49 M** | 31.7 / 30.6 KB |

| Cortex-M0 (runtime only) | Image `-O0` | **Image** |
|---|---:|---:|
| `fib` | 2.60 M | **1.95 M** |
| `loop` | 20.74 M | **13.23 M** |
| `objects` | 4.71 M | **4.37 M** |
| `sensor` | 1.23 M | **1.21 M** |
| `strings` | 0.78 M | **0.77 M** |

Where the time goes now: `sensor` is dominated by soft-float `double` arithmetic
(`MCS_FLOAT_DOUBLE=1`; the M4F FPU is single precision — `MCS_FLOAT_DOUBLE=0` uses it),
`strings` by string allocation and the GC, `objects` by allocation; script-level dispatch is
no longer the bottleneck for those. `fib` is call/return-bound (~170 instructions per call).

### Flash cost

The superinstruction handlers and caches cost about **8–11 KB** of Thumb code.
`profiles/mcs_profile_min.h`
sets `MCS_ENABLE_SUPEROPS=0` and `MCS_FIELD_CACHE=0` so the 64 KB-flash build keeps its margin
(60.6 KB with this toolchain; Ubuntu's newlib adds ~0.9 KB); such VMs run
`-O0` images and reject optimized ones with a clear error. `MCS_ENABLE_SUPEROPS=0` saves the
same on any build; `MCS_FIELD_CACHE=0` saves another ~1 KB.

## MicroCS vs MicroPython vs .NET nanoFramework

The `bench/mcu/` scripts (and the three host scripts) ported idiomatically to Python, to
nanoFramework C# and to plain C, same output everywhere. Sources, build scripts and exact
configurations: [`bench/compare/`](../bench/compare). MicroPython **1.26.0**, nanoFramework
**nanoCLR 1.1.311** with `mscorlib` 1.17.12, MicroCS **1.6.0**, GCC 13.2.1 (C, `-Os` on
Cortex-M); emulator and PC numbers measured in one session on the same machine.

### Real hardware — ESP32-S3

`fib(18)` on an ESP32-S3 board (Xtensa LX7, 160 MHz, ESP-IDF 5.5 for MicroCS), timed inside
the script, same board for both:

| | MicroCS 1.6 | MicroPython | |
|---|---:|---:|---:|
| `fib(18)` | **30 ms** | 98 ms | 3.3× |

The ratio matches the emulated Cortex-M4F one for `fib` (3.7×). Measured by the project
author; more workloads on silicon are welcome — see [`bench/compare/`](../bench/compare).

### Cortex-M (emulated) — MicroCS vs MicroPython

MicroPython built from `ports/embed` with the same toolchain (GCC 13.2.1, `-Os`), CPU flags,
linker script and board code as the MicroCS bench firmware, `EXTRA_FEATURES` ROM level (as on
stm32/rp2), computed goto, map lookup cache, `double` floats (MicroCS also uses
`MCS_FLOAT_DOUBLE=1`). Emulated instructions for a cold run: interpreter init + load + run.
MicroCS optimized image vs MicroPython `.mpy` (mpy-cross), and both compiling from source on
the device.

| Cortex-M4F | MicroCS image | MicroPython `.mpy` | ratio | MicroCS source | MicroPython source | ratio |
|---|---:|---:|---:|---:|---:|---:|
| `fib` | **1.43 M** | 5.23 M | 3.7× | **2.07 M** | 5.36 M | 2.6× |
| `loop` | **6.70 M** | 23.71 M | 3.5× | **13.16 M** | 23.94 M | 1.8× |
| `objects` | **3.19 M** | 7.45 M | 2.3× | **3.64 M** | 7.96 M | 2.2× |
| `sensor` | **0.68 M** | 2.15 M | 3.2× | **0.95 M** | 2.80 M | 2.9× |
| `strings` | **0.49 M** | 1.79 M | 3.7× | **0.63 M** | 2.29 M | 3.6× |

| Cortex-M0 | MicroCS image | MicroPython `.mpy` | ratio |
|---|---:|---:|---:|
| `fib` | **1.95 M** | 6.99 M | 3.6× |
| `loop` | **13.23 M** | 38.56 M | 2.9× |
| `objects` | **4.37 M** | 11.94 M | 2.7× |
| `sensor` | **1.21 M** | 3.16 M | 2.6× |
| `strings` | **0.77 M** | 2.46 M | 3.2× |

**RAM** — smallest heap each script still runs in (binary search on the output, M4F,
precompiled code; MicroCS: `BENCH_CFLAGS="-DBENCH_MINHEAP -DBENCH_MODES=1"`, pool with the
`mcu` profile's 256-slot value stack):

| | `fib` | `loop` | `objects` | `sensor` | `strings` |
|---|---:|---:|---:|---:|---:|
| MicroCS pool | 7.6 KB | 7.6 KB | 41.7 KB | 9.5 KB | 25.1 KB |
| MicroPython GC heap | **0.7 KB** | **0.7 KB** | **21.3 KB** | **2.9 KB** | **17.3 KB** |

MicroPython needs less heap here. Part of it is accounting: about 5.7 KB of the MicroCS pool
is the VM itself (1.6 KB state, 256-slot value stack, 64 call frames), which MicroPython keeps
in static RAM and on the C stack (not counted). The rest is real: MicroCS's per-object and
collection overhead and the headroom its GC needs. Flash is in the same class: the M4F bench firmwares (VM, compiler,
stdlib, newlib) are 170 KB (MicroCS, incl. the fs/hal modules) and 166 KB (MicroPython).

### Bare-metal C — the ceiling

What an interpreter costs: [`c/cbench.c`](../bench/compare/c/cbench.c) does the same work in
C (heap objects in a growable array for `objects`, `snprintf` for the output) on the same
emulated boards, flags (`-Os`) and newlib. C has no load step; MicroCS and MicroPython counts
also include interpreter init + image/`.mpy` load.

| Cortex-M4F | C | MicroCS image | × C | MicroPython `.mpy` | × C |
|---|---:|---:|---:|---:|---:|
| `fib` | 0.059 M | 1.43 M | 24× | 5.23 M | 88× |
| `loop` | 0.51 M | 6.70 M | 13× | 23.71 M | 46× |
| `objects` | 0.30 M | 3.19 M | 11× | 7.45 M | 25× |
| `sensor` | 0.25 M | 0.68 M | 2.7× | 2.15 M | 8.5× |
| `strings` | 0.13 M | 0.49 M | 3.8× | 1.79 M | 14× |

| Cortex-M0 | C | MicroCS image | × C | MicroPython `.mpy` | × C |
|---|---:|---:|---:|---:|---:|
| `fib` | 0.059 M | 1.95 M | 33× | 6.99 M | 118× |
| `loop` | 5.07 M | 13.23 M | 2.6× | 38.56 M | 7.6× |
| `objects` | 0.46 M | 4.37 M | 9.5× | 11.94 M | 26× |
| `sensor` | 0.57 M | 1.21 M | 2.1× | 3.16 M | 5.5× |
| `strings` | 0.20 M | 0.77 M | 3.8× | 2.46 M | 12× |

Pure call/arithmetic code is where C is far ahead (GCC turns part of `fib`'s recursion into a
loop; ~7 instructions per call vs ~170 in the VM). Work that ends in library code —
soft-float `double`, `snprintf`, division on the M0 (no hardware divider, `loop` is
dominated by `__aeabi_idivmod` in C too) — narrows the gap to 2–4×.

### PC — all four

x86-64, same machine. C `gcc -O2`, MicroCS `make bench` (optimized image, `gcc -O2`),
MicroPython unix port (standard build), nanoFramework on the nanoCLR virtual device (the
native x64 nanoCLR the `nanoclr` tool ships). Best of 3–50 runs.

| | C | MicroCS | MicroPython | nanoFramework |
|---|---:|---:|---:|---:|
| `fib(30)` | 1.3 ms | **41 ms** | 254 ms | 717 ms |
| loop 10 M | 12 ms | **95 ms** | 780 ms | 1485 ms |
| objects 1 M | 32 ms | **132 ms** | 548 ms | 4272 ms |
| `bench/mcu` fib | 4 µs | **128 µs** | 760 µs | 2251 µs |
| `bench/mcu` loop | 71 µs | **493 µs** | 3042 µs | 9523 µs |
| `bench/mcu` objects | 33 µs | **253 µs** | 974 µs | 8566 µs |
| `bench/mcu` sensor | 1 µs | **35 µs** | 194 µs | 407 µs |
| `bench/mcu` strings | 11 µs | **43 µs** | 265 µs | 2080 µs |

Caveats: apart from the ESP32-S3 `fib(18)` above, none of this ran on silicon; instruction counts ignore flash wait states and memory
speed (identical for both firmwares). nanoFramework was not measured on a Cortex-M — it has
no bare-metal build for this emulator — so its MCU speed and footprint are not compared; its
PC numbers come from a different native build than its firmware, and `objects` uses
`ArrayList` (nanoFramework's `mscorlib` has no `List<T>`). MicroPython `-O2` instead of the
default unix build is 4–16 % faster on the PC.

## Host interpreter — `make bench`

x86-64 Xeon @ 2.9 GHz, gcc 11.5 `-O2`, best of 5. "Compile" = source → image without running.
Host timings vary by ±10 % between runs on this shared machine and more between days;
compare numbers only within one session.

| Script | Source | Image | Compile | Run (from source) | **Run (image)** |
|---|---:|---:|---:|---:|---:|
| `bench/fib.cs` (fib 30) | 174 B | 226 B | 8 µs | 50 ms | **41 ms** |
| `bench/loop.cs` (10 M iterations) | 182 B | 220 B | 7 µs | 180 ms | **95 ms** |
| `bench/objects.cs` (1 M objects) | 462 B | 440 B | 18 µs | 141 ms | **132 ms** |
| `ports/cortex-m/demo.cs` | 2533 B | 1942 B | 137 µs | 0.33 ms | **0.22 ms** |

Same session: CPython 3.13 (`bench/*.py`, in-process) fib **97 ms**, loop **895 ms**,
objects **356 ms**; MicroPython 1.26 unix port 254 / 780 / 548 ms
([comparison](#microcs-vs-micropython-vs-net-nanoframework)).

## Cortex-M — `make cm-check`

arm-none-eabi-gcc 13.2.1, `-Os`, newlib-nano, executed in the Unicorn emulator. 1 KB = 1024 B.

| | m0-runtime | m0-lowram | m0-node | m4-full | m33-full |
|---|---:|---:|---:|---:|---:|
| Config | default, no compiler | lowram profile | lowram, no modules, 2 lib groups | default | default |
| Flash `.text` (whole firmware) | 171 608 B | 162 272 B | 147 536 B | 218 608 B | 218 504 B |
| VM creation (`mcs_new` + modules) | 161 859 instr | 156 983 instr | — | 131 122 instr | 131 100 instr |
| Heap after `mcs_new` | 25 688 B | 20 332 B | 13 296 B | 25 904 B | 25 904 B |
| Workload | demo image | demo image | `examples/lowram` | demo image + source | demo image + source |
| Image run: instructions | 4.56 M | 4.59 M | 1.28 M (whole firmware) | 3.47 M | 3.47 M |
| Source (compile + run): instructions | n/a | n/a | n/a | 4.32 M | 4.32 M |
| Pool peak (whole run) | 64 984 of 102 400 B | 34 076 of 40 960 B | 28 044 of 32 768 B | 90 480 of 163 840 B | 90 480 of 262 144 B |
| C stack peak (painted) | 2.2 KB | 2.1 KB | — | 4.1 KB | 4.1 KB |

`m33-shell` (the UART script-manager firmware) is 221 688 B. The M0 needs ~1.3× the
instructions of the M4 for the demo — it has no hardware divider, no `IT` blocks and mostly
16-bit Thumb-1 encodings. Per-function profiles: `python3 tools/cm_emu.py
build/cm/m0-runtime.elf --cpu m0 --profile 20`.

### Flash by component — `python3 tools/map_sizes.py build/cm/m33-full.map`

<p align="center"><img src="../assets/footprint.svg" alt="Flash by component" width="720"></p>

| Component | Bytes |
|---|---:|
| MicroCS stdlib | 65 500 |
| MicroCS VM core (VM, GC, objects, image loader) | 53 822 |
| MicroCS compiler (lexer, parser, codegen) | 44 656 |
| newlib libm | 25 900 |
| newlib libc (printf incl. float, strtod) | 24 310 |
| module fs (VFS, RAM FS, File API) | 9 071 |
| libgcc (soft double, division) | 7 974 |
| port (`main.c` incl. the demo image) | 6 760 |
| module hal (+ simulator) | 5 925 |
| module scheduler | 1 428 |

Linker input sections after `--gc-sections`; mergeable string literals are counted before
merging, so the total is a little above `size`'s `.text`.

### Profiles
MicroCS objects only (all modules, no libc), Cortex-M0 `-Os`, before `--gc-sections`:

| Build | default | `embedded` | `mcu` | `lowram` | `lowram` + `MCS_ENABLE_LINQ=0` | `tiny` |
|---|---:|---:|---:|---:|---:|---:|
| Code | 171.1 KB | 169.1 KB | 120.7 KB | 111.3 KB | 99.5 KB | 84.3 KB |

What each profile turns off and the per-option savings: [LOW_RESOURCE.md](LOW_RESOURCE.md#8-flash).

## Memory notes

* **String churn**: the weak string-intern table rehashes in place when most used slots are
  tombstones and shrinks after a collection, so scripts that create many short-lived strings
  run in a bounded heap. Regression test: `tests/t12_memory_churn.cs` (`--heap 131072`).
* **VM baseline**: stdlib metadata takes ~25 KB of heap per VM (13 KB with only
  `MCS_LIB_CORE | MCS_LIB_COLLECTIONS`). Moving the remaining method tables and names to ROM
  is the main lever left (planned).
* **Open**: compiling needs the whole AST of a script in RAM (the M4 run that also compiles
  the demo on the device peaks at 88.4 KB of pool; image-only runs fit in 33 KB). Use
  precompiled images — ideally with `mcs_exec_image_xip()` — on small parts; streaming
  per-declaration compilation is planned.

## Feasibility of a "tiny" target (2 KB SRAM / 16 KB flash)
Not achievable with this VM: the smallest build is ~84 KB of code and the smallest
measured working configuration (`examples/lowram`) needs a 24–32 KB heap. A tiny profile would need a separate design (static typing at
compile time, no GC, no reflection-style metadata, ahead-of-time images only). It is
listed as research, not as a supported target.
