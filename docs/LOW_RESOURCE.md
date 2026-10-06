# Running MicroCS on small MCUs

This guide is for parts with **32–128 KB of RAM** (Cortex-M0+/M3/M4 class: STM32F0/G0/L4,
nRF52, RP2040, …). It lists every knob that trades features or speed for RAM and flash,
what each one costs, and how to measure your own script.

> [!IMPORTANT]
> All Cortex-M numbers below come from firmware built with arm-none-eabi-gcc 13.2.1
> (`-Os`, newlib-nano) and executed in the Unicorn emulator (`make cm-check`,
> `tools/cm_emu.py`). They are instruction counts and byte counts, **not** measurements on
> physical silicon. 1 KB = 1024 bytes. Host numbers are from a 64-bit Linux build, which needs roughly twice the
> heap of a 32-bit MCU for the same script.

## Quick answer

| RAM for MicroCS | Build | Notes |
|---|---|---|
| ≥ 160 KB | default, or `profiles/mcs_profile_embedded.h` | compile scripts on the device, shell, filesystem |
| 64–160 KB | `profiles/mcs_profile_mcu.h` | precompiled images only, all modules |
| 24–64 KB | `profiles/mcs_profile_lowram.h` + `mcs_exec_image_xip()` + a reduced `cfg.stdlib` | see [the example](../examples/lowram/) |
| < 16 KB | not supported | needs a different VM design (see [PERFORMANCE.md](PERFORMANCE.md#feasibility-of-a-tiny-target-2-kb-sram--16-kb-flash)) |

## Measured configurations (Cortex-M0, emulated)

| Firmware | Flash `.text` | Heap after `mcs_new` | Workload | Pool peak |
|---|---:|---:|---|---:|
| `m0-runtime` — default config, no compiler, all modules, full stdlib, double floats | 167.6 KB | 25.1 KB | `demo.cs` image, 4.56 M instr | 63.5 KB of 100 KB |
| `m0-lowram` — lowram profile, all modules, full stdlib | 158.5 KB | 19.9 KB | `demo.cs` image, 4.59 M instr | 33.3 KB of 40 KB |
| `m0-node` = [`examples/lowram`](../examples/lowram/) — lowram profile, no FS/HAL/scheduler, `MCS_LIB_CORE \| MCS_LIB_COLLECTIONS` | 144.1 KB (−11.9 KB with `MCS_ENABLE_LINQ=0`) | 13.0 KB | 24 `Node.Tick()` calls, 1.28 M instr | 27.4 KB of 32 KB |

Flash includes newlib-nano with float `printf`/`strtod` and libm. The same
`examples/lowram` firmware also completes in a **24 KB** pool, but the collector then runs
158 times instead of 3 and the run takes 6.4 M instead of 1.3 M instructions — the usual
sign that the heap is too small for the live data.

Reproduce: `make cm-check` (builds and runs all of them; output must match the host).

## Checklist, biggest win first

### 1. Run precompiled images, not source
`MCS_ENABLE_COMPILER=0` removes the lexer, parser and code generator (~44 KB of flash) and,
more importantly, the compile-time RAM: the whole token array and AST of a script are alive
while it compiles. Compiling the 2.5 KB `demo.cs` on the emulated M4 peaks at 88.4 KB of pool;
running its image peaks at 50.0 KB. Build images on the host:

```sh
./mcs -C node.cs -n node_image -o node_image.h    # const C array, link it into flash
./mcs -c node.cs -o node.mcsb                       # or a file for the filesystem / OTA
```

Images are only validated structurally — load images you built yourself
([SECURITY.md](SECURITY.md)).

### 2. Execute images in place
`mcs_exec_image_xip(vm, image, len)` runs bytecode straight out of the image buffer (flash)
instead of copying every function's code to the heap. The image must stay valid and
unchanged until `mcs_free(vm)`.

| Script (host, 64-bit) | Image | Heap in use, copy → XIP |
|---|---:|---:|
| `tests/t02_oop.cs` | 6.4 KB | 72.6 → 70.7 KB |
| `tests/t05_functional.cs` | 4.0 KB | 58.3 → 57.3 KB |
| `tests/t13_stdlib_more.cs` | 5.1 KB | 117.1 → 95.7 KB (peak 129.0 → 124.1 KB) |

The saving is roughly the bytecode size of the image; a small per-function table for global
slots is added. On the M4 the extra indirection costs 0.4 % of instructions on the demo.
`MCS_ENABLE_XIP=0` removes the feature (then `mcs_exec_image_xip` copies).

### 3. Shrink the value size
Every stack slot, global, field, array element and dictionary entry is one `mcs_value_t`.

| Configuration (32-bit MCU) | `sizeof(mcs_value_t)` |
|---|---:|
| `MCS_FLOAT_DOUBLE=1`, `MCS_COMPACT_VALUES=0` | 16 B |
| `MCS_FLOAT_DOUBLE=1`, `MCS_COMPACT_VALUES=1` (default on 32-bit) | 12 B |
| `MCS_FLOAT_DOUBLE=0`, `MCS_INT64=0` (lowram profile) | 8 B |

`MCS_COMPACT_VALUES` packs values to 4-byte alignment. On Cortex-M0 it also made the demo
**~4× faster** (18.2 M → 4.6 M instructions): with 16-byte values GCC called `memcpy` for
every value copy. On the M4 it costs ~2 % more instructions and saves 7.8 KB of pool on the
demo. It is off by default on 64-bit hosts, where it would only cost speed.

`MCS_FLOAT_DOUBLE=0` makes `double` single precision (also faster on M4F/M33 FPUs);
`MCS_INT64=0` (the default) makes `long` 32 bits. Both change numeric results compared with
.NET — see [LANGUAGE.md](LANGUAGE.md).

### 4. Open only the libraries the script uses
```c
cfg.stdlib = MCS_LIB_CORE | MCS_LIB_COLLECTIONS;   /* no Math, StringBuilder, Random, ... */
```
Built-in members are registered lazily from ROM tables (`MCS_LAZY_REGS=1`, default), so an
unused library costs little, but each opened class still needs its class object and name.
Leaving out the FS/HAL/scheduler modules and the text/math/system libraries is what takes
the `examples/lowram` VM from 19.9 KB to 13.0 KB.

### 5. Pool allocator alignment
`MCS_POOL_ALIGN=4` (lowram profile) halves the per-block header of the built-in pool
allocator and its rounding waste. It is safe on every Cortex-M (ARMv6-M/v7-M/v8-M need only
word alignment, including for `LDRD`/`VLDR`); it is clamped to the pointer size, so it cannot
break 64-bit hosts. On `m0-runtime` it saved another 1.9 KB right after `mcs_new`.

### 6. VM limits that are static RAM
| Option | Default | lowram | Cost |
|---|---:|---:|---|
| `MCS_DEFAULT_STACK` (or `cfg.stack_slots`) | 1024 | 192 | one value per slot |
| `MCS_DEFAULT_FRAMES` (or `cfg.max_frames`) | 64 | 32 | ~24 B per frame |
| `MCS_MAX_HANDLERS` | 32 | 16 | nested `try` blocks |
| `MCS_ERROR_SIZE` | 256 | 128 | last-error text buffer |
| `MCS_MAX_PINS` | 16 | 8 | C-held handles (`mcs_pin`) |

Exceeding the stack or frame limit raises a catchable `StackOverflowException`; the ports
show how to size them (`CM_STACK_SLOTS`, `CM_FRAMES` in `ports/cortex-m/main.c`).

### 7. Garbage-collector tuning
* `cfg.heap_limit` — the hard cap. When an allocation would cross it, the VM collects first
  and only then fails with `MCS_ERR_MEMORY`. Leave ~1 KB of pool slack for fragmentation.
* `MCS_GC_INITIAL` (default 16 KB, lowram 4 KB) — bytes allocated before the first
  collection; `MCS_GC_GROW` (2) — next threshold = live bytes × grow. A low first threshold
  keeps the peak close to the live set.
* `mcs_mem_stats()` gives in-use bytes, peak, objects and the collection count. A collection
  count in the hundreds for a short run means the pool is too small for the live data.

### 8. Flash
Cortex-M0 `-Os`, MicroCS objects only (before `--gc-sections`, all modules, no libc):

| Build | Code |
|---|---:|
| default | 171.1 KB |
| `profiles/mcs_profile_embedded.h` | 169.1 KB |
| `profiles/mcs_profile_mcu.h` | 120.7 KB |
| `profiles/mcs_profile_lowram.h` | 111.3 KB |
| lowram + `MCS_ENABLE_LINQ=0` | 99.5 KB |
| `profiles/mcs_profile_tiny.h` (no float, no Dictionary, no modules) | 84.3 KB |

Other flash switches: `MCS_ENABLE_DISASM=0`, `MCS_ENABLE_LINES=0` (no line numbers in
errors), `MCS_ENABLE_FORMAT=0`, `MCS_ENABLE_STRINGBUILDER=0`, `MCS_ENABLE_RANDOM=0`, and the
module flags. `MCS_ENABLE_LINQ=0` keeps the `List<T>` instance methods (`Find`, `ForEach`,
`Contains`, `ToArray`, …) and removes the LINQ operators and `Enumerable`.
`MCS_COMPUTED_GOTO` and `MCS_FIELD_CACHE` together cost only ~1.3 KB on the M0; keep them.

### 9. Write scripts that keep the live set small
* Reuse objects and arrays in loops (see the `Ring` buffer in
  [`examples/lowram/node.cs`](../examples/lowram/node.cs)); every `new` and every string
  concatenation allocates.
* All strings are interned: building many distinct temporary strings churns the intern table.
* `Dictionary`/`HashSet` cost per entry (1.3, 64-bit host): an index slot of 1, 2 or 4 bytes
  (by capacity) plus the key and value arrays; `HashSet` stores no values. A 700-string
  `HashSet` went from 104.3 KB to 58.3 KB in 1.3, a 100-entry `Dictionary<int,int>` from
  12.1 KB to 4.3 KB.
* LINQ operators materialise their results (eager), so `xs.Where(...).Select(...)` allocates
  two lists. A `for` loop allocates nothing.

## Measuring your own script

```sh
./mcs -c app.cs && ./mcs --xip --heap 65536 --stats app.mcsb   # host: in use, peak, collections
./mcs --heap 32768 --stack 192 app.mcsb                         # does it still run in 32 KB?
```

Host figures are an upper bound for a 32-bit MCU (16-byte values and 8-byte pointers on the
host). For exact numbers build the Cortex-M reference port with your script and run it in
the emulator; `--profile` shows where the instructions go:

```sh
make cm                                                   # needs arm-none-eabi-gcc
python3 tools/cm_emu.py build/cm/m0-lowram.elf --cpu m0 --profile 15
python3 tools/cm_emu.py build/cm/m0-lowram.elf --cpu m0 --callers memcpy
```

`--profile N` prints the N hottest functions (instruction counts per symbol);
`--callers SYMBOL` shows who calls a function — this is how the `memcpy` value copies on the
M0 were found.
