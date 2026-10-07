# Running MicroCS on small MCUs

This guide is for parts with **16–128 KB of RAM and 64 KB+ of flash** (Cortex-M0+/M3/M4
class: STM32F0/F1/G0/L4, nRF52, SAMD21, RP2040, …). The floor is the same as MicroPython's
16 KB of RAM — and 64 KB of flash instead of 256 KB. It lists every knob that trades features or speed for RAM and flash,
what each one costs, and how to measure your own script.

> [!IMPORTANT]
> All Cortex-M numbers below come from firmware built with arm-none-eabi-gcc 13.2.1
> (`-Os`, newlib-nano) and executed in the Unicorn emulator (`make cm-check`,
> `tools/cm_emu.py`). They are instruction counts and byte counts, **not** measurements on
> physical silicon. 1 KB = 1024 bytes. Host numbers are from a 64-bit Linux build, which needs roughly twice the
> heap of a 32-bit MCU for the same script.

## Quick answer

| Target | Build | Notes |
|---|---|---|
| any | `profiles/mcs_profile_auto.h` (CMake `MICROCS_PROFILE=auto`, default for STM32 and Zephyr) | picks one of the rows below from `MCS_TARGET_RAM_KB` / `MCS_TARGET_FLASH_KB` or the device macro |
| ≥ 256 KB RAM | default | everything, 64-bit `long` optional |
| 96–256 KB RAM | `profiles/mcs_profile_embedded.h` | compile scripts on the device, REPL, shell, filesystem |
| 64–96 KB RAM | `profiles/mcs_profile_mcu.h` | precompiled images only, all modules |
| 32–64 KB RAM | `profiles/mcs_profile_lowram.h` + `mcs_exec_image_xip()` + a reduced `cfg.stdlib` | see [the example](../examples/lowram/) |
| 16–32 KB RAM, ≥ 128 KB flash | lowram or tiny + a 12 KB pool | the [`examples/lowram`](../examples/lowram/) node runs in 16 KB (`m0-16k`) |
| **16 KB RAM, 64 KB flash** | `profiles/mcs_profile_min.h` | reduced stdlib, tiny printf, no libm — whole firmware 60.6 KB (`m0-64k`; superinstructions and inline caches off, runs `-O0` images) |
| < 16 KB RAM or < 64 KB flash | not supported | the auto profile and the STM32 device table stop the build (`MCS_ALLOW_SMALL_TARGET=1` to try anyway) |

## Measured configurations (Cortex-M0, emulated)

| Firmware | Flash | RAM | Heap after `mcs_new` | Workload | Pool peak |
|---|---:|---:|---:|---|---:|
| `m0-runtime` — default config, no compiler, all modules, full stdlib, double floats | 197.6 KB | 128 KB | 7.8 KB | `demo.cs` image, 4.54 M instr | 38.0 KB of 100 KB |
| `m0-lowram` — lowram profile, all modules, full stdlib | 185.7 KB | 64 KB | 5.0 KB | `demo.cs` image, 4.49 M instr | 30.0 KB of 40 KB |
| `m0-node` = [`examples/lowram`](../examples/lowram/) — lowram profile, no FS/HAL/scheduler, `MCS_LIB_CORE \| MCS_LIB_COLLECTIONS` | 146.7 KB | 48 KB | 1.7 KB | 24 `Node.Tick()` calls, 1.04 M instr | 14.3 KB of 32 KB |
| `m0-16k` — the same node firmware in a **16 KB** part (12 KB pool, 2 KB C stack) | 146.7 KB | 16 KB | 1.7 KB | same, 1.03 M instr, 12 collections | 11.2 KB of 12 KB |
| `m0-64k` — `min` profile, **64 KB flash / 16 KB RAM** | **60.6 KB** | 16 KB | 1.8 KB | same, 0.93 M instr, 4 collections | 10.4 KB of 12 KB |

Flash is the whole firmware: MicroCS, the script image, startup code and newlib-nano with
float `printf`/`strtod` and libm (the `m0-64k` build uses MicroCS's built-in tiny printf
and needs no libm). The class tables are built lazily (`MCS_LAZY_CLASSES=1`): a class costs
RAM only once a script touches it, which is why a VM now starts in 1.7–8 KB instead of the
13–25 KB of 1.4.

Reproduce: `make cm-check` (builds and runs all of them; output must match the host).

## Checklist, biggest win first

