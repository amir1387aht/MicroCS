# MicroCS vs MicroPython vs .NET nanoFramework vs bare-metal C

The same five firmware-style scripts as [`bench/mcu/`](../mcu) (and the three host scripts
of [`bench/`](..)) written idiomatically for each runtime and in plain C, with identical output.
Results and caveats: [docs/PERFORMANCE.md](../../docs/PERFORMANCE.md#microcs-vs-micropython-vs-net-nanoframework).

| Workload | What it does |
|---|---|
| `fib` | recursive `fib(18)` — calls/returns |
| `loop` | 50 000-iteration integer loop with `%`, `&`, branches, in a function |
| `objects` | 4 × 500 small objects in a list, a method call on each |
| `sensor` | 300 LCG samples into a 64-entry `double` ring buffer, EMA, average, formatted output |
| `strings` | build `"0,1,…,199,"`, split it and parse every number |

## MicroCS — `make mcu-bench`

`ports/cortex-m/bench.c`, emulated Cortex-M4F / M0 (`tools/cm_emu.py`). Build with
`BENCH_CFLAGS="-DBENCH_MINHEAP -DBENCH_MODES=1"` to also binary-search the smallest pool each
script runs in (VM state + 256-slot value stack + frames + GC heap).

## MicroPython — `micropython/`

`MPY=/path/to/micropython sh bench/compare/micropython/build.sh` (tested v1.26.0, run
`make -C mpy-cross` first). Builds MicroPython from `ports/embed` with
[`mpconfigport.h`](micropython/mpconfigport.h) (`EXTRA_FEATURES` ROM level as on stm32/rp2,
computed goto, map lookup cache, double floats, MPZ long ints) and
[`mpbench.c`](micropython/mpbench.c) with the **same** toolchain, `-Os`, CPU flags, linker
script and board code as the MicroCS bench firmware, and runs it in the same emulator. Each
script runs on a fresh interpreter from a precompiled `.mpy` (mpy-cross) and from source;
the instruction count covers interpreter init + load + run, as for MicroCS. It also
binary-searches the smallest GC heap that still prints the right output.

Host: `micropython host_time.py fib.py` (unix port, best of 50, compile excluded) and the
`bench/*.py` scripts.

## .NET nanoFramework — `nanoframework/`

`sh bench/compare/nanoframework/run.sh` (needs the .NET 8 SDK and nuget.org). Compiles
[`Program.cs`](nanoframework/Program.cs) with Roslyn against nanoFramework `mscorlib`
1.17.12 (+ `System.Text` for `StringBuilder`; `ArrayList` instead of `List<T>`), converts it
with MetadataProcessor 3.0.104 and runs it on the **nanoCLR virtual device** (`nanoclr`
1.1.311, the native x64 nanoCLR) on the PC — nanoFramework has no bare-metal Cortex-M build
that runs in this emulator, so it is compared on the host only.

## Bare-metal C — `c/`

`sh bench/compare/c/build.sh`: [`cbench.c`](c/cbench.c) on the same emulated boards with the
same toolchain, `-Os`, linker script and newlib as the MicroCS firmware (instructions per
workload, output included), plus a host build at `-O2`. Inputs are read from `volatile`s so
GCC cannot compute the results at build time.

## Real hardware

Timed inside the script on the board (MicroCS: `Stopwatch.StartNew()` /
`ElapsedMilliseconds`; MicroPython: `time.ticks_ms()` / `time.ticks_diff()`). Results so far
are in [PERFORMANCE.md](../../docs/PERFORMANCE.md#real-hardware--esp32-s3); PRs with other
boards are welcome.
