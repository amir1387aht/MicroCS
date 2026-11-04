# MicroCS on Apache NuttX

MicroCS as a NuttX application: the NSH command **`mcs`** (REPL, script runner, device shell on the
NuttX console) plus the MicroCS library, and optional **real C# threads** on NuttX pthreads
(`Thread.Run`, `RunOn(cpu, …)`, `Every`, `Worker`, `Channel`; see [docs/THREADS.md](../../docs/THREADS.md)).

This port is the NuttX counterpart of the other optional OS backends (FreeRTOS, Zephyr, POSIX):
`modules/os/mcs_os.c` has an `MCS_OS_NUTTX` backend, selected automatically by the app
(`-DMCS_OS=MCS_OS_NUTTX`) and also by `MCS_OS_AUTO` when `__NuttX__` is defined.

| | |
|---|---|
| Verified | 🧪 NuttX **12.11.0** simulator, single-core and SMP: CMake app, thread suite and bounded native-stack regression (`make nuttx-test`); single-core native GPIO simulator test; Make-built app and GPIO smoke test; minimal simulator with pthreads disabled |
| Not verified | physical boards, NuttX older than 12, and other NuttX configurations |
| Not there yet | 🗓️ Native GPIO interrupts, I2C, SPI, UART and other peripheral drivers; native networking |

## Quick start

```sh
# 1. put the app in the NuttX apps tree (a symlink is enough)
ln -s /path/to/MicroCS/ports/nuttx  apps/interpreters/microcs

# 2. enable it: Application Configuration > Interpreters > MicroCS C# interpreter
#    (and "Real threads on NuttX pthreads" for C# threads)
cd nuttx
tools/configure.sh sim:nsh          # or your board:config
kconfig-tweak --enable CONFIG_INTERPRETERS_MICROCS
kconfig-tweak --enable CONFIG_INTERPRETERS_MICROCS_THREADS
make olddefconfig && make           # or: cmake -B build -DBOARD_CONFIG=sim:nsh -GNinja && cmake --build build

# 3. on the NuttX shell
nsh> mcs --version
nsh> mcs -e "Console.WriteLine(Thread.Os);"
nsh> mcs /data/hello.cs
nsh> mcs                            # REPL / shell on the console
```

`mcs` takes the same arguments as the host CLI (`ports/unix/main.c`): `--fs <dir>` sets the root of
the C# `File` API (default: the current directory), `-e` evaluates a snippet, and so on.
Files come from the NuttX VFS: LittleFS, FAT, SmartFS, ROMFS, hostfs on the simulator, …

## Configuration (`Application Configuration > Interpreters > MicroCS`)

| Option | Default | |
|---|---|---|
| `INTERPRETERS_MICROCS_PROGNAME` | `mcs` | NSH built-in name |
| `INTERPRETERS_MICROCS_PRIORITY` | 100 | task priority of `mcs` |
| `INTERPRETERS_MICROCS_STACKSIZE` | 16384 | stack of the `mcs` task; stack needs depend on script and board (see below) |
| profile | full | `embedded`, `mcu`, `lowram`, `min` = `MCS_PROFILE_*` ([docs/CONFIGURATION.md](../../docs/CONFIGURATION.md)) |
| `INTERPRETERS_MICROCS_USER_CONFIG_FILE` | – | path of a `mcs_user_config.h` that sets any `MCS_*` option |
| `INTERPRETERS_MICROCS_THREADS` | off | C# threads on pthreads (needs `!DISABLE_PTHREAD`) |
| `INTERPRETERS_MICROCS_GPIO` | off | 🧪 Enable native GPIO character devices (needs `DEV_GPIO`) |

Requirements: floating point and `long long` printf support (selected automatically) and a **full
math library** — the C# `Math` class uses `hypot`, `fma`, `remainder`, `cbrt`, `log2`, … NuttX's small
built-in libm does not have all of them; use the toolchain's libm, newlib or OpenLibm
(*Library Routines > Math library*).

NuttX printf limits double output to 15 significant digits. `MCS_OWN_DTOA=1` (the NuttX
default) supplies exact digits for shortest-round-trip double formatting and explicit fixed,
number, percent, scientific and custom decimal formats. These use extra computation and stack;
the existing output buffer limits still apply to unusually large precision requests. Networking
APIs are disabled by default on NuttX and native networking has not been validated.

For a simulator build with `CONFIG_DISABLE_PTHREAD=y`, NuttX 12.11.0 also needed parent/child
scheduling, NSH pipelines and `ostest` disabled to avoid unrelated NuttX build errors. In this
configuration the nested-callback native stack guard below is unavailable.

### Native GPIO (experimental)

Enable `CONFIG_DEV_GPIO` and `CONFIG_INTERPRETERS_MICROCS_GPIO`. The board must register and
configure `/dev/gpioN` itself; MicroCS pin `N` refers to that device, not necessarily a physical
pin number. `Gpio.Mode` validates the device's existing type and rejects mismatches; it does not
configure pin mux or direction. Reads and writes open the device and use NuttX GPIO ioctls, so they
are not interrupt-safe. Interrupts and rotary encoders are not provided by this backend. With
`--sim`, the simulated HAL is used instead. Test and validate the device permissions, wiring and
modes on the target board.