### 1. Run precompiled images, not source
`MCS_ENABLE_COMPILER=0` removes the lexer, parser and code generator (~44 KB of flash) and,
more importantly, the compile-time RAM: the whole token array and AST of a script are alive
while it compiles. Compiling the 2.5 KB `demo.cs` on the emulated M4 peaks at 71.8 KB of pool;
running its image peaks at 38.0 KB. Build images on the host:

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
With lazy class tables (`MCS_LAZY_CLASSES=1`, default) a class that the script never
touches costs nothing at all; leaving out the FS/HAL/scheduler modules mostly saves flash.

### 5. Pool allocator alignment
`MCS_POOL_ALIGN=4` (lowram profile) halves the per-block header of the built-in pool
allocator and its rounding waste. It is safe on every Cortex-M (ARMv6-M/v7-M/v8-M need only
word alignment, including for `LDRD`/`VLDR`); it is clamped to the pointer size, so it cannot
break 64-bit hosts. On `m0-runtime` it saved another 1.9 KB right after `mcs_new`.

### 6. VM limits that are static RAM
| Option | Default | lowram | min | Cost |
|---|---:|---:|---:|---|
| `MCS_DEFAULT_STACK` (or `cfg.stack_slots`) | 1024 | 128 | 128 | one value per slot |
| `MCS_DEFAULT_FRAMES` (or `cfg.max_frames`) | 64 | 24 | 24 | ~24 B per frame |
| `MCS_MAX_HANDLERS` | 32 | 12 | 12 | nested `try` blocks |
| `MCS_MAX_ROOTS` | 32 | 16 | 16 | C-side GC roots |
| `MCS_ERROR_SIZE` | 256 | 128 | 96 | last-error text buffer |
| `MCS_MAX_PINS` | 16 | 8 | 8 | C-held handles (`mcs_pin`) |

Exceeding the stack or frame limit raises a catchable `StackOverflowException`; the ports
show how to size them (`CM_STACK_SLOTS`, `CM_FRAMES` in `ports/cortex-m/main.c`).

### 7. Garbage-collector tuning
* `cfg.heap_limit` — the hard cap. When an allocation would cross it, the VM collects first
  and only then fails with `MCS_ERR_MEMORY`. Leave ~1 KB of pool slack for fragmentation.
* `MCS_GC_INITIAL` (default 16 KB, lowram 4 KB) — bytes allocated before the first
  collection; `MCS_GC_GROW` (2) — next threshold = live bytes × grow. A low first threshold
  keeps the peak close to the live set.
* `cfg.alloc_overhead` — per-allocation header bytes of your allocator. `mcs_new` sets it
  for the built-in pool (`MCS_POOL_OVERHEAD`); with your own `realloc_fn`, set it so the GC
  threshold and `heap_limit` count what the allocator really uses — on a 12 KB pool the
  headers are a quarter of the heap.
* The string intern table shrinks after a collection and grows only when it is really full
  near a collection, so it no longer doubles while garbage is still waiting to be freed.
* `mcs_mem_stats()` gives in-use bytes, peak, objects and the collection count. A collection
  count in the hundreds for a short run means the pool is too small for the live data.

### 8. Flash
Cortex-M0 `-Os`, MicroCS core objects (`src/`, before `--gc-sections`, no modules, no libc):

| Build | Code |
|---|---:|
| default | 154.0 KB |
| `profiles/mcs_profile_embedded.h` | 152.0 KB |
| `profiles/mcs_profile_mcu.h` | 107.0 KB |
| `profiles/mcs_profile_lowram.h` | 98.1 KB |
| `profiles/mcs_profile_tiny.h` (no float, no Dictionary) | 81.9 KB |
| `profiles/mcs_profile_min.h` | 60.2 KB |

Modules (min profile objects): HAL classes 25.5 KB, simulator board 3.6 KB, File/Directory
5.1 KB, VFS 2.1 KB, RAM filesystem 1.5 KB, scheduler 2.6 KB, SPI NOR/NAND drivers 2.6 KB,
LittleFS adapter 0.9 KB.

### 8a. Fitting 64 KB of flash: `profiles/mcs_profile_min.h`
Nothing is removed from MicroCS — every part of the standard library is a switch. The `min`
profile turns off the big ones; a script that calls a disabled member gets a
`MissingMemberException`. Turn any of them back on with `-D…=1`; cost on top of the min
profile (core objects, Cortex-M0):

