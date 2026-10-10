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
MicroCS 1.11.0 C# REPL. .help for commands, Ctrl-E paste mode, Ctrl-A machine mode.
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
  `.jobs`, `.cancel <id>|all|scripts|files`, `.clear` (reset all definitions), `.help`,
  `.exit` (machine mode).
* **Jobs outlive their script.** `Scheduler.Every` / `Scheduler.After` hand a delegate to the
  runtime's scheduler; the compiled code lives in RAM, so the job keeps running after the
  script returns — and after its `.cs` file is deleted — until `.cancel` / `cancel`,
  `Scheduler.Cancel(id)` / `Scheduler.CancelAll()`, or a reset. Only `/jobs.cfg` entries come
  back after a reset. MicroCS Studio cancels the jobs left by earlier scripts before each Run
  (⋯ → *Stop script jobs before each Run*) and has ⋯ → *Stop all jobs*.

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
| `jobs`, `every <ms> <f>`, `after <ms> <f>` | scheduler: list jobs (`<id> <state> every\|once <ms> runs=<n> <file or <delegate>>`), add file jobs |
| `cancel <id>` / `cancel all` / `cancel scripts` / `cancel files` | cancel one job, every job, the delegate jobs scripts started, or the file jobs (`jobs.cfg`, `every`/`after`); bulk forms print `cancelled <n>` |
| `df [dir]` | `<mount> <format> N KB total, N KB used, N KB free` per mount (or for the one holding `dir`) |
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
python tools\mcs_remote.py --port COM5 repl                   # Windows (no extra packages; uses pyserial if installed)
python3 tools/mcs_remote.py --exec "./mcs --shell --sim --ramfs 65536" put app.cs /app.cs + run /app.cs
```

The tool sends Ctrl-A first, so it works whether the device sits in the REPL or in machine mode.

## 6. In the browser: MicroCS Studio

**https://amir1387aht.github.io/MicroCS/** — nothing to install. Open it in **Chrome or Edge**
on a desktop (they have [Web Serial](https://developer.mozilla.org/docs/Web/API/Web_Serial_API)),
click **Connect** and pick the board's port. It speaks the protocol above, like `mcs_remote.py`.

Offline, open [`tools/studio/index.html`](../tools/studio/index.html) from a clone or the
release zip — double-click is enough, no server. The page is plain HTML/CSS/JS split by job
(`studio.css` look, `studio.js` app, `templates.js` templates and snippets, `docs.js`
signatures, `lang.js` language service, `api.js` member names, see
[tools/studio/README.md](../tools/studio/README.md)); the published copy is
updated from `main` by `.github/workflows/pages.yml`. The look follows Visual Studio 2022
Dark (Light via the theme button).

* **Device files** — browse folders, upload (button or drag & drop, several files at once),
  download, rename/move, duplicate, delete (folders recursively), create files and folders,
  *Run at boot* (copies a script to `/main.cs`). The bar at the bottom shows free space
  (`df`) and the VM heap (`mem`).
* **Editor** — tabs, C# highlighting with Visual Studio's colours, auto-indent and bracket
  pairs, `Ctrl+/` comments. `Ctrl+S` saves to the device; `F5` / `Ctrl+Enter` saves and runs,
  streaming the output; compile errors and exceptions mark the line and are clickable in the
  console. `Ctrl+Shift+Enter` runs only the selection. Binary files (`.mcsb`) open in a hex
  view. Find / replace (`Ctrl+F` / `Ctrl+H`, match case, whole word, regex), go to line
  (`Ctrl+G`), format document (`Shift+Alt+F`), move / copy lines (`Alt+↑↓`), bracket matching.
  Unsaved files are kept in the browser and come back after a reload.
* **IntelliSense** — every library member has a signature and a one-line doc
  ([`docs.js`](../tools/studio/docs.js)). Typing `GPIO.` lists the members with their return
  types and a doc panel; typing `(` or `,` shows parameter info with the active parameter,
  `↑`/`↓` for overloads and hints such as the valid `GPIO` modes; hovering a name shows its
  signature or type; `using System.C` completes namespaces. Types are inferred from
  `var x = new T()`, declarations, method return types (`var found = I2C.Scan(0)` →
  `List<int>`), `foreach` and chains like `s.Trim().Split(',')`; the classes, fields,
  methods and local functions of the open file are completed too. 50 snippets such as `cw`,
  `for`, `try`, `pin`, `every`, `i2cdev`.
* **Templates** (`Alt+T`) — 127 ready-to-run templates and examples in an *Add New Item* dialog
  with categories, search and preview: getting started, **boot scripts** (`boot.cs`,
  `main.cs` with safe mode / settings / watchdog, `jobs.cfg` and job scripts), **scheduler**
  (`Every`/`After`/`Cancel`, failure policy, state machines, debouncing, timeouts), every
  API — GPIO, PWM, ADC/DAC, UART, I2C, SPI, timers, events, watchdog, RTC, I2S, CAN, QSPI,
  files — plus **sensors** (MPU6050, DS3231, ADS1115, INA219, AHT20/SHT31, DHT22, BH1750),
  **displays** (SSD1306, MAX7219, LCD1602, TM1637), **motors** (H-bridge, steppers, servo),
  **input devices** (rotary encoder, keypad, button gestures), **control** (PID, filters,
  Kalman), **protocols** (CRC, Modbus RTU, NMEA, Base64), games, benchmarks and C# language
  features. *Add* opens one as a new file, *Insert at cursor* pastes it into the current one.
  CI compiles and runs each one on the simulated board (`tests/studio/test_templates.js`).
* **Console** — the C# REPL (variables survive between lines and file operations; `↑`/`↓`
  history, `Shift+Enter` for multi-line input) or raw shell commands (`jobs`, `cat`, `help` …).
  Output of background jobs and the boot log appear here too; `Ctrl+C` stops a script.
  **Plotter** charts what a script prints: `Console.WriteLine($"temp:{t} hum:{h}")` gives
  named series, a line of plain numbers gives `v1`, `v2` … (click to pause).
* **Reset board** pulses EN through RTS like esptool. Boards on native USB (ESP32-S3/C3/C6
  USB-Serial-JTAG, RP2040 CDC) may disappear while resetting; the page reconnects
  automatically when they return.
* **Connecting** does not touch DTR/RTS (like `mcs_remote.py`; changing them can reset an
  ESP32 through its auto-reset circuit), sends Ctrl-C + Ctrl-A and waits for the `OK` status.
  A board that is still booting gets up to 14 s and its boot log is shown live. Only a board
  that reports the ESP32 ROM bootloader (`waiting for download`) is reset into the firmware.
  If it still fails, the message says how many bytes arrived: nothing (wrong port, another
  program holds it), garbled text (wrong baud rate), or a boot log without a MicroCS answer
  (the firmware's console is on another port, or it crashed — the log shows it). Boards that
  need DTR/RTS low: ⋯ → *Release DTR/RTS on connect*.

Uploads are written to `<name>.part` and renamed, so an interrupted upload keeps the old file.
If an upload stalls (a UART bridge losing bytes while the flash erases), it is retried in small
paced chunks. File names cannot contain spaces (the protocol splits arguments on spaces).
`tests/studio/test_studio.js` drives the page in a headless browser against `mcs --repl --echo`.

## 7. Without the runtime

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
