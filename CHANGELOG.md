# Changelog

All notable changes. Versions follow `MCS_VERSION_*` in `include/mcs.h`.

## 1.11.0 — U8g2 displays (optional)

* **C# `U8g2` and `U8x8` on olikraus' u8g2 (2.37.1, the latest release)** for 365 monochrome
  OLED / LCD / e-paper displays (SSD1306, SH1106, SSD1309, ST7920, ST7565, PCD8544, KS0108, …)
  on hardware I²C / SPI, bit-banged I²C / SPI / 3-wire SPI, 8080 / 6800 parallel and KS0108 pins.
  The wrapper covers both APIs: full-buffer and page mode (`Draw(() => …)`,
  `FirstPage`/`NextPage`), every drawing call, text and UTF-8 in any font, metrics, rotation,
  clipping, bitmaps, buttons, the three u8g2 menus (buttons on pins or `SetMenuInput(() => …)`),
  u8log terminals, Arduino-style `Print`, buffer export (`GetBuffer`, PBM, XBM, `Dump`), and the
  U8x8 tile API. Display names work as u8g2 writes them, including Arduino class names.
  Errors say what is wrong (`no display answers at I2C address 0x3C on bus 0`, the list of
  compiled-in displays / fonts).
* **Optional, never in the release firmware.** u8g2 is not bundled. CMake (`-DMICROCS_U8G2=ON`, or
  `MCS_ENABLE_U8G2 1` in `mcs_user_config.h`), ESP-IDF and Zephyr (`CONFIG_MICROCS_U8G2`), Arduino
  (`make_arduino.py --u8g2` + the U8g2 library), PlatformIO and make (`make mcs-u8g2`) find the
  sources in `MICROCS_U8G2_DIR` or the usual places. Otherwise they stop with instructions, or
  download 2.37.1 once with `MICROCS_U8G2_DOWNLOAD=ON` / `make fetch-u8g2`.
* **Displays and fonts are chosen before the build** (`MCS_U8G2_DISPLAYS`, `MCS_U8G2_FONTS`,
  `MCS_U8X8_FONTS`, or `MICROCS_U8G2_DISPLAYS` / `_FONTS` lists that are checked at configure
  time). Only those are linked; the defaults are four common OLEDs and six small fonts (≈ 7 KB).
  **Every other font still works**: `tools/u8g2.py extract` writes it as a file, and
  `SetFont("name")` loads `/fonts/u8g2_font_name.bin` at run time (or a path / `byte[]`).
* `tools/u8g2.py`: `where`, `fetch`, `displays`, `fonts` (with sizes), `extract`, and `config`
  (prints the lines for `mcs_user_config.h`, CMake, make and PlatformIO).
* **Simulator OLED:** `mcs --oled[=sh1106,128x32,0x3D]` emulates an SSD1306 / SH1106 on the
  simulated I²C bus and draws each frame in the terminal. `make u8g2-test` compares the frames of
  the test scripts and smoke-runs `examples/u8g2/` (7 examples). CI runs it, builds with CMake,
  and builds a Pico firmware and the Arduino library with u8g2.
* HAL: `mcs_hal_spi_config(vm, bus, cfg)` reconfigures an open SPI bus (mode, clock) for drivers.
* Docs: [docs/U8G2.md](docs/U8G2.md).

## 1.10.0 — real OS threads (optional): FreeRTOS, Zephyr, POSIX, second core

* **C# on real OS threads, opt-in.** Choose an OS (ESP-IDF menuconfig *Real threads*,
  Zephyr `CONFIG_MICROCS_THREADS=y`, CMake `-DMICROCS_OS=freertos|posix`, `make OS=posix`,
  or `MCS_OS` in `mcs_user_config.h`) and scripts and functions run on their own OS tasks,
  in parallel: `Thread.Start(path [, core [, heapKB]])`, `Thread.Run(path, fn, args…)`,
  `Thread.RunOn(core, path, fn, args…)` for the **second core of the ESP32 / ESP32-S3 /
  RP2040** (FreeRTOS SMP) and SMP Zephyr boards, and `Thread.Every(ms, …)` for OS-timed periodic
  work. A `Worker` has `Join`, `Stop`, `Result`, `State` and `Error`; named `Channel`s copy
  values between threads. Each thread gets its own VM and heap. The filesystem, console and
  peripherals are shared under locks. Interrupt callbacks stay with the main VM.
* **The scheduler uses the OS too:** with an OS chosen, `jobs.cfg` file jobs run on their own
  OS thread, timed by the OS (`MCS_SCHED_THREADS`, default on with threads). `startup` jobs and
  jobs marked `main` stay on the main VM; when no thread slot or heap is free, a job falls back
  to the main VM. Per job: `core=`, `prio=`, `stack=`, `heap=` (e.g. `every 10ms /control.cs
  core=1 heap=24k prio=1`). Without an OS these options are ignored and the job is polled as
  before. `jobs` / `.jobs` show thread jobs.
* **Release firmware runs on FreeRTOS:** the ready-to-flash ESP32 images are built with
  threads on ESP-IDF's FreeRTOS, and the Pico / Pico 2 / RP2040-Zero `.uf2` files with
  Raspberry Pi's FreeRTOS SMP kernel (the RTOS pico-examples uses; pico-sdk itself has none),
  so `Thread.RunOn(1, …)` uses the second core out of the box. STM32 images stay bare-metal.
* **Headers found or asked for:** CMake picks up a `freertos_kernel` / `FreeRTOS-Kernel` /
  CubeMX target, `MICROCS_FREERTOS_PATH` + `_PORT` + `_CONFIG_DIR` or `FREERTOS_KERNEL_PATH`, or
  a FreeRTOS-Kernel folder in the project. If none is found, configure stops and names the
  variable to set. `make OS=freertos` and IDE builds stop the same way.
* `ports/rp2/example`: `-DMICROCS_OS=freertos -DFREERTOS_KERNEL_PATH=…` (Raspberry Pi's
  FreeRTOS-Kernel fork) runs the REPL on core 0 under FreeRTOS SMP and leaves core 1 for C#,
  on RP2040 and RP2350. `ports/zephyr/example/overlay-threads.conf`.
* C API (`mcs_threads.h`, `mcs_os.h`): `mcs_thread_start/stop/join/info`. `mcs_runtime` sets
  threads up by itself (`cfg.thread_setup`, `cfg.thread_heap`, `cfg.thread_stack`).
  Filesystems can be locked (`mcs_vfs_t.lock`). Worker VMs can open the HAL without callbacks
  (`mcs_hal_open_lib_ex`).
* **No OS = no change:** `MCS_OS_NONE` is the default. Without an OS, no thread code is
  compiled, and the core gains only `Thread.Cores` (1), `Thread.CurrentCore` (0) and
  `Thread.Os` (`"None"`).
