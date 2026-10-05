# MicroCS — a C# runtime for microcontrollers

MicroCS runs a practical subset of C# on MCUs, the way MicroPython does for Python.
It is portable C99 with no dependencies, embeds in any firmware (bare metal, RT-Thread,
FreeRTOS, Zephyr) and is easy to extend with C functions. Version **1.1.0** (Phase 2)
adds an optional platform around the Phase 1 interpreter: a virtual filesystem, a
hardware abstraction layer with C# peripheral classes, a job scheduler, execution
limits, and a standalone "script manager" runtime driven over UART.

```
 C# source ─► lexer ─► parser ─► compiler ─┐                      (optional on device)
 .mcsb image (precompiled on a PC) ────────┴─► stack VM + GC + stdlib        = core
                                                   │ public API: include/mcs.h
   modules/fs  (VFS: RAM, POSIX, LittleFS; C# File/Directory/Path)
   modules/hal (GPIO, UART, I2C, SPI, ADC, PWM; simulator board)
   modules/sched (startup / once / periodic jobs, jobs.cfg)
   modules/shell (boot.cs → jobs.cfg → main.cs, upload/run/manage over UART)
```

## Status — what is validated and what is not

| Area | Status | Evidence |
|---|---|---|
| Interpreter core (Phase 1) | **stable** | `make test`, GC-stress build, 17 feature-flag/profile builds |
| `ref`/`out`/`in`, `case` patterns with `when` (Phase 2) | **stable** | `tests/t10_lang_phase2.cs`, output byte-identical to .NET 8 |
| VFS + RAM/POSIX backends, C# `File`/`Directory`/`Path` | **stable (host-tested)** | `tests/t07_filesystem.cs`, `tests/c/test_modules.c` |
| LittleFS backend | **experimental** | `make lfs-test` (RAM block device, remount, ENOSPC); not run on real flash |
| HAL + C# peripheral API | **stable API, simulator only** | `tests/t08_hal.cs`; no real board driver is included |
| Scheduler | **stable (host-tested)** | `tests/t09_scheduler.cs`, unit tests |
| Standalone shell / script manager | **stable (host + emulated M33)** | `tests/test_shell.py`, `tests/test_cm_shell.py` |
| Execution limits (time/steps), abort reasons | **stable** | `tests/err_limits.cs`, unit tests |
| Cortex-M0 / M4 / M33 bare-metal builds | **experimental** | built with arm-none-eabi-gcc 13.2, executed in the Unicorn CPU emulator (`make cm-check`); **never run on real silicon** |
| RT-Thread / SF32LB525 port | **planned** | integration notes in `docs/PORTING.md`, untested |
| "Tiny" 2 KB RAM / 16 KB flash class | **not feasible with this VM** | smallest measured build is ~81 KB flash, ~40 KB heap (see `docs/PERFORMANCE.md`) |
| Debugger, bytecode signing, cron syntax, USB/TCP/BLE transports | **planned** | — |

## Quick start
```sh
make                 # ./mcs  (CLI + REPL + standalone shell)
make test            # 24 script runs (source + image), 79 C unit checks, 16 shell protocol checks
make check           # + GC-stress build + 17 feature-flag / profile builds (-Werror)
make lfs-test        # LittleFS backend (downloads littlefs v2.9.3)
make cm cm-check     # Cortex-M firmware + emulator run (needs arm-none-eabi-gcc, pip unicorn)
make bench           # startup / compile / image-load / execution timings

./mcs app.cs                          # run source (files under . are visible to File.*)
./mcs -c app.cs && ./mcs app.mcsb     # precompile to a bytecode image, run it
./mcs -C app.cs -n app_image -o app_image.h   # image as a C array for flash
./mcs --sim tests/t08_hal.cs          # peripherals against the simulator board
./mcs --step-limit 100000 --time-limit 500 untrusted.cs
./mcs --shell --fs device_root        # standalone runtime on stdin/stdout
python3 tools/mcs_remote.py --exec "./mcs --shell --fs device_root" put app.cs /main.cs + run /main.cs
```

## C# on the device
```csharp
GPIO.Mode(13, GPIO.Output);
Scheduler.Every(500, () => GPIO.Toggle(13));             // polled by the host loop
byte[] t = I2C.WriteRead(0, 0x48, new byte[] { 0 }, 2);    // TMP102-style sensor
File.AppendAllText("/data/log.csv", $"{Environment.TickCount},{t[0]}\n");
if (int.TryParse(File.ReadAllText("/cfg/rate.txt"), out var rate)) Console.WriteLine(rate);
switch (msg) { case string s when s.StartsWith("set "): Apply(s); break; }
```