| Switch | Gives back | Flash |
|---|---|---:|
| `MCS_ENABLE_COMPILER` | on-device compiler | +44.5 KB |
| `MCS_ENABLE_FLOAT` | `float`/`double`, `Math` floating point | +9.6 KB |
| `MCS_ENABLE_LINQ` | `Enumerable` + LINQ operators | +9.5 KB |
| `MCS_ENABLE_ARRAY_EXTRA` | `Array.*` statics, `List.AddRange/InsertRange/RemoveRange/RemoveAll/GetRange/Sort/Reverse/TrimExcess/AsReadOnly`, extra sequence members | +6.7 KB |
| `MCS_INT64` | 64-bit `long` | +5.5 KB |
| `MCS_ENABLE_DICT` | `Dictionary`, `HashSet` | +4.2 KB |
| `MCS_ENABLE_STRING_EXTRA` | `string.PadLeft/PadRight/Insert/Remove/Replace/Split/LastIndexOf…`, `char` extras | +2.8 KB |
| `MCS_ENABLE_STRINGBUILDER` | `StringBuilder` | +2.3 KB |
| `MCS_ENABLE_STACK_QUEUE` | `Stack<T>`, `Queue<T>` | +2.0 KB |
| `MCS_ENABLE_CONVERT` | `Convert` | +1.3 KB |
| `MCS_ENABLE_DIAGNOSTICS` | `GC`, `Debug`, `Stopwatch` | +1.1 KB |
| `MCS_ENABLE_RANDOM` | `Random` | +0.7 KB |
| `MCS_ENABLE_LINES` | line numbers in errors | +0.1 KB (+ image line tables) |
| `MCS_ENABLE_STDIO` | `stdout`/`stdin` fallback when `cfg.write_fn` is unset, `clock()` | +0.1 KB (+ libc stdio) |
| `MCS_ENABLE_MALLOC` | `realloc`/`free` as the default allocator | – (+ libc malloc) |
| `MCS_TINY_PRINTF=0` | libc `snprintf` instead of the built-in one (`src/mcs_fmt.c`) | + libc printf (~1–8 KB) |
| `MCS_ENABLE_FORMAT` | `string.Format` / interpolation format specifiers | ~0 without float |

The firmware itself must avoid pulling in what the profile saves: no `printf` (use
`cfg.write_fn` and your own number printing, as [`examples/lowram/lowram_firmware.c`](../examples/lowram/lowram_firmware.c)
does with `NODE_PUTS`), no float formatting, and link with `-Wl,--gc-sections`
newlib-nano (see the `m0-64k` rule in [`ports/cortex-m/Makefile`](../ports/cortex-m/Makefile)).

`MCS_ENABLE_DISASM`, `MCS_ENABLE_BYTECODE_SAVE`, `MCS_COMPUTED_GOTO`, `MCS_FIELD_CACHE` and the
module flags (`MCS_ENABLE_FS`, `MCS_ENABLE_HAL`, `MCS_ENABLE_SCHED`, `MCS_ENABLE_SHELL`,
`MCS_ENABLE_FLASH`) work the same way in every profile. `MCS_COMPUTED_GOTO` and
`MCS_FIELD_CACHE` together cost only ~1.3 KB on the M0; keep them.

### 8b. Per-MCU settings: the auto profile
`profiles/mcs_profile_auto.h` chooses a profile from the target's memory, so the same
project builds the right runtime for a 16 KB STM32F072 and a 1 MB STM32H7:

```cmake
set(MICROCS_PROFILE auto)          # default when MICROCS_PORT=stm32
set(MICROCS_RAM_KB 64)             # optional: otherwise from the device macro / Zephyr Kconfig
set(MICROCS_FLASH_KB 256)
```

| Detected | Profile |
|---|---|
| flash < 128 KB or RAM < 32 KB | min |
| flash < 256 KB | tiny + lowram VM limits |
| RAM < 64 KB | lowram |
| RAM < 96 KB | mcu |
| RAM < 256 KB | embedded |
| otherwise or unknown | default |

Sources, in order: `MCS_TARGET_RAM_KB`/`MCS_TARGET_FLASH_KB` (CMake `MICROCS_RAM_KB`/
`MICROCS_FLASH_KB`), Zephyr `CONFIG_SRAM_SIZE`/`CONFIG_FLASH_SIZE`, the STM32 CMSIS device
macro ([`mcs_target_stm32.h`](../include/profiles/mcs_target_stm32.h)), RP2040/RP2350,
nRF52, SAMD21/51. Board ports (`MCS_PORT_HAL=1`, set by CMake for `MICROCS_PORT` and by
Zephyr) keep the HAL classes when the flash is 128 KB or more. A device macro below 16 KB
RAM / 64 KB flash (STM32F030x6, F031, G031, L031, …) stops the build with an `#error`.
ESP-IDF has its own choice (`idf.py menuconfig` → MicroCS profile; ESP32-C2 defaults to
embedded).

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