* Core: the parser and `OrderBy().ThenBy()` no longer use static buffers, so separate VMs can
  run at the same time.
* Tests: `make threads-test` (in `make test`), `make tsan-test` (ThreadSanitizer),
  `make freertos-test` (the FreeRTOS POSIX simulator port), and CI builds of ESP32,
  ESP32-C3, Zephyr and RP2040 with threads.

## 1.9.1 — ready-to-flash STM32 firmware

* **STM32 firmware in every release:** `microcs-<version>-<board>.bin` and `.hex` for
  Nucleo-F401RE, Nucleo-F411RE, Nucleo-F446RE, Black Pill F411CE, Nucleo-G474RE, Nucleo-L476RG and
  Nucleo-H743ZI. Copy the `.bin` to the Nucleo's ST-LINK drive (Black Pill: USB DFU), open the
  virtual COM port at 115200 baud or MicroCS Studio and use the C# REPL — no CubeMX project or
  toolchain needed. Each image sets up the clock from the internal oscillator (no crystal
  needed), the console UART, `new Pin("LED")` and every GPIO, I2C1 on the Arduino D14/D15 pins,
  four PWM / `Servo` outputs on TIM3 and TinyFS in the chip's own flash (28 KB on F4, 105 KB on
  G474/L476, 307 KB on H743) for `/boot.cs`, `/main.cs` and uploads. Pins and flash layout:
  [ports/stm32/firmware/README.md](ports/stm32/firmware/README.md).
* `tools/build_stm32_firmware.sh <board>` builds them (downloads the STM32Cube HAL + CMSIS of
  the family); the board setup is one file, `ports/stm32/firmware/board.c`.
* CI builds the 7 images on every push and boots the F4 and H743 ones in the Renode emulator
  (`tools/renode_check.py`: REPL, `Hal.Board`, a file written to and read back from the internal
  flash, the LED pin).
* STM32 flash driver: `mcs_stm32_flash_init()` with an explicit address on F2/F4/F7 may now use a
  run of equal small sectors (e.g. the 16 KB sectors 1–3) when the linker script keeps them free
  and marks them with `__mcs_fs_flash_start` / `__mcs_fs_flash_end`.

## 1.9.0 — TinyFS on internal flash, Servo driver, complete config header

* **TinyFS, a built-in flash filesystem for the MCU's own flash:** STM32s (and any other chip)
  without an external NOR/NAND can keep `/boot.cs`, `/main.cs`, uploads and data files in a few KB
  of internal flash — e.g. 8 KB of a 64/128 KB F103. Part of MicroCS (`modules/fs/mcs_vfs_tinyfs.c`,
  MIT, no sources to fetch, ~5 KB of Thumb-2 code, ~1 KB of static RAM, no heap): a log of CRC'd
  records, power-fail safe (garbage collection ends with a commit record), wear levelling across
  the erase blocks, works from 2 erase blocks. Selected like LittleFS and YAFFS2:
  `MCS_ENABLE_TINYFS 1`, CMake `-DMICROCS_FS=tinyfs`, ESP-IDF menuconfig *TinyFS*
  (`CONFIG_MICROCS_FS_TINYFS`), Zephyr `overlay-tinyfs.conf` (`CONFIG_MICROCS_TINYFS`), Arduino
  `make_arduino.py --fs tinyfs`; `mcs_flashfs_mount(..., MCS_FLASHFS_TINYFS, ...)` or
  `MCS_FLASHFS_DEFAULT`. Options `MCS_TINYFS_MAX_FILES` / `_CHUNK` / `_HANDLES` / `_MAX_UNIT`.
* **Internal-flash size:** `MCS_INTFLASH_SIZE` (CMake `-DMICROCS_FS_SIZE=8192`) sets how much of
  the chip's flash holds files on STM32 (`MCS_STM32_FS_SIZE`) and RP2 (`MCS_RP2_FS_SIZE`). With
  TinyFS alone the STM32 driver uses each flash page as a block (`MCS_STM32_FS_BLOCK` overrides).
* **Flash port for new MCUs:** fill a `mcs_flash_port_t` (region size, erase unit, program unit,
  `read` / `write` / `erase`) and `mcs_intflash_init()` makes the `mcs_flash_t` that TinyFS,
  LittleFS and YAFFS2 use (`include/mcs_flash.h`, skeleton in `ports/template`). STM32, RP2040 /
  RP2350, ESP32 and Zephyr ship theirs.
* **`Servo` driver** (built-in `servo` driver, like `ws2812`): `new Servo(channel[, minUs, maxUs[,
  maxAngle]])`, `Angle` / `Write`, `Pulse` / `WritePulse`, eased `MoveTo(deg, ms)`, `Detach` /
  `Attach`. Default backend: the board's HAL PWM at 50 Hz on every port and the simulator; register
  `MCS_SERVO_DRIVER(ops, ctx)` for a PCA9685 or a servo bus. Off with `MCS_ENABLE_SERVO 0`, CMake
  `-DMICROCS_SERVO=OFF`, `CONFIG_MICROCS_SERVO=n`, `make_arduino.py --no-servo`,
  `MICROCS_SERVO := 0`. Studio: Servo templates (sweep, smooth motion, pan/tilt with two servos),
  a `servo` snippet, completions and docs.
* **Config header with the defaults set:** [`config/mcs_user_config.h`](config/mcs_user_config.h)
  now holds every option *set* to its default value (each in `#ifndef`, so `-D` and the build
  systems still win) — copying it enables it, then edit the values. Only the options detected per
  compiler / CPU / board stay commented. `tools/gen_config.py --profile lowram -o
  mcs_user_config.h` writes it with another profile's values (`--profile auto --ram-kb --flash-kb`,
  `-D NAME=VALUE`); MicroCS warns when the header's profile differs from the build's.
  `make_arduino.py --profile` uses it. `make test` checks the template against
  `include/mcs_config.h` (`gen_config.py --check`), `make check` builds every generated profile.
* **CI:** 18 popular STM32 boards (Blue Pill, Black Pill F401/F411, F4 / F429 Discovery, Nucleo
  F072/F446/F767/G071/G431/G474/H563/H743/L073/L432/L476/U575/WB55) compile in parallel jobs, each
  in 8 configurations at once (default, no ws2812/servo, no drivers, auto profile, internal flash +
  LittleFS / YAFFS2 / TinyFS) with the ST headers cached. `make tinyfs-test` (power cut at every
  flash operation) runs in `make test` and the flash-filesystem job.

## 1.8.0 — project config header

