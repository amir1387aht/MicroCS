# Standalone firmware: REPL, runtime and script manager

With `mcs_runtime` (modules/runtime, `MCS_ENABLE_RUNTIME`) a board behaves like a
MicroPython device, but in C#: you get an interactive prompt on the console, a filesystem
with boot scripts, background jobs, interrupt callbacks and a protocol to upload programs
from the PC — without reflashing.

## 1. The firmware

Your `main()` provides only the board glue — a heap, a console, time and the HAL table from
a port:

```c
#include "mcs_runtime.h"
#include "mcs_port_rp2.h"                 /* or stm32 / esp32 / zephyr / arduino */

static uint8_t heap[160 * 1024];
static mcs_runtime_t rt;
static mcs_hal_t hal;

int main(void) {
    stdio_init_all();
    mcs_rp2_cfg_t pins = MCS_RP2_CFG_DEFAULT;
    mcs_rp2_hal_init(&hal, &pins);

    mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
    cfg.heap = heap;  cfg.heap_size = sizeof heap;
    cfg.ramfs_size = 32 * 1024;           /* RAM disk carved from the heap ... */
    /* cfg.fs_ops = &mcs_lfs_ops; cfg.fs_ctx = &my_lfs;   ... or LittleFS on flash */
    cfg.console = mcs_rp2_console_stdio();
    cfg.ticks = mcs_rp2_ticks;  cfg.delay = mcs_rp2_delay;
    cfg.hal = &hal;
    for (;;) mcs_runtime_run(&rt, &cfg);
}
```

| `mcs_runtime_cfg_t` field | Meaning |
|---|---|
| `heap`, `heap_size` | one static buffer for the VM (and the RAM disk) — no `malloc` |
| `fs_ops` + `fs_ctx` / `ramfs_size` | your filesystem (e.g. LittleFS) or a RAM disk; neither = no files |
| `console` | `mcs_transport_t { read, write, ud }` — UART, USB CDC, RTT, TCP… |
| `ticks`, `delay`, `time_ud` | millisecond clock and sleep |
| `mode` | `MCS_RUNTIME_REPL` (default, humans), `MCS_RUNTIME_SHELL` (machine protocol), `MCS_RUNTIME_HEADLESS` (no console input: app + jobs + callbacks only) |
| `echo` | echo typed characters (raw UART terminals: `true`) |
| `hal` | peripherals; `NULL` = none |
| `app_image`, `app_image_len` | optional built-in application (`mcs -C app.cs`), executed in place from flash |
| `run_boot_scripts` | run `/boot.cs`, `/jobs.cfg`, `/main.cs` (default `true`) |
| `setup(vm, ud)` | register your own C bindings |
| `idle(ud)` | called every loop: feed a hardware watchdog, yield to the RTOS |
| `stack_slots`, `max_frames`, `stdlib`, `time_limit_ms` | tuning; `time_limit_ms` caps every REPL line, job and callback |

Prefer your own main loop or an RTOS task? Call `mcs_runtime_start(&rt, &cfg)` once and
`mcs_runtime_step(&rt, timeout_ms)` whenever you like — it never blocks longer than asked.

Ready-made projects: [ports/rp2/example](../ports/rp2/example),
[ports/esp32/example](../ports/esp32/example), [ports/stm32/example_main.c](../ports/stm32/example_main.c),
[ports/zephyr/example](../ports/zephyr/example), [ports/arduino/examples/MicroCS_REPL](../ports/arduino/examples/MicroCS_REPL).
Try it on the PC first: `./mcs --repl --sim`.

## 2. Boot sequence

1. the built-in `app_image`, if any
2. `/boot.mcsb` or `/boot.cs`
3. `/jobs.cfg` → scheduler ([SCHEDULER.md](SCHEDULER.md))
4. `/main.mcsb` or `/main.cs`
5. the console (REPL prompt or `\x04OK` in machine mode)

