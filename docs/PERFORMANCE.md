# Performance and footprint

> [!IMPORTANT]
> Every number on this page was produced in this repository's sandbox with the command next
> to it. Nothing here was measured on physical hardware; Cortex-M figures are *instruction
> counts* in an emulator, not cycles.

## Host interpreter — `make bench`

x86-64 Xeon @ 2.9 GHz, gcc 11.5 `-O2`, best of 5. "Compile" = source → image without running.
Host timings vary by ±10 % between runs on this shared machine — and by much more between
days (the same binary ran fib in 74 ms and in 120 ms on different days). Compare numbers
only within one session. The table below is from the 1.2.0 release; the 1.3.0 comparison
is in [What changed in 1.3](#what-changed-in-13).

| Script | Source | Image | VM new | Compile | Run (from source) | Run (image) |
|---|---:|---:|---:|---:|---:|---:|
| `bench/fib.cs` (fib 30) | 174 B | 318 B | 30–47 µs | 12–17 µs | 80–94 ms | **74 ms** |
| `bench/loop.cs` (10 M iterations) | 182 B | 277 B | 30 µs | 8–10 µs | 332 ms | **330 ms** |
| `bench/objects.cs` (1 M objects) | 462 B | 642 B | 30 µs | 27 µs | 238 ms | **232 ms** |
| `ports/cortex-m/demo.cs` | 2533 B | 2763 B | 30 µs | 137 µs | 0.45 ms | **0.34 ms** |

CPython 3.13 on the same machine (`bench/*.py`): fib **109 ms**, loop **931 ms**, objects **386 ms**.

### What changed in 1.3

1.3 is about RAM and flash on small parts; run speed on the host is unchanged. Same machine,
same session, 1.2.0 vs 1.3.0 builds (host) and `make cm-check` (Cortex-M, instructions):

| | 1.2.0 | 1.3.0 | Change |
|---|---:|---:|---:|
| fib(30) / 10 M loop / 1 M objects, host | 89 / 385 / 311 ms | 89 / 385 / 311 ms | ±0 (noise) |
| `mcs_new`, host | 43 µs | 30 µs | −30 % |
| demo compile, host | 201 µs | 140 µs | −30 % |
| demo heap peak, host | 116 438 B | 87 510 B | −25 % |
| heap after `mcs_new`, Cortex-M0 | 50 136 B | 25 688 B | −49 % |
| demo image on Cortex-M0 | 18.2 M instr | 4.56 M instr | −75 % |
| demo image on Cortex-M0, pool peak | 86 096 B | 64 984 B | −25 % |
| demo image on Cortex-M4 | 3.29 M instr | 3.47 M instr | +5 % |
| demo compile + run on Cortex-M4, pool peak | 131 528 B | 90 480 B | −31 % |
| demo from source on Cortex-M4 | 4.92 M instr | 4.32 M instr | −12 % |
| 700-string `HashSet`, host | 104.3 KB | 58.3 KB | −44 % |
| 100-entry `Dictionary<int,int>`, host | 12.1 KB | 4.3 KB | −64 % |

Where it came from (details in [LOW_RESOURCE.md](LOW_RESOURCE.md)):
* **`MCS_COMPACT_VALUES`** — 12-byte values with 4-byte alignment on 32-bit targets. On
  the M0 + newlib-nano a 16-byte 8-aligned value copy was a `memcpy` call; now it is three
  word moves. On the M4 the packed `double` loads cost ~2 % instructions; the rest of the +5 % is
  spread over the RAM-saving changes below and was not broken down further. Turn it off with
  `-DMCS_COMPACT_VALUES=0` if speed matters more than RAM.
* **Smaller VM baseline** — keys-only intern set, global slots stored in the name string,
  one shared field layout for all built-in exceptions, hash tables starting at 4 entries.
* **Compact `Dictionary`/`HashSet` index** — 1/2/4-byte position slots; `HashSet` has no
  values array.
* **Compiler RAM** — 24-byte tokens on 32-bit, smaller AST nodes, token array sized by the
  observed token density.

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
16-bit Thumb-1 encodings. (In 1.2 it was 5.5×; most of that was the `memcpy` per value copy
that `MCS_COMPACT_VALUES` removed.) Per-function profiles: `python3 tools/cm_emu.py
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

* **Fixed in 1.2 — intern table growth**: scripts that create many short-lived strings made
  the weak string-intern table fill with tombstones and double forever until the heap limit
  was hit (`OutOfMemory` after a few hundred thousand concatenations in a 128 KB heap).
  The table now rehashes in place when most used slots are tombstones and shrinks after a
  collection. Regression test: `tests/t12_memory_churn.cs` (`--heap 131072`).
* **Fixed in 1.1**: an out-of-memory panic during compilation leaked the AST arena;
  compiler states were not recycled; the token array was over-allocated.
* **Fixed in 1.3 — VM baseline**: stdlib metadata dropped from ~50 KB to ~25 KB of heap per
  VM (13 KB with only `MCS_LIB_CORE | MCS_LIB_COLLECTIONS`). Moving the remaining method
  tables and names to ROM is still the main lever left (planned).
* **Open**: compiling needs the whole AST of a script in RAM (the M4 run that also compiles
  the demo on the device peaks at 88.4 KB of pool; image-only runs fit in 33 KB). Use
  precompiled images — ideally with `mcs_exec_image_xip()` — on small parts; streaming
  per-declaration compilation is planned.

## Feasibility of a "tiny" target (2 KB SRAM / 16 KB flash)
Not achievable with this VM: the smallest build is ~84 KB of code and the smallest
measured working configuration (`examples/lowram`) needs a 24–32 KB heap. A tiny profile would need a separate design (static typing at
compile time, no GC, no reflection-style metadata, ahead-of-time images only). It is
listed as research, not as a supported target.