* **`mcs_user_config.h`, one header for every build option:** copy the new template
  [`config/mcs_user_config.h`](config/mcs_user_config.h) — every `MCS_*` option of the core,
  modules and ports, commented out with its default and a description — into your project and
  uncomment what you change. `include/mcs_config.h` includes it before anything else, so the
  library, the ports and the application all see it. It is found automatically when it is on the
  include path (`__has_include`); `-DMCS_USER_CONFIG_FILE="…"` names another file,
  `-DMCS_USER_CONFIG=1` forces it on compilers without `__has_include`, `-DMCS_USER_CONFIG=0`
  ignores it. Guide: [docs/CONFIGURATION.md](docs/CONFIGURATION.md).
* **Compiler options keep working and win:** precedence is `-D` (CMake, Kconfig, menuconfig,
  PlatformIO flags) → the header → the profile → the defaults.
* **Profiles by name:** `#define MCS_PROFILE MCS_PROFILE_LOWRAM` (or `-DMCS_PROFILE=…`) with
  `MCS_PROFILE_FULL/AUTO/EMBEDDED/MCU/LOWRAM/TINY/MIN/LINUX`. Build systems pass their choice as
  `MCS_DEFAULT_PROFILE`, so a profile in the header wins over CMake `MICROCS_PROFILE`, menuconfig or
  Kconfig. The old `-DMCS_USER_CONFIG_FILE='"profiles/mcs_profile_x.h"'` still works.
* **Build integrations find the header:** CMake (next to the top-level `CMakeLists.txt` or in its
  `include/`, `src/`, `main/`; `MICROCS_CONFIG=path|NONE`), ESP-IDF (project folder / `main/`,
  menuconfig *Project config header*), Zephyr (application folder, `include/`, `src/`;
  `CONFIG_MICROCS_USER_CONFIG_FILE`), PlatformIO (project `include/`), `microcs.mk`
  (`MICROCS_CONFIG`, `MICROCS_PROFILE` now accepts every profile), Arduino
  (`tools/make_arduino.py` writes `src/mcs_user_config.h`; new `--config` and `--profile`; it no
  longer patches `mcs_config.h`).
* **Ports:** the STM32 port reads `MCS_STM32_HAL_HEADER` from the header too; every port documents
  its options in the template.
* **Examples and tests:** `examples/lowram` is configured by its own `mcs_user_config.h`;
  `make test` checks discovery, `MCS_USER_CONFIG_FILE`, `MCS_USER_CONFIG=0/1` and the precedence
  (`tests/c/test_config.c`), CI repeats it through CMake, `make check` builds the unmodified
  template and the profiles by name.
* **Releases and CI:** the release also ships an ESP32-C2 image (40 MHz crystal); a manual run of
  the Release workflow rebuilds the current version and refreshes its files. Every CI run keeps
  the Pico `.uf2` and ESP32 `.bin` it built as downloadable artifacts (14 days).

## 1.7.0 — device drivers, optional LED strips, console games

* **Releases with ready-to-flash firmware:** `.github/workflows/release.yml` publishes a GitHub release
  for every new `MCS_VERSION_STRING` — `.uf2` for Pico / Pico 2 / RP2040-Zero, single-image `.bin`
  for ESP32 / -S3 / -C3 / -C6, the Linux `mcs` binary and the Arduino library zip.
* **Project:** README demo (`assets/demo.gif`, recorded by `tools/make_demo_gif.js`), social preview
  image (`assets/social-preview.png`), issue and pull request templates, `SECURITY.md`,
  `CODE_OF_CONDUCT.md`.
* **Studio templates:** four playable console games under *Fun and games* — Guess the number,
  Rock, paper, scissors, Tic-tac-toe (against a simple computer player) and Hangman.
* **Fix:** declaring a type again as an `enum` (or with a different kind) crashed the compiler; it
  is now the error `the type 'X' is already defined` (partial classes/structs/interfaces still merge).
* **Device drivers** (`include/mcs_driver.h`, `modules/drivers/`, [docs/DRIVERS.md](docs/DRIVERS.md)):
  a driver = a C# front end + a per-MCU backend (`ops` table + context), kept in a small registry
  (`mcs_driver_register` / `_register_default` / `_unregister` / `_find`). Firmware can add its own
  drivers or replace a built-in backend without editing MicroCS; C# `Drivers.Has(name)`,
  `Drivers.List`, and `Hal.Has` also answers for driver and class names. The template port shows a
  `ws2812` backend and a complete custom driver (`Buzzer`).
* **`LedStrip` is now the built-in `ws2812` driver and optional:** `MCS_ENABLE_WS2812` (CMake
  `-DMICROCS_WS2812=OFF` — on RP2 it also drops `hardware_pio`; ESP-IDF menuconfig and Zephyr
  `CONFIG_MICROCS_WS2812`; `make_arduino.py --no-ws2812`; `microcs.mk` `MICROCS_WS2812 := 0`).
  The unreleased `mcs_hal_t.ledstrip_write` hook is gone (the HAL API stays at v2); backends
  register `MCS_WS2812_DRIVER(&ops, ctx)`. `include/mcs_ledstrip_drivers.h` is now
  `include/mcs_ws2812_backends.h` (`mcs_ws2812_pio_write`, `mcs_ws2812_rmt_write`).
* **Filesystem sources are looked up, not downloaded behind your back:** `cmake/MicroCSFS.cmake`
  uses `MICROCS_LITTLEFS_DIR` / `MICROCS_YAFFS2_DIR` (CMake or environment), then a copy next to the
  project or MicroCS (`third_party/`, `lib/`, `external/`, `Middlewares/Third_Party/`, siblings, the
  west workspace, `make fetch-lfs` output), then an earlier download; it downloads only with
  `-DMICROCS_FS_DOWNLOAD=ON` / `MICROCS_FS_DOWNLOAD=1`, else stops with a message saying what to
  set. LittleFS must be v2.x. The Pico example now needs one of those for its default LittleFS.
* **Arduino:** `tools/make_arduino.py --fs littlefs|yaffs2 [--fs-dir DIR]` bundles a flash
  filesystem into the library (for `mcs_flashfs_mount` on any `mcs_flash_t`), `--define NAME=VALUE`
  sets build options in the packaged `mcs_config.h`.
* **Scheduler jobs run while a script sleeps:** `Thread.Sleep` in a main loop now runs due
  jobs (Scheduler.Every/After delegates and jobs.cfg scripts); before, a looping `/main.cs`
  starved every job. Nested script jobs keep the outer script's top-level variables intact.
  `startup /main.cs` in jobs.cfg no longer runs main twice.
* **No-space handling:** `File.WriteAllText` / `WriteAllBytes` / `AppendAllText` / `Copy` check
  free space first and throw `IOException` without touching the old file (it used to be
  truncated); the shell's `put` answers `ERR not enough space: N bytes, M free` before the data
  is sent; Studio refuses uploads / saves that do not fit.
