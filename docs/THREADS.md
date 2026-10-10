# Real OS threads (FreeRTOS, Zephyr, POSIX) — optional

MicroCS is single-threaded by default: one VM, a polled job scheduler, and nothing that
needs an RTOS. **If you don't choose an OS, nothing changes**: no thread code is compiled,
`jobs.cfg` behaves exactly as before, and `Thread.Os` is `"None"`.

Once you pick an OS, C# scripts and functions can run on **their own OS tasks**, and you can
**pin work to a core**. That includes the second core of the ESP32 / ESP32-S3 and the RP2040
(FreeRTOS SMP), and the CPUs of SMP Zephyr boards.

| OS | How to turn it on | Cores |
|---|---|---|
| ESP-IDF (FreeRTOS) | `idf.py menuconfig` → MicroCS → *Real threads on FreeRTOS tasks* (`CONFIG_MICROCS_THREADS=y`) | 2 on ESP32 / S3, 1 on C2 / C3 / C6 |
| FreeRTOS (any port) | CMake `-DMICROCS_OS=freertos`, make `OS=freertos FREERTOS_PATH=…`, or `#define MCS_OS MCS_OS_FREERTOS` | SMP builds: `configNUMBER_OF_CORES` |
| RP2040 (pico-sdk) | `ports/rp2/example`: `-DMICROCS_OS=freertos -DFREERTOS_KERNEL_PATH=<FreeRTOS-Kernel>` | 2 (FreeRTOS SMP) |
| Zephyr | `CONFIG_MICROCS_THREADS=y` (see `ports/zephyr/example/overlay-threads.conf`) | SMP: `CONFIG_MP_MAX_NUM_CPUS` |
| Linux / macOS host | CMake `-DMICROCS_OS=posix`, `make OS=posix` | all |
| detect | `MCS_OS_AUTO`: Zephyr in a Zephyr build, FreeRTOS under ESP-IDF or when `FreeRTOS.h` + `FreeRTOSConfig.h` are on the include path, else none | |

## C#

```csharp
// call a function in another script on its own thread (any core) and get the result
var w = Thread.Run("/math.cs", "Fib", 30);
Console.WriteLine(w.Result);                 // waits; also w.Join([ms]), w.IsAlive, w.Stop()

// the same, pinned to core 1 (the REPL keeps core 0)
var c1 = Thread.RunOn(1, "/dsp.cs", "Filter", samples);

// a whole script on a thread, optionally on a core with its own heap size (KB)
var logger = Thread.Start("/logger.cs", 1, 24);

// OS-timed periodic task: Tick() every 10 ms on core 1, no drift
var loop = Thread.Every(10, "/control.cs", "Tick", 1);

// named channels copy values between threads
var ch = new Channel("samples", 16);         // the same name opens the same channel anywhere
ch.Send(42);                                 // blocks while full; Send(v, ms), TrySend(v)
var v = ch.Receive(500);                     // null after 500 ms; Receive(), TryReceive()

Console.WriteLine($"{Thread.Os} cores={Thread.Cores} on core {Thread.CurrentCore}");
```

| Member | |
|---|---|
| `Thread.Start(path [, core [, heapKB]])` | run a script; returns a `Worker` |
| `Thread.Run(path, function, args…)` | load the script, call `function(args…)`; `Result` is its return value |
| `Thread.RunOn(core, path, function, args…)` | same, pinned to `core` |
| `Thread.Every(ms, path [, function] [, core])` | run the script (or call `function`) every `ms`, OS-timed |
| `Thread.StopAll()` | ask every other thread to stop |
| `Thread.Cores`, `Thread.CurrentCore`, `Thread.Os`, `Thread.Count`, `Thread.Id` | (`Id` 0 = main VM) |
| `Worker.Join([ms])` `Stop()` `IsAlive` `State` `Result` `Error` `Runs` `Id` `Core` | `State`: starting, running, done, failed, stopped |
| `new Channel(name [, capacity])` `Send` `TrySend` `Receive` `TryReceive` `Count` `Capacity` `Name` | capacity 1..256, default 8 |

`Thread.Sleep`, `Thread.Cores` (1), `Thread.CurrentCore` (0) and `Thread.Os` (`"None"`)
also exist without an OS, so one script can check what it runs on.

### What a thread is

* **Its own VM and heap.** VMs are never shared, so threads on different cores run truly in
  parallel without a global lock. Globals are not shared: pass data as arguments, results and
  through `Channel`s. Values are copied: `null`, `bool`, `int`, `char`, `float`/`double`,
  `string`, and arrays / `List`s of them (nested up to 4 levels, `MCS_CHANNEL_MSG_MAX` bytes).
  Objects and dictionaries are rejected with `ArgumentException`.
* **Shared filesystem.** Every filesystem call is locked, so threads can read scripts and
  write logs at the same time as the REPL.
* **Shared console.** Each thread prints whole lines. Unhandled exceptions are printed with a
  `[thread N]` prefix and are also stored in `Worker.Error`. Reading `Result` rethrows them as
  `InvalidOperationException`.
* **Peripherals, not callbacks.** Threads can use `GPIO`, `I2C`, `SPI`, … Interrupt callbacks
  (`GPIO.OnChange`, `Timer`, …) stay with the main VM. Setting one in a thread raises
  `InvalidOperationException`. To get events to a thread, forward them with a `Channel`.
* **Stopping.** `Stop()` is cooperative: the thread notices it between statements, and also
  while it sleeps or waits on a channel.
