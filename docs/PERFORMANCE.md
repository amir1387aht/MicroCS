# Performance and footprint (measured)

All numbers below were produced in this repository's CI-like sandbox; reproduce them
with the listed command. Nothing here was measured on physical hardware.

## Host interpreter (x86-64 Xeon @ 2.9 GHz, gcc 11.5 -O2) — `make bench`
Best of 5 runs; "compile" is source → image without executing.

| Script | Source | Image | VM new | Compile | Run (from source) | Run (image) |
|---|---|---|---|---|---|---|
| bench/fib.cs (fib 30) | 174 B | 319 B | 29–46 µs | 17 µs | 97.6 ms | 93.3 ms |
| bench/loop.cs (10 M iterations) | 182 B | 279 B | 29 µs | 11 µs | 427 ms | 424 ms |
| bench/objects.cs (1 M objects) | 462 B | 645 B | 29 µs | 27 µs | 245 ms | 240 ms |
| ports/cortex-m/demo.cs | 2533 B | 2774 B | 29 µs | 141 µs | 0.52 ms | 0.41 ms |

CPython 3.13 on the same machine (`bench/*.py`): fib 109 ms, loop 931 ms, objects 386 ms.
Observation: compilation is cheap relative to execution on a PC; images matter on MCUs
mainly for **RAM** (no compiler working set) and flash (compiler can be left out).

## Cortex-M (arm-none-eabi-gcc 13.2.1, -Os, newlib-nano) — `make cm-check`
Executed in the Unicorn emulator; figures are *instructions*, not cycles (real cores
need more cycles per instruction: flash wait states, loads, branches).

| | m0-runtime | m4-full | m33-full |
|---|---|---|---|
| Flash `.text` (whole firmware) | 165 432 B | 205 240 B | 205 192 B |
| VM creation (`mcs_new` + modules) | 564 507 instr | 206 160 instr | 206 160 instr |
| Heap after `mcs_new` (full stdlib) | 49 952 B | 50 168 B | 50 168 B |
| demo as image: instructions | 19.5 M | 3.96 M | 3.96 M |
| demo as image: pool in use after run | 85 336 B (pool peak 86 128 B of 102 400 B) | 108 488 B* | 108 488 B* |
| demo from source (compile + run) | n/a | 5.79 M (≈1.83 M compile) | 5.79 M |
| pool peak incl. source run | n/a | 162 920 B of 163 840 B** | 162 920 B of 262 144 B |
| C stack peak (painted) | 2.7 KB | 2.7 KB | 2.7 KB |

\* the GC grows its threshold with available heap, so bigger pools show higher peaks; the
M0 run shows the same demo completes with an 86 KB peak. ** the pool limit was the binding
constraint (GC kept usage just under `heap_limit`).
The M0 needs ~5× the instructions of the M4 for the same work; plausible causes are the
missing hardware divider and Thumb-1 code density (not profiled further).

### Flash by component (m33-full, linker map, `python3 tools/map_sizes.py build/cm/m33-full.map`)
| Component | Bytes |
|---|---|
| MicroCS stdlib | 60 817 |
| MicroCS VM core (VM, GC, objects, image loader) | 49 745 |
| MicroCS compiler (lexer, parser, codegen) | 38 189 |
| newlib libm | 25 900 |
| newlib libc (printf incl. float, strtod) | 24 310 |
| module fs (VFS, RAM FS, File API) | 9 363 |
| libgcc (soft double, division) | 7 974 |
| module hal (+ simulator) | 6 183 |
| module shell / scheduler | 3 465 / 1 504 (m33-shell) |
(input-section sizes; string literals counted before merging)

### Profiles (object totals before --gc-sections, Cortex-M0 -Os)
`tiny` 80.8 KB · `mcu` 117.3 KB · `embedded` 160.2 KB (MicroCS code only, no libc).

## Memory findings (Phase 2)
* **Fixed**: an out-of-memory panic during compilation leaked the whole AST arena
  (39 KB in the host test); now freed and the panic propagates.
* **Fixed**: each compiled function allocated a fresh ~4.5 KB (32-bit) compiler state
  from the arena; states are recycled → host peak for demo.cs 294 KB → 237 KB.
* **Improved**: token array pre-sized from the source length (no doubled copies left in
  the arena) → 237 KB → 200 KB host peak.
* **Open**: the stdlib metadata (≈60 builtin classes with method/static tables, interned
  names) costs ~40–50 KB of heap per VM; inherited method tables are copied per class
  (`mcs_table_copy`, ~6 KB). Moving class metadata to ROM tables is the main lever for
  smaller targets (planned).
* **Open**: compile needs the whole token array + AST in RAM (~100 KB for a 2.5 KB script
  on 32-bit); streaming per-declaration compilation is planned. Use images on small parts.

## Feasibility of the "Tiny" target (2 KB SRAM / 16 KB flash)
Not achievable with this VM: the smallest measured build is ~81 KB of code and the
object model needs tens of KB of heap. A Tiny profile would require a separate design
(static typing at compile time, no GC, no reflection-style metadata, ahead-of-time image
only). It is listed as planned research, not as a supported target.