* **Studio Reset / reconnect:** Reset reboots through the firmware (`Hal.Reset`, works on RP2040,
  STM32 and native-USB ESP32 boards without reset wiring), falling back to the RTS/DTR pulse,
  and stops a running script first. After a reset or unplug Studio keeps retrying to reopen the
  port for a minute instead of giving up after the first failed open, and tells you when
  connecting stopped a running script (e.g. a looping `/main.cs`).
* **Fix: `Console.ReadLine` on devices** — scripts run by the shell / `mcs_runtime` now read the
  line typed in the console (`mcs_shell_readline`: input queue, backspace, CR LF, echo). Before,
  it fell back to `fgets(stdin)`, which never saw the Studio's input and blocked Ctrl-C / Stop.
  Ctrl-C while waiting stops the script; the wait honours the time limit and idle callback.
* **Studio:** a console line typed while a script runs goes to the script (`Console.ReadLine`)
  instead of waiting for the script to end; Refresh / Run re-read open, unmodified files that
  changed on the device (e.g. a log the script appended to); double-click on a template = Add.
* `List<int>.Sort()` / `Array.Sort(int[])` sort in place without extra memory (was 16 bytes per
  element); the *CPU benchmark* template sizes fit a 160 KB heap (RP2040).
* **`LedStrip` — WS2812 / NeoPixel / SK6812 on any pin** (the `ws2812` driver, see *Device drivers* above): `new LedStrip(pin, count[, order])`, indexer, `SetPixel`, `Fill`, `Clear`, `Show`,
  `Brightness`, `LedStrip.Rgb` / `LedStrip.Hsv`, GRB / RGB / GRBW orders. Drivers: RP2040/RP2350 PIO,
  ESP32 RMT (all chips with RMT), STM32 cycle-timed bit-bang, Zephyr `led_strip` (alias `led-strip`),
  Arduino (ESP32 RMT, Arduino-Pico PIO, Adafruit_NeoPixel), simulator log. Pin name `"NEOPIXEL"`
  maps to the on-board RGB LED (RP2040-Zero GP16, ESP32-S3 GPIO48, C3/C6 GPIO8).
  Studio template *WS2812 / NeoPixel LED strip* now uses it; the SPI version stays as
  *WS2812 LED strip via SPI*.
* **Fix:** indexers (`this[int]`) on native classes whose members are registered lazily from ROM.

* **Studio template: WS2812 / NeoPixel LED strip** (Displays) — addressable RGB LEDs driven through
  SPI MOSI at 2.4 MHz (3 SPI bits per LED bit, timing done by the SPI hardware): colour wipe,
  rainbow, theater chase, brightness limit; up to 28 LEDs per `SPI.Write` (more with a larger
  `MCS_HAL_MAX_XFER`).

* **Files on internal flash in every port:** `mcs_flashfs_mount()` puts LittleFS or YAFFS2 on
  any `mcs_flash_t` in one call (formats a blank partition). New internal-flash drivers:
  `mcs_rp2_flash_init()` (RP2040/RP2350 QSPI flash via `flash_safe_execute`),
  `mcs_stm32_flash_init()` (every STM32 family: page and sector flash, ECC flash words,
  L0/L1 inverted erase), `mcs_esp32_partition_flash()` + `mcs_esp32_flash_fs()` and
  `mcs_zephyr_flash_area_init()`. The RP2 and STM32 examples now keep `/boot.cs`, `/main.cs` and
  uploads on flash. Build choice: CMake `-DMICROCS_FS=littlefs|yaffs2` (`cmake/MicroCSFS.cmake`
  downloads the sources), ESP-IDF menuconfig *Filesystem on the "storage" partition*, Zephyr
  `overlay-yaffs2.conf`. `mcs_flash_t.write_size` (program unit) sets LittleFS's `prog_size`.
  Tested on simulated L4-, H7- and RP2-like internal flash; CI builds Pico 2, ESP32-S3 and Zephyr
  with YAFFS2 and runs `native_sim` with both filesystems.
* **Byte utilities in the core library** (no HAL needed, `MCS_ENABLE_BYTES`): `Convert.ToBase64String`
  / `FromBase64String` / `ToHexString` / `ToHexStringLower` / `FromHexString`, `BinaryPrimitives`
  (`Read/Write{Int16,UInt16,Int32,UInt32,Int64}{Big,Little}Endian`), `BitConverter.ToString(bytes,
  start, length)`. `Encoding.UTF8/ASCII` and `BitConverter` moved from the HAL module into the core,
  so they work on every build (off in the `min` profile). Test `t18_utils` is checked against .NET 8.
* README: performance chart legend says "MicroCS 1.6"; footprint chart label fixed; status and
  roadmap updated (internal-flash filesystems done; USB mass-storage is a separate roadmap item).

* **Zephyr port completed:** files on LittleFS (`mcs_zephyr_fs_mount()` on `storage_partition`,
  or any mounted Zephyr filesystem through `mcs_zephyr_fs_ops`, e.g. FAT on SD), so `/boot.cs`,
  `/jobs.cfg`, `/main.cs` and Studio uploads persist; I2S (`mcs-i2sN` aliases, memory-slab
  streaming), two CAN buses (`mcs-can0/1`), hardware RTC (`rtc` alias), watchdog fallbacks
  (`wdt0`, `wdt`, `iwdg`), polled UART fallback for drivers without interrupts, USB CDC ACM
  console (`overlay-usb.conf` + `usb.overlay`), Kconfig options (`MICROCS_HEAP_SIZE`,
  `MICROCS_FS`, `MICROCS_RAMFS_SIZE`, `MICROCS_CONSOLE_ECHO`), board overlays for the nRF52840
  DK and the Pico, a west manifest (`ports/zephyr/west.yml`). Fixed: the example's
  `app.overlay` did not build (missing PWM include), `clock()` link error with newlib, `native_sim`
  profile detection (`CONFIG_SRAM_SIZE=0`), the host-only POSIX backend compiled into Zephyr.
  CI builds `native_sim`, nRF52840 DK (UART + USB), Pico and Nucleo-F429ZI with Zephyr 4.1 and
  runs `tests/zephyr/smoke.py` on `native_sim`.
* **Arduino port completed:** files on any `fs::FS` (ESP32 LittleFS / SPIFFS / FFat / SD /
  SD_MMC, RP2040 LittleFS / SDFS) or the Arduino SD library (`MCS_ARDUINO_FS`,
  `MCS_ARDUINO_SD_FS`, `mcs_arduino_fs_ops`); `MicroCS_REPL` is now a full device firmware
  (LittleFS, boot scripts, Studio), new `MicroCS_SD` example; ESP32 I2S, CAN (TWAI), task
  watchdog and calibrated `ADC.ReadMillivolts`; RP2040 I2S and watchdog; 12/16-bit PWM duty on
  ESP32 / RP2040 / Teensy; `Hal.UniqueId` on RP2040, nRF52, SAMD, STM32. CI compiles every
  example for ESP32, S3, C3, Pico, Pico 2 and Nano 33 BLE.