* **Priority.** Threads start at the priority of the thread that starts them, plus
  `MCS_THREAD_PRIORITY` (0) or `prio=` in `jobs.cfg`. A thread busy-looping at that priority
  starves lower-priority tasks on its core. Give the OS a moment with `Thread.Sleep(…)`. The
  ESP32 example turns off the idle-task watchdog on core 1 for this reason.

## jobs.cfg

Add `thread` to a file job to run it on its own OS thread, timed by the OS.
`core=`, `prio=`, `stack=` and `heap=` imply `thread`:

```
every 10ms /control.cs thread core=1 heap=24k
every 1s   /log.cs     thread stack=8k prio=-1
startup    /main.cs
```

Without an OS these options are ignored and the job is polled as before. `jobs` / `.jobs`
in the shell show `thread core=N worker=ID` for running thread jobs.

## From C

```c
#include "mcs_threads.h"
mcs_thread_spec_t s = MCS_THREAD_SPEC_DEFAULTS;
s.path = "/worker.cs";  s.core = 1;  s.period_ms = 100;   // or s.code/s.code_len, s.function
int id = mcs_thread_start(&s);                            // > 0, or -1 full, -2 memory, -3 core ...
mcs_thread_join(id, 1000); mcs_thread_stop(id); mcs_thread_release(id);
```

`mcs_runtime` (the REPL firmware) sets threads up by itself. The thread heaps come from
`cfg.heap` (behind a lock), or from the OS heap when `cfg.heap` is NULL. `cfg.thread_setup`
registers your own C# bindings in every thread VM. `cfg.thread_heap` and `cfg.thread_stack`
set the defaults. Without the runtime, call `mcs_threads_init()` and then
`mcs_threads_open_lib(vm)` (see `ports/unix/main.c`).

## Options (mcs_user_config.h)

| Option | Default | |
|---|---|---|
| `MCS_OS` | `MCS_OS_NONE` | `MCS_OS_FREERTOS`, `MCS_OS_ZEPHYR`, `MCS_OS_POSIX`, `MCS_OS_AUTO` |
| `MCS_THREADS_MAX` | 8 | threads running at once |
| `MCS_THREAD_HEAP` | 32 KB (256 KB with POSIX threads, 64 KB on other 64-bit builds) | C# heap per thread (`Thread.Start` heapKB, `heap=`) |
| `MCS_THREAD_STACK` | 8 KB (12 KB on ESP32) | OS stack per thread (`stack=`) |
| `MCS_THREAD_SLOTS` / `MCS_THREAD_FRAMES` | 256 / 48 | VM stack of a thread |
| `MCS_THREAD_PRIORITY` | 0 | relative to the starting thread |
| `MCS_CHANNELS_MAX` / `MCS_CHANNEL_MSG_MAX` | 8 / 1024 | |

## Where are the FreeRTOS headers?

ESP-IDF and Zephyr bring their own kernel. For plain FreeRTOS, MicroCS looks in these places,
in order:

1. **CMake**: an existing `freertos_kernel` (FreeRTOS-Kernel's CMake), `FreeRTOS-Kernel`
   (pico-sdk import), or `freertos` target, or a CubeMX `stm32cubemx` target with FreeRTOS
   in `Middlewares/`.
2. **`MICROCS_FREERTOS_PATH`** (or the `FREERTOS_KERNEL_PATH` / `FREERTOS_PATH` environment
   variables), plus **`MICROCS_FREERTOS_PORT`** (e.g. `portable/GCC/ARM_CM4F`) and
   **`MICROCS_FREERTOS_CONFIG_DIR`** (the folder with your `FreeRTOSConfig.h`, searched in the
   project when not set).
3. Common folders in the project: `FreeRTOS-Kernel/`, `third_party/FreeRTOS-Kernel/`,
   `lib/…`, `Middlewares/Third_Party/FreeRTOS/Source/`.

If none of these fits, the configure step stops and says which variable to set. The make
build stops the same way (`make OS=freertos` needs `FREERTOS_PATH`, `FREERTOS_PORT`,
`FREERTOS_CONFIG_DIR`). In an IDE, a missing `FreeRTOS.h` stops the compile with an
`#error` that names the include paths to add. `FreeRTOSConfig.h` needs `configUSE_MUTEXES 1`
and `configUSE_COUNTING_SEMAPHORES 1`. SMP builds also need `configUSE_CORE_AFFINITY 1` for
`RunOn`.

**Your application starts the scheduler.** Run MicroCS (`mcs_runtime_run`, or your own VM)
from a task, then call `vTaskStartScheduler()`. `ports/rp2/example/main.c` shows how.

## Zephyr

`CONFIG_MICROCS_THREADS=y` selects `CONFIG_DYNAMIC_THREAD` and
`CONFIG_DYNAMIC_THREAD_ALLOC`. On SMP boards it also implies `CONFIG_SCHED_CPU_MASK`, which
`RunOn` needs. Thread stacks come from the system heap, so `CONFIG_HEAP_MEM_POOL_SIZE` must
hold `MCS_THREADS_MAX × MCS_THREAD_STACK`. The heap is the part to size for your board:

```
west build -b <board> ports/zephyr/example -- -DEXTRA_CONF_FILE=overlay-threads.conf
```

## Tests

`make threads-test` (part of `make test`) runs the C# suite and the runtime test on POSIX
threads. `make tsan-test` runs them under ThreadSanitizer. `make freertos-test` runs the
runtime test as a task of the FreeRTOS POSIX simulator port, and checks the error message
for missing headers.