### Stack sizing

The compiler and native calls use the task stack; the 16 KB default is not a guarantee for arbitrary
scripts. Increase `INTERPRETERS_MICROCS_STACKSIZE` and measure on the target. When NuttX pthread
stack bounds are available, a best-effort guard checks stack space on nested interpreter callbacks
and raises `StackOverflowException` before the configured reserve is exhausted. Set
`MCS_NUTTX_STACK_RESERVE` in the user config (default 16384 bytes, capped at half the reported
stack size; 0 disables it). The guard can limit recursion earlier on small stacks; it does not cover
compiler recursion or all native calls, and cannot protect a task if stack bounds are unavailable.

On the 12.11.0 simulator, the focused nested-callback regression catches the exception and continues
with the default configuration. Previously, `tests/t26_robustness.cs` crashed with the 16 KB
default and repeated XIP runs could crash with 64 KB; source, image and ten repeated XIP runs passed
with 256 KB. These measurements are simulator-specific, not a recommended stack size or a guarantee
for a board.

## Threads on NuttX

* Each C# thread is a pthread with its own VM and heap. Stacks and heaps come from the NuttX heap:
  budget `MCS_THREADS_MAX × (MCS_THREAD_STACK + MCS_THREAD_HEAP)`.
* `RunOn(cpu, …)` pins the thread with the pthread CPU affinity on `CONFIG_SMP=y` builds; with one
  CPU `Thread.Cores` is 1 and `RunOn(0, …)` works, other cores raise `ArgumentOutOfRangeException`.
* Thread priorities are relative to the creating thread (`+n` is n levels more urgent), clamped to
  the range the scheduler policy allows (`sched_get_priority_min/max`).
* **Scheduling:** NuttX schedules threads FIFO unless `CONFIG_RR_INTERVAL` is set, so a script that
  spins would starve the shell at the same priority. Without round-robin the default
  `MCS_THREAD_PRIORITY` is therefore -1 on NuttX (workers run one level below the creator); with
  `CONFIG_RR_INTERVAL > 0` they use `SCHED_RR` at the creator's priority. Override with
  `MCS_THREAD_PRIORITY` in the user config.
* The HAL event queue lock is NuttX's `enter_critical_section()` (nested up to 8 deep), so it may be
  used from interrupt handlers as well.

## Test it on the simulator

```sh
make nuttx-test                 # sim:nsh, 1 CPU
NUTTX_SMP=1 make nuttx-test     # plus an SMP simulator: Thread.Cores > 1, RunOn pinning
```

`tools/nuttx_check.sh` downloads NuttX and apps (`NUTTX_REF`, default `nuttx-12.11.0`) into
`build/nuttx`, links this folder as `apps/interpreters/microcs`, builds `sim:nsh` with MicroCS and
threads, mounts `tests/` through hostfs and runs `tests/threads/t_threads.cs`, comparing the output
with `tests/threads/t_threads.out` (excluding the interleaved thread exception diagnostics). The
single-core run also checks GPIO character devices and the nested-callback stack guard in source,
image and XIP modes. It needs `git`, `cmake`, `ninja`, `genromfs`, `xxd` and
`pip install kconfiglib`. The same job runs in CI.

By hand, on any simulator build that has `CONFIG_FS_HOSTFS=y`:

```
nsh> mount -t hostfs -o fs=/path/to/MicroCS/tests /host
nsh> mcs --fs /host/threads /host/threads/t_threads.cs
```

## Layout

| File | |
|---|---|
| `Kconfig` | options above |
| `Make.defs`, `Makefile` | NuttX Make build: `src/*.c` + `modules/*/*.c` of the repo, `ports/unix/main.c` as the `mcs` main |
| `CMakeLists.txt` | the same for the NuttX CMake build (`ports/unix/main.c` is the first source so its `main` becomes the built-in entry) |
| `mcs_port_nuttx.c`, `.h` | opt-in GPIO character-device HAL |

Nothing is copied: the app builds the repository's sources in place, so a `git pull` of MicroCS
is all an update takes.

## Porting notes

* `modules/os/mcs_os.c` `MCS_OS_NUTTX`: pthreads (`pthread_attr_setstacksize`, explicit scheduling
  attributes, `pthread_attr_setaffinity_np`, `pthread_setname_np` when task names are enabled),
  pthread mutexes, `sem_t` semaphores with `sem_clockwait` on `CLOCK_MONOTONIC`, `malloc`/`free`,
  `clock_gettime` ticks.
* `modules/fs/mcs_vfs_posix.c` is the file system backend (NuttX has the POSIX file API).
* NuttX defines a `CODE` macro in `<nuttx/compiler.h>`; the shell module's internal constants are
  `SC_*` for that reason.
* NuttX's `main` of an app is renamed per built-in; `ports/unix/main.c` is shared, with
  `getcwd()` as the default `--fs` root because `"."` does not resolve inside a NuttX task.