* **Jobs:** shell `cancel all|scripts|files`, REPL `.cancel`, C `mcs_sched_cancel_all()`;
  Studio stops the jobs earlier scripts left running before each Run (option) and has ⋯ →
  *Stop all jobs*. Docs explain that `Scheduler.Every` jobs outlive their script and file.
* **MicroCS Studio** (`tools/studio/`, online at https://amir1387aht.github.io/MicroCS/): a
  browser IDE over Web Serial — device file manager (upload/download/rename/delete, drag & drop,
  free space), C# editor with Visual Studio 2022 Dark colours, API completion and 40+ snippets
  that saves and runs on the device, 85 templates and examples (boot scripts, `jobs.cfg`,
  scheduler patterns, every HAL peripheral, files, C# features) and a REPL / shell console.
  Connecting recovers an ESP32 left in its ROM bootloader by the port's DTR/RTS lines. Tested
  in CI against the host shell (`mcs --repl --echo`, new `--echo` flag); every template is
  compiled and run on the simulated board.
* **Studio: connecting no longer resets the board.** Studio used to set DTR/RTS low right after
  opening the port; Chrome applies the two lines one after the other, which briefly gives
  DTR=0/RTS=1 - "hold EN low" on ESP32 auto-reset circuits and on the native USB-Serial-JTAG -
  so the board rebooted on every connect, and Studio then also reset it again when it did not
  answer at once. Now the lines are left as the browser opens them (like `mcs_remote.py`), a
  booting board gets up to 14 s (its boot log is shown live), only a board in the ROM download
  mode is reset (esptool order, one line per call), read errors are reported and survived, and
  the error message says what was received. Optional: ⋯ → *Release DTR/RTS on connect*.
* **Studio IntelliSense:** signatures and docs for the whole library (`docs.js`) and a small
  C# language service (`lang.js`): member lists with return types and a doc panel, parameter
  info while typing a call (active parameter, overloads with ↑/↓, hints such as
  `GPIO.Input, Output…`), hover tooltips, `using` namespace completion, type inference for
  `var x = new T()`, method return types (`I2C.Scan(0)` → `List<int>`), `foreach` variables,
  and the classes, fields, methods and local functions of the open file; camel-hump matching.
* **Studio editor:** find / replace (Ctrl+F / Ctrl+H, case / word / regex, F3), go to line
  (Ctrl+G), format document (Shift+Alt+F), move / copy lines (Alt+↑↓, Shift+Alt+↑↓), delete line
  (Ctrl+Shift+K), bracket matching and Ctrl+] jump; unsaved files survive a reload.
* **Studio serial plotter:** Console → *Plotter* charts the numbers a script prints
  (`temp:21.5 hum:40` or `12 34`), up to 8 series, click to pause.
* **Studio templates:** 125 (was 85) - new categories Sensors (MPU6050, DS3231, ADS1115, INA219,
  AHT20/SHT31, DHT22, BH1750, joystick), Displays (MAX7219, LCD1602, TM1637, OLED text),
  Motors and actuators (H-bridge, steppers, relay, smooth servo), Input devices (rotary encoder,
  keypad, button gestures, touch), Control and filters (PID, moving average / median / EMA,
  hysteresis, Kalman), Protocols and data (CRC-8/16/32, Modbus RTU, NMEA, Base64, command shell,
  JSON, ring buffer), Serial plotter demos, Fun and games, Benchmarks.
* Docs: the I2S direction constants are `I2S.Transmit` / `Receive` / `Duplex` (HAL.md said `Tx`/`Rx`).
* **ESP32: scripts on flash** — the example mounts LittleFS on the `storage` partition
  (`mcs_esp32_littlefs()`), so uploaded files survive resets and power cycles.
* **Free space**: C# `DriveInfo` (`new DriveInfo("/").AvailableFreeSpace`, `TotalSize`,
  `DriveFormat`, `DriveInfo.GetDrives()`), shell/`mcs_remote.py` command `df`, and
  `mcs_vfs_statfs()` with an optional `statfs` backend op (RAM, POSIX, LittleFS, YAFFS2,
  ESP32 LittleFS).

## 1.6.0 — fast bytecode images

