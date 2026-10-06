# Performance and footprint

> [!IMPORTANT]
> Every number on this page was produced in this repository's sandbox with the command next
> to it. Nothing here was measured on physical hardware; Cortex-M figures are *instruction
> counts* in an emulator, not cycles.

## Host interpreter — `make bench`

x86-64 Xeon @ 2.9 GHz, gcc 11.5 `-O2`, best of 5. "Compile" = source → image without running.
Host timings vary by ±10 % between runs on this shared machine.

| Script | Source | Image | VM new | Compile | Run (from source) | Run (image) |
|---|---:|---:|---:|---:|---:|---:|
| `bench/fib.cs` (fib 30) | 174 B | 318 B | 30–47 µs | 12–17 µs | 80–94 ms | **74 ms** |
| `bench/loop.cs` (10 M iterations) | 182 B | 277 B | 30 µs | 8–10 µs | 332 ms | **330 ms** |
| `bench/objects.cs` (1 M objects) | 462 B | 642 B | 30 µs | 27 µs | 238 ms | **232 ms** |
| `ports/cortex-m/demo.cs` | 2533 B | 2763 B | 30 µs | 137 µs | 0.45 ms | **0.34 ms** |

CPython 3.13 on the same machine (`bench/*.py`): fib **109 ms**, loop **931 ms**, objects **386 ms**.

### What changed in 1.2

| | 1.1 (Phase 2) | 1.2 | Change |
|---|---:|---:|---:|
| fib(30) | 93 ms | 74 ms | −20 % |
| 10 M loop | 424 ms | 330 ms | −22 % |
| 1 M objects | 240 ms | 232 ms | −3 % |
| demo on Cortex-M4, image | 3.96 M instr | 3.29 M instr | −17 % |
| demo on Cortex-M4, from source | 5.79 M instr | 4.92 M instr | −15 % |
| demo compile, host heap peak | 200 KB | 168 KB | −16 % |
| demo compile, M4 pool peak | 162.9 KB (limit-bound) | 131.5 KB | −19 % |

Where it came from:
* **Superinstructions** (image v2): `SET_LOCAL_POP` / `SET_GLOBAL_POP` for statement stores and
  fused compare-and-branch `JF_EQ … JF_GE` for `if`/`while`/`for`/`?:` conditions — fewer
  dispatches and no boolean push/pop on the hot path.
* **Inline field cache** (`MCS_FIELD_CACHE`): each `GET_FIELD`/`SET_FIELD` site remembers
  `(class, slot)`; a hit skips the hash lookup.
* **Call fast path** for closures called with exact arity (no `params`, no defaults).
* **Integer `/` and `%` fast path** for positive divisors.
* **Token array freed after parsing** and allocated on the VM heap instead of being kept
  until compilation ends.

## Cortex-M — `make cm-check`

arm-none-eabi-gcc 13.2.1, `-Os`, newlib-nano, executed in the Unicorn emulator.

| | m0-runtime | m4-full | m33-full |
|---|---:|---:|---:|
| Flash `.text` (whole firmware) | 172 328 B | 216 272 B | 216 192 B |
| VM creation (`mcs_new` + modules) | 582 923 instr | 218 675 instr | 218 675 instr |
| Heap after `mcs_new` (full stdlib) | 50 136 B | 50 352 B | 50 352 B |
| demo as image: instructions | 18.2 M | 3.29 M | 3.29 M |
| demo as image: pool in use after run | 75 248 B | 108 824 B\* | 108 824 B\* |
| demo from source (compile + run) | n/a | 4.92 M | 4.92 M |
| pool peak (whole run) | 86 096 B of 102 400 B | 131 528 B of 163 840 B | 131 528 B of 262 144 B |
| C stack peak (painted) | 2.5 KB | 3.5 KB | 3.5 KB |

\* the GC grows its threshold with the available heap, so bigger pools show higher
in-use figures; the M0 run proves the same demo completes in an 86 KB peak.
The M0 needs ~5.5× the instructions of the M4 for the same work — it has no hardware
divider, no `IT` blocks and only 16-bit Thumb-1 encodings for most operations.

### Flash by component — `python3 tools/map_sizes.py build/cm/m33-full.map`

<p align="center"><img src="../assets/footprint.svg" alt="Flash by component" width="720"></p>

| Component | Bytes |
|---|---:|
| MicroCS stdlib | 64 617 |
| MicroCS VM core (VM, GC, objects, image loader) | 53 750 |
| MicroCS compiler (lexer, parser, codegen) | 42 541 |
| newlib libm | 25 900 |
| newlib libc (printf incl. float, strtod) | 24 310 |
| module fs (VFS, RAM FS, File API) | 9 363 |
| libgcc (soft double, division) | 7 974 |
| port (`main.c` incl. the demo image) | 6 760 |
| module hal (+ simulator) | 6 183 |
| module scheduler | 1 504 |

Linker input sections after `--gc-sections`; mergeable string literals are counted before
merging, so the total is a little above `size`'s `.text`.

### Profiles
Core only (`src/*.c`, no modules, no libc), Cortex-M0 `-Os -ffunction-sections`, sum of
object `.text` before linking:

| Profile | `tiny` | `mcu` | `embedded` |
|---|---:|---:|---:|
| Code | 85.9 KB | 103.4 KB | 147.2 KB |

```sh
for f in src/*.c; do arm-none-eabi-gcc -std=gnu99 -Os -mcpu=cortex-m0 -mthumb -Iinclude \
  -ffunction-sections -fdata-sections -DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_mcu.h"' \
  -c $f -o /tmp/ps_$(basename $f).o; done; arm-none-eabi-size /tmp/ps_*.o
```

## Memory notes

* **Fixed in 1.2 — intern table growth**: scripts that create many short-lived strings made
  the weak string-intern table fill with tombstones and double forever until the heap limit
  was hit (`OutOfMemory` after a few hundred thousand concatenations in a 128 KB heap).
  The table now rehashes in place when most used slots are tombstones and shrinks after a
  collection. Regression test: `tests/t12_memory_churn.cs` (`--heap 131072`).
* **Fixed in 1.1**: an out-of-memory panic during compilation leaked the AST arena;
  compiler states were not recycled; the token array was over-allocated.
* **Open**: stdlib metadata (~60 builtin classes with method tables and interned names)
  costs ~40–50 KB of heap per VM. Moving it to ROM tables is the main lever for smaller
  targets (planned).
* **Open**: compiling needs the whole AST of a script in RAM (the M4 run that also compiles
  the demo on the device peaks at 131.5 KB of pool; the image-only M0 run at 86 KB). Use
  precompiled images on small parts; streaming per-declaration compilation is planned.

## Feasibility of a "tiny" target (2 KB SRAM / 16 KB flash)
Not achievable with this VM: the smallest build is ~86 KB of code and the object model
needs tens of KB of heap. A tiny profile would need a separate design (static typing at
compile time, no GC, no reflection-style metadata, ahead-of-time images only). It is
listed as research, not as a supported target.