A failing boot script is reported and the device still starts, so it always stays reachable.
Jobs, timers and interrupt callbacks keep running while the REPL waits for input.

## 3. The REPL

```text
MicroCS 1.4.0 C# REPL. .help for commands, Ctrl-E paste mode, Ctrl-A machine mode.
> var adc = ADC.ReadMillivolts(0);
> adc
1650
> for (int i = 0; i < 3; i++) {
...   Console.Write(i);
... }
012
> class Led { public static void Blink(Pin p) => p.Toggle(); }
> .ls
```

* Statements run as you type them; a bare expression is printed.
* Unfinished blocks continue on `... ` lines. Variables, functions and classes stay defined.
* **Ctrl-C** cancels the current input or stops a running script; **Ctrl-E** paste mode
  (paste a whole file, **Ctrl-D** runs it); **Ctrl-A** switches to the machine protocol.
* Dot commands: `.ls [dir]`, `.cat <file>`, `.run <file>`, `.rm <file>`, `.mem`, `.info`,
  `.jobs`, `.clear` (reset all definitions), `.help`, `.exit` (machine mode).

## 4. Machine protocol (tools and IDEs)

One command per line; every reply ends with a status line starting with EOT (0x04):
`\x04OK` or `\x04ERR <message>`. Script output appears before the status line.

| Command | Effect |
|---|---|
| `ls [dir]` | `f <size> <name>` / `d - <name>` lines |
| `cat <f>` | print a file |
| `put <f> <len>` | reply `\x04READY`, then send exactly `<len>` raw bytes; written to `<f>.part` and renamed, so an interrupted or over-quota upload leaves the old file intact |
| `get <f>` | `\x04DATA <len>`, raw bytes, `\x04OK` |
| `rm`, `mkdir`, `mv` | file management |
| `run <f>` | run a source or image file (NOEXEC mounts refused) |
| `exec <code>` | compile + run one line |
| `jobs`, `every <ms> <f>`, `after <ms> <f>`, `cancel <id>` | scheduler |
| `mem`, `info`, `help` | diagnostics |
| `repl` | back to the interactive prompt |
| `quit` | close the session |

A 0x03 byte (Ctrl-C) received while a script runs aborts it at the next safe point.

## 5. From the PC: `tools/mcs_remote.py`

Python standard library only. `--port /dev/ttyUSB0 [--baud 115200]` for a serial port, or
`--exec "<command>"` to talk to a local process (used by the tests). Commands chain with `+`:

```sh
python3 tools/mcs_remote.py --port /dev/ttyACM0 put app.cs /main.cs + run /main.cs + ls
python3 tools/mcs_remote.py --port /dev/ttyACM0 get /log.csv log.csv
python3 tools/mcs_remote.py --port /dev/ttyACM0 repl          # interactive terminal, Ctrl-] quits
python3 tools/mcs_remote.py --exec "./mcs --shell --sim --ramfs 65536" put app.cs /app.cs + run /app.cs
```

The tool sends Ctrl-A first, so it works whether the device sits in the REPL or in machine mode.

## 6. Without the runtime

The shell can also be wired by hand when you already own a VM:

```c
mcs_transport_t t = { uart_read, uart_write, NULL };
mcs_shell_t sh;
mcs_shell_init(&sh, vm, &vfs, &sched, t);
sh.repl = true;                         /* start at the C# prompt (false = machine protocol) */
mcs_shell_boot(&sh, true);
mcs_shell_run(&sh);                     /* or mcs_shell_step() from your loop */
```

## Validation

`tests/test_shell.py` (host `./mcs --shell`: boot order, binary uploads, quota, atomic
replace, Ctrl-C, scheduler commands), `tests/test_cm_shell.py` (the same protocol against the
Cortex-M33 firmware built by `make cm-check`) and the `test_runtime` unit test (setup hook, REPL
lines, GPIO interrupt callbacks while idle, Ctrl-A switch to the machine protocol, shutdown).