Focus: a precompiled image is now clearly the fast path. Up to 1.5 an image held the same
bytecode the on-device compiler emits, so it only saved the compile step (a few ms) and was
larger than the source. Numbers: [PERFORMANCE.md](docs/PERFORMANCE.md#bytecode-images).

### Speed (emulated Cortex-M4F instructions, image run, 1.5 → 1.6)
- `fib` 2.43 → 1.43 M (1.7×), `loop` 15.0 → 6.70 M (2.2×), `objects` 5.64 → 3.19 M (1.8×),
  `sensor` 0.87 → 0.68 M (1.3×), `strings` 0.63 → 0.49 M (1.3×); `demo.cs` image 3.44 → 2.18 M
  on M4F and 4.54 → 2.96 M on M0. Host: fib(30) 73 → 41 ms, 10 M loop 322 → 95 ms.
- Images now run 1.1–2.0× faster than the same script from source and need about half the RAM
  peak (no compiler state): e.g. `fib` 31.4 → 15.1 KB, `sensor` 40.7 → 17.0 KB.

### Bytecode optimizer (`src/mcs_opt.c`)
- 71 **superinstructions** (opcodes 86–156, image version 2+): local/immediate/constant
  arithmetic with optional store, compare-and-branch on locals/immediates/constants,
  accumulate (`x += e`), `a[i]` / `a[i] = v` and `obj.f` / `obj.f = v` on locals,
  `return local`. Jump threading, loop rotation (one branch per iteration), dead-code removal.
- `mcs -c` / `mcs -C` / `mcs_compile_image*` optimize by default; `mcs -O0` and
  `MCS_IMAGE_NO_OPT` produce plain bytecode. Source run on the device is unchanged unless
  `MCS_OPTIMIZE_SOURCE=1`.
- New switches `MCS_ENABLE_SUPEROPS` (default 1; **0 in `mcs_profile_min.h`**, which also turns
  `MCS_FIELD_CACHE` off) and
  `MCS_ENABLE_OPTIMIZER`. A VM without superinstructions rejects optimized images with a clear
  error; `examples/lowram/node_image.h` is now built with `-O0` so it runs on every profile.

### VM
- Stack pointer kept in a register; in-place int fast paths for arithmetic, shifts, compares,
  `a[i]`, `a[i] = v`; `array.Length` / `List.Count` / `string.Length` without lookup.
- `INVOKE` method inline cache per call site, constructor cache for `new C(...)`, direct
  closure entry, cheaper `RETURN`; caches are invalidated by a class epoch.

### Image format v3
- Varints, delta-coded line tables, every string stored once, f32 constants when exact.
  Images are 25–45 % smaller (host `demo.cs` 2763 → 1942 B; `bench/mcu/loop.cs` image 166 B for
  232 B of source). v1 and v2 images still load.

### Fixes
- Typed top-level variables used inside functions declared before them had the wrong type
  (`double ema = 0; void F() { ema = ema * 0.5; }` did integer arithmetic).
- Calls of hoisted top-level functions keep the declared return type (no extra conversion).
- `foreach (var p in list) acc = …` no longer emitted a stray tuple-names call.
- An interpolation format spec longer than 95 characters overflowed a compiler buffer (found by
  `tools/fuzz.py`; present since 1.0).
- Image loader: `FIELD`'s default-value operand is range-checked (found by `tools/fuzz.py --image`).
- CLI builds with `MCS_ENABLE_COMPILER=0` / `MCS_ENABLE_BYTECODE_SAVE=0` compile warning-free.

### Tooling
- `make mcu-bench` (`tools/mcu_bench.sh`, `bench/mcu/*.cs`, `ports/cortex-m/bench.c`):
  image vs `-O0` image vs source on emulated M4F/M0, outputs checked against the host.
- `make check`: +3 flag builds and +3 full-suite configurations (`MCS_ENABLE_SUPEROPS=0`,
  `MCS_OPTIMIZE_SOURCE=1`, switch dispatch); new `tests/t17_optimizer.cs`.

### Costs
- Flash: +8–11 KB on Cortex-M with superinstructions (`m0-runtime` 208.5 KB, `m4-full`
  253.1 KB); the `min` build (superinstructions and inline caches off) stays at 60.6 KB of 64 KB.
- Compiling an image on the PC takes longer (optimizer): host `demo.cs` 83 → 120 µs.

## 1.5.0 — 16 KB RAM / 64 KB flash, per-MCU configuration, flash filesystems

Focus: run on the smallest 32-bit parts (the same 16 KB RAM floor as MicroPython, but in
64 KB of flash), configure every port from the chip's resources, and put real filesystems
on external NOR and NAND flash. Nothing was removed: every reduction is a compile-time switch.

### Small targets
- **Lazy class tables** (`MCS_LAZY_CLASSES=1`, default): built-in and module classes are created
  the first time they are named; constants live in ROM (`mcs_const_t`, `MCS_CONST`,
  `mcs_register_consts`). A VM now starts in **1.7–8 KB** of heap instead of 13–25 KB
  (Cortex-M0: `m0-node` 13.0 → 1.7 KB, `m0-lowram` 19.9 → 5.0 KB, `m0-runtime` 25.1 → 7.8 KB).
- **`profiles/mcs_profile_min.h`**: a complete firmware in **64 KB of flash and 16 KB of RAM**
  (`m0-64k`: 60.9 KB, run by `make cm-check`). New switches, all default on:
  `MCS_ENABLE_STRING_EXTRA`, `MCS_ENABLE_ARRAY_EXTRA`, `MCS_ENABLE_STACK_QUEUE`,
  `MCS_ENABLE_CONVERT`, `MCS_ENABLE_DIAGNOSTICS`, `MCS_ENABLE_STDIO`, `MCS_ENABLE_MALLOC`;
  `MCS_TINY_PRINTF=1` uses the new built-in `snprintf` (`src/mcs_fmt.c`) instead of libc's.
  Flash cost of each switch: [LOW_RESOURCE.md](docs/LOW_RESOURCE.md#8a-fitting-64-kb-of-flash-profilesmcs_profile_minh).
- `examples/lowram` runs in a **16 KB** part (`m0-16k`: 12 KB pool, 11.2 KB peak); the firmware
  no longer needs `printf` and can report its C-stack high-water mark.
- `lowram` profile: 128 stack slots, 24 frames, 12 handlers, 16 roots.
- All profile headers are overridable (`#ifndef` guards), so `-D` flags win over a profile.
- `cfg.alloc_overhead` (+ `MCS_POOL_OVERHEAD`): the GC threshold and `heap_limit` count the
  allocator's per-block headers (set automatically for the built-in pool).

### Per-MCU configuration
- **`profiles/mcs_profile_auto.h`** picks min / tiny / lowram / mcu / embedded / default from
  `MCS_TARGET_RAM_KB` / `MCS_TARGET_FLASH_KB` (CMake `MICROCS_RAM_KB` / `MICROCS_FLASH_KB`),
  Zephyr `CONFIG_SRAM_SIZE` / `CONFIG_FLASH_SIZE`, or the STM32 / RP2040 / RP2350 / nRF52 /
  SAMD device macro. Board ports (`MCS_PORT_HAL=1`) keep the HAL classes on parts with
  ≥ 128 KB of flash. Default for CMake `MICROCS_PORT=stm32` and for Zephyr (new
  `MICROCS_PROFILE_*` Kconfig choice); ESP-IDF gets a menuconfig choice.
- **STM32 parts below 16 KB RAM or 64 KB flash are no longer supported**: their device macros
  (C011/C031/C051, F030x4–x8, F031, F038, F042, F048, F051, F058, F070x6, F100, F101x4–xB,
  F102, F103x4/x6, F301x6, F302x6, F303x6/x8, F328, F334, G030/G031/G041, L010x4–x8,
  L011–L063, L100/L151/L152 small variants, U031) stop the build
  (`include/profiles/mcs_target_stm32.h`; `MCS_ALLOW_SMALL_TARGET=1` overrides).
- CI compiles every STM32 family twice (default and auto profile, `PORT_CFLAGS`).

### ESP32-C2
- ESP32-C2 / ESP8684 support: embedded profile by default (`sdkconfig.defaults.esp32c2`),
  ROM nano-printf disabled (with a `#warning` if it is re-enabled with floats), LEDC clock
  from the 60 MHz PLL divider, hardware timer count clamped to the chip, adaptive VM heap in
  the example (128 KB on the C2, clamped to the largest free block), C2 pin map, 26 MHz
  crystal note. Built in CI.

### Flash filesystems (`modules/fs`)
- **`include/mcs_flash.h`** (`MCS_ENABLE_FLASH`): a NOR/NAND device description plus generic
  **SPI NOR** (JEDEC id, 3-/4-byte addressing, 4/64 KB erase, power-up unlock) and **SPI NAND**
  (W25N-style command set, on-die ECC status, bad-block markers, cached page) drivers, and
  `mcs_flash_hal_xfer` to run them over a MicroCS HAL SPI bus + GPIO chip select. ~2.6 KB.
- **LittleFS** on NOR and NAND: `mcs_lfs_flash_config()` fills an `lfs_config` for a
  partition; NAND bad blocks → `LFS_ERR_CORRUPT` (relocation), failing blocks get marked.
- **YAFFS2** backend `mcs_yaffs_ops` (`MCS_ENABLE_YAFFS`) + `mcs_yaffs_flash_dev()`: in-band
  tags on NOR and (by default) NAND, spare-area tags optional, full bad-block handling;
  optional single-threaded OS glue (`MCS_YAFFS_OSGLUE=1`). YAFFS2 is GPLv2 — not bundled.
- Tests on simulated chips with real command sets (`tests/c/flash_sim.h`): `make test`
  (drivers, 42 checks), `make lfs-test` (now also SPI NOR + SPI NAND with bad blocks),
  new `make yaffs-test` (downloads a pinned yaffs2 revision; NAND in-band + spare tags with a
  block wearing out, NOR). CI job "Flash filesystems".

### Fixes
- GC: the string intern table no longer doubles while dead strings are waiting for the next
  collection (it grows from the live count and defers growth near a collection), and it
  shrinks after a collection; this removed the collection storm on `m0-lowram`
  (now 3 collections) and made the 16 KB targets possible.
- GC: when the live data was close to ¾ of `heap_limit`, the next collection threshold
  landed at or below the bytes already allocated and the VM collected at every safepoint;
  it is now placed halfway to the limit. Allocator block headers were not counted, so a
  small pool could run out before the GC threshold was reached (`cfg.alloc_overhead`).
- Images: constant tables are allocated at their exact size when loading.
- `{0:F<huge>}` format precision could overflow an `int`; exponent formatting no longer uses
  `sprintf` (and cannot truncate).
- The compiler treated not-yet-created built-in classes as unknown names / interfaces.

### Docs
- New/updated: LOW_RESOURCE.md (16 KB / 64 KB, switch costs, auto profile), FILESYSTEM.md
  (flash layer, LittleFS vs YAFFS2), STM32 supported-parts list, ESP32-C2 section, footprint
  tables re-measured.

## 1.4.0 — hardware, ports and the REPL

Focus: run MicroCS on real boards with every peripheral, from any build system — either as a
library inside existing firmware or as a complete C# firmware with a REPL.

### Hardware API v2 (`modules/hal`)
- New classes: `Pin`, `I2cDevice`, `SpiDevice`, `DAC`, `Timer`, `I2S`, `QSPI`, `CAN` + `CanFrame`,
  `Watchdog`, `RTC`; many new members (`GPIO.OnChange/Off/PulseIn/Pin`, `UART.Open(port, baud,
  bits, parity, stop)`, `ReadLine`, `WriteLine`, `OnReceive`, `I2C.Scan/ReadRegister(s)/WriteRegister`,
  `SPI.Open(bus, hz, mode)`, `ADC.ReadMillivolts/ReadVoltage/ReadAverage`, `PWM.Servo/Tone/SetPulse/Stop`,
  `Hal.Poll/Run/Micros/DelayMicroseconds/Reset/UniqueId/CpuHz/OnEvent/Post/DroppedEvents`).
- Interrupts and timers: ISR-safe event ring (`mcs_hal_post`) or driver queue (`poll_event`);
  callbacks run in script context and adapt to their parameter count.
- Pin names (`"PA5"`, `"GPIO21"`, `"P0.13"`, `"LED"`) via `pin_lookup` / `mcs_hal_parse_pin`.
- `Encoding.UTF8`/`ASCII` and `BitConverter` helpers for packet work.
- Fixed: `GPIO.Write(pin, 0)` drove the pin high (an `int` 0 was treated as truthy).
- The simulator board gained interrupts, timers, DAC, I²S, QSPI flash, CAN and an MPU-6050-style
  register device. v1 board tables compile unchanged.

### Ports
- `ports/stm32` — every STM32Cube family (C0, F0–F7, G0, G4, H5, H7, L0–L5, U5, WB, WL), using
  CubeMX handles; compile-checked against 21 family/device combinations.
- `ports/esp32` — ESP-IDF 5.x for ESP32, S2, S3, C2, C3, C5, C6, H2, P4 (no Wi-Fi/BLE needed).
- `ports/rp2` — RP2040 and RP2350 with the pico-sdk; example firmware for Pico and Pico 2.
- `ports/zephyr` — devicetree-driven port + Zephyr module (beta).
- `ports/arduino` — any 32-bit Arduino core, Arduino library packager (beta).
- `ports/template` rewritten: every HAL member, both entry points.

### Firmware runtime and REPL
- `mcs_runtime` (`modules/runtime`): a complete firmware in one call — REPL, filesystem, boot
  scripts, scheduler, callbacks, upload protocol; or `mcs_runtime_start/step` from your own loop.
- Shell REPL: multi-line input, expression printing, Ctrl-C, Ctrl-E paste mode, Ctrl-A machine
  mode, dot commands. CLI: `mcs --repl`; `tools/mcs_remote.py repl` terminal.
- Core: `mcs_arity`, `mcs_ticks`, `mcs_sleep` (dispatches callbacks while sleeping),
  `mcs_safepoint`, `mcs_set_idle`; `Thread.Sleep` is abortable.

### Build integration
- `CMakeLists.txt` (plain CMake, pico-sdk, CubeMX CMake, ESP-IDF component), `microcs.mk`,
  `idf_component.yml`, `library.json` (PlatformIO), `zephyr/` module, `include/MicroCS.h`.
- CI: CMake host build, Pico/Pico 2 firmware, ESP-IDF builds (ESP32/S3/C3/C6), STM32 port checks.

### Docs and examples
- New README (two ways to use MicroCS, build-system matrix, ports, comparison with MicroPython and
  nanoFramework), rewritten HAL, PORTING and STANDALONE guides, a README per port.
- 16 commented hardware examples (`examples/hardware/`) run by `make test`; `examples/quickstart_embed.c`.

## 1.3.0 — small-MCU release

Focus: RAM and flash on weak microcontrollers, without changing script behaviour.
Guide: [docs/LOW_RESOURCE.md](docs/LOW_RESOURCE.md).

### Memory & speed (all measured, emulated Cortex-M unless noted)
- **`MCS_COMPACT_VALUES`** (default on for 32-bit targets): values packed to 4-byte alignment,
  12 instead of 16 bytes with double/int64 payloads. On Cortex-M0 + newlib-nano this removed a
  `memcpy` call per value copy: the demo image runs in **4.56 M instead of 18.2 M instructions**.
- **Execute-in-place images**: `mcs_exec_image_xip()` runs bytecode from the image buffer
  (flash) instead of copying it to the heap; `mcs --xip`; `MCS_ENABLE_XIP`.
- VM baseline after `mcs_new` on Cortex-M: **50 136 B → 25 688 B** (M0, full stdlib). Interned-string
  set stores keys only, a global's slot lives in its name string (no global index table),
  built-in exception classes share one field layout, hash tables start at `MCS_TABLE_MIN_CAP` (4).
- **Compact `Dictionary`/`HashSet` index**: 1-, 2- or 4-byte position slots instead of full
  key/value entries; `HashSet` stores no values array; `Remove` no longer rehashes. Host:
  700-string `HashSet` 104 → 58 KB, 100-entry `Dictionary<int,int>` 12.1 → 4.3 KB.
- Compiler RAM: smaller AST nodes and tokens (24 bytes on 32-bit), token array grows by the
  observed density instead of doubling. On-device compile + run of the demo on the M4:
  pool peak **131 528 B → 90 480 B**, compile instructions −12 %.
- Lexer keyword lookup by length + first character, cheaper primitive-type check.
- Host (`make bench`, same machine, same session as 1.2.0): run times unchanged, `mcs_new`
  43 → 30 µs, demo compile 201 → 140 µs, demo heap peak 116 438 → 87 510 B.

### New options and profiles
- `profiles/mcs_profile_lowram.h` for 32–64 KB RAM parts (images only, single-precision
  floats → 8-byte values, small VM limits, 4 KB first GC, 4-byte pool alignment).
- `MCS_ENABLE_LINQ` (LINQ operators + `Enumerable`, ~12 KB flash on M0), `MCS_POOL_ALIGN`,
  `MCS_TABLE_MIN_CAP`, `MCS_ERROR_SIZE`, `MCS_MAX_PINS`, `MCS_COMPACT_VALUES`, `MCS_ENABLE_XIP`.
- Cortex-M targets `m0-lowram` (64 KB RAM, 40 KB pool) and `m0-node` (the new
  `examples/lowram` firmware, 48 KB RAM, 32 KB pool) in `make cm-check`.
- `tools/cm_emu.py --profile N` (hottest functions) and `--callers SYMBOL`.

### Library
- LINQ operators on `Dictionary`, `HashSet`, `Stack` and `Queue` (`Where`, `Select`, `Skip`,
  `Take`, `Last`, `Average`, `GroupBy`, … — previously only lists/arrays had the full set).
- `HashSet`, `Stack` and `Queue` are accepted wherever a sequence is: `string.Join`,
  `string.Concat`, `new List<T>(…)`, `AddRange`, `Concat`, `Zip`, `SequenceEqual`, `new Stack/Queue(…)`
  (previously `string.Join(",", set)` printed the type name).
- `HashSet.IsSupersetOf`, `SetEquals`, `SymmetricExceptWith`.

### Fixes
- `base(msg)` into a lazily registered native constructor (e.g. `class E : IOException`) failed
  with "does not contain .ctor".
- `class X : IOException` was treated as an interface by the `I`+uppercase heuristic.

### Docs, examples, tests
- New: [docs/LOW_RESOURCE.md](docs/LOW_RESOURCE.md), [examples/lowram/](examples/lowram/).
- New test `t14_low_resource.cs` (exception subclasses of built-ins, globals, string churn,
  dictionary index widths, collection interop) — byte-identical to .NET 8.
- Every script test now also runs as an XIP image (46 runs); 104 C unit checks (XIP images,
  300 globals, intern churn, shared layouts, dictionary index); `make check` adds the lowram
  profile and five alternative configurations that run the whole suite.

## 1.2.0

### Language
- **Tuples**: literals `(1, "a")`, named elements `(x: 1, y: 2)`, tuple types `(int x, int y)` in
  locals, fields, parameters, `out` parameters, return types and generic arguments
  (`Dictionary<(int, int), string>`); `Item1..Item7`; structural `==`/`!=`, hashing and `ToString()`.
- Tuple element **name projection** (`var t = (a, b); t.a`) and names that survive later
  assignments, `out` parameters, tuple-typed fields/properties and object initializers.
- **Deconstruction**: `var (a, b) = t;`, `(int a, var b) = t;`, swaps `(a, b) = (b, a)` without
  allocation, `_` discards, `foreach (var (k, v) in dict)`.
- **Index from end and ranges**: `x[^1]`, `x[1..^1]`, `x[..3]`, `x[2..]` on strings, arrays and lists.
- **Patterns in `is`**: relational, `and`, `or`, `not` (`t is < 0 or > 100`); `and` also in switch
  arms and `case` labels.
- `sizeof(builtin)`, `checked(...)` / `unchecked(...)` expressions, `static` local functions with
  tuple return types; `.HasValue` / `.Value` / `.GetValueOrDefault()` on nullable values.
- Declarations used as embedded statements (`while (c) int i = 0;`) are now a compile error (as in C#).

### Library
- LINQ `Zip` (tuple and selector forms) and `Chunk`; `Stack.TryPop` / `TryPeek`,
  `Queue.TryDequeue` / `TryPeek`.
- Generated API reference `docs/STDLIB.md` (`tools/gen_stdlib_doc.py`).

### Performance
- Image format **v2** with superinstructions: `SET_LOCAL_POP`, `SET_GLOBAL_POP` and fused
  compare-and-branch `JF_EQ/NE/LT/LE/GT/GE`. v1 images still load.
- Per-site inline cache for field access (`MCS_FIELD_CACHE`), exact-arity call fast path,
  integer `/` `%` fast path.
- Token array freed right after parsing. Host: fib −20 %, loop −22 %; Cortex-M4 demo −17 %
  instructions; on-device compile peak 163 KB → 132 KB.

### Fixes
- Unbounded growth of the string intern table with many temporary strings (eventually OOM).
- Stack-slot leak when an unbraced loop/if body introduced locals (`out var`, deconstruction temps).
- Lexer: `1..3` is a range, not `1.` followed by `.3`.

### Hardening & tooling
- Image loader validates local/upvalue operands, closure captures and that every branch lands
  on an instruction boundary.
- `mcs -d file.mcsb` disassembles images (`mcs_disassemble_image`).
- `tools/fuzz.py` mutation fuzzer (source and `--image` modes), clean under ASan/UBSan;
  `make asan-test` runs the whole suite with sanitizers; GitHub Actions CI.
- `.NET 8` parity check extended to `t11`, `t13`, `examples/tour.cs`.
- New examples (`blink.cs`, `sensor_logger.cs`, `tour.cs`) run by `make test`.

## 1.1.0 — Phase 2
- Virtual filesystem (RAM, POSIX, LittleFS) with C# `File` / `Directory` / `Path`.
- HAL with C# `GPIO`, `UART`, `I2C`, `SPI`, `ADC`, `PWM` + simulator board.
- Job scheduler, standalone shell with UART upload protocol, execution limits.
- `ref` / `out` / `in`, `case` patterns with `when`.
- Cortex-M0/M4/M33 reference port verified in an emulator. Memory fixes in the compiler.

## 1.0.0 — Phase 1
- Lexer, parser, single-pass compiler, stack VM with computed goto, precise GC, stdlib,
  bytecode images, CLI/REPL.