## Language
Classes, structs (reference semantics), interfaces, inheritance/virtual/abstract, properties,
indexers, operator overloading, enums, erased generics, delegates/events, lambdas and
closures, local functions, `params`, default arguments, overloads, initializers, string
interpolation with format specifiers, `switch` statements (constants, type patterns,
relational patterns, `or`, `when` guards) and switch expressions, `is`/`as`, `?.`/`??`,
exceptions with filters and `finally`, `using`/`using var`, `foreach`, top-level statements,
**`ref`/`out`/`in` parameters and arguments** (`out var`, `out _`, `int/double/bool.TryParse`,
`Dictionary.TryGetValue`). Library: Console, Math, Convert, string, StringBuilder, List,
Dictionary, HashSet, Stack, Queue, arrays, eager LINQ, Random, Stopwatch, Thread.Sleep,
Environment, GC, the .NET exception hierarchy; plus File/Directory/Path, GPIO/UART/I2C/SPI/
ADC/PWM/Hal and Scheduler when the modules are enabled.

Not supported: named arguments, tuples, `yield`, `async`, `goto`, multi-dimensional arrays,
reflection, `unsafe`, inheriting built-in collections. `ref`/`out` use copy-in/copy-out
(the callee works on a copy that is written back when it returns — visible difference
only if the callee throws, or aliases the same variable twice). Details:
`docs/LANGUAGE.md`.

## Embedding (minimal)
```c
#include "mcs.h"
#include "mcs_vfs.h"
#include "mcs_hal.h"

static uint8_t heap[160 * 1024]; static mcs_pool_t pool;
static mcs_vfs_t vfs; static mcs_hal_t hal;   /* fill hal with your board's functions */

void script_task(void) {
    mcs_config_t cfg; mcs_config_default(&cfg);
    mcs_pool_init(&pool, heap, sizeof heap);
    cfg.realloc_fn = mcs_pool_realloc; cfg.alloc_ud = &pool;
    cfg.write_fn = uart_write; cfg.ticks_fn = millis; cfg.delay_fn = delay_ms;
    mcs_vm_t* vm = mcs_new(&cfg);
    mcs_limits_t lim = { .time_ms = 2000, .steps = 0 };
    mcs_set_limits(vm, &lim);                       /* runaway scripts are stopped */
    mcs_fs_open_lib(vm, &vfs);                      /* File / Directory / Path   */
    mcs_hal_open_lib(vm, &hal);                     /* GPIO / UART / I2C / ...   */
    mcs_exec_file(vm, &vfs, "/main.cs");            /* source or .mcsb, auto-detected */
}
```
More: `docs/EMBEDDING.md`, `examples/firmware_example.c`, `ports/cortex-m/main.c`.

## Measured numbers (details and method: `docs/PERFORMANCE.md`)
Host (x86-64, gcc -O2, best of 5) vs CPython 3.13 on the same machine:

| | MicroCS | CPython 3.13 |
|---|---|---|
| fib(30) recursive | 93 ms | 109 ms |
| 10M-iteration loop | 424 ms | 931 ms |
| 1M objects + method calls | 240 ms | 386 ms |

Cortex-M (arm-none-eabi-gcc 13.2 `-Os`, emulated; instruction counts, not cycles):

| Target | Flash (.text) | Heap after `mcs_new` | Demo as image | Demo from source (compile + run) |
|---|---|---|---|---|
| M0, runtime only | 165 KB | 50 KB | 19.5 M instr, 86 KB heap peak | — |
| M4F, full | 205 KB | 50 KB | 4.0 M instr | 5.8 M instr (compile ≈ 1.8 M), ≈160 KB heap peak |
| M33, full (+ shell: 208 KB) | 205 KB | 50 KB | 4.0 M instr | 5.8 M instr |

Flash includes newlib + libm (~58 KB) and the optional modules (~21 KB).

## Layout
```
include/   mcs.h (API)  mcs_bind.h  mcs_config.h  mcs_vfs.h  mcs_hal.h  mcs_sched.h  mcs_shell.h
include/profiles/   tiny / mcu / embedded / linux build profiles
src/       core: lexer, parser, compiler, bytecode images, VM, GC, stdlib
modules/   fs/ hal/ sched/ shell/   (optional; public API only)
ports/     unix (CLI), cortex-m (bare-metal reference firmware), template
tools/     mcs_remote.py, cm_emu.py, cm_check.sh, map_sizes.py, verify_dotnet.sh
tests/     *.cs + .out, c/ unit tests, shell protocol tests
docs/      ARCHITECTURE, LANGUAGE, EMBEDDING, FILESYSTEM, HAL, SCHEDULER, STANDALONE,
           SECURITY, PORTING, PERFORMANCE, PHASE2_ROADMAP, HANDOFF_PHASE1/2
```

## License
MIT (see `LICENSE`). LittleFS (BSD-3-Clause) is *not* bundled; `make lfs-test` downloads it.
