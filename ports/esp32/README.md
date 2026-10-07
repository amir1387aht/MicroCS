# MicroCS for ESP32 (ESP-IDF 5.x)

ESP32, ESP32-S2, S3, C2, C3, C5, C6, H2 and P4. Peripherals only — Wi-Fi and Bluetooth are not
used (and not required), so the port works on every variant including the H2 and P4.

## Quick start — MicroCS as the whole firmware

[`example/`](example) is a complete ESP-IDF project. Copy it, put MicroCS in
`components/MicroCS`, build:

```sh
cp -r MicroCS/ports/esp32/example my_board && cd my_board   # or start from your own project
git clone https://github.com/amir1387aht/MicroCS components/MicroCS
idf.py set-target esp32s3          # or esp32, esp32c2, esp32c3, esp32c6, ...
idf.py build flash monitor         # press Enter, type C# at the "> " prompt
```

```
my_board/
├── CMakeLists.txt          # plain IDF project file, nothing MicroCS-specific
├── sdkconfig.defaults      # 16 KB main-task stack, partitions.csv, 4 MB flash
│                           #   (delete an old sdkconfig after adding it)
├── partitions.csv          # 1.5 MB app + LittleFS "storage" partition for your scripts
├── main/
│   ├── CMakeLists.txt      # idf_component_register(SRCS "main.c")
│   └── main.c              # ~45 lines: flash filesystem, heap, console, mcs_runtime_run()
└── components/
    └── MicroCS/            # this repository (git clone or git submodule)
```

In VS Code (ESP-IDF extension): *Set Espressif Device Target* → *Build, Flash and Monitor*.
There is no `idf_component.yml` to add to your project; the one in the repository root
belongs to MicroCS itself (it pulls in the `joltwallet/littlefs` component automatically
on the first build).

The console (`mcs_esp32_console()`) is UART0 **and** USB-Serial-JTAG at the same time, so
the prompt shows up on either USB connector of an S3/C3/C6 DevKit. The banner is printed
once at boot — if the monitor attached later, press Enter for a fresh `> `.

## Files on flash (LittleFS)

Scripts you upload, `/boot.cs`, `/main.cs` and every file a script writes live in a
**LittleFS** filesystem on the chip's SPI NOR flash, so they survive resets and power cycles.
LittleFS is made for NOR flash: power-loss safe (an interrupted write leaves the old file),
wear levelling, small RAM use. Uploads by `tools/mcs_remote.py` are written to a temporary
file and renamed, so a reset during `put` never leaves a half-written `/main.cs`.

```c
mcs_runtime_cfg_t cfg = MCS_RUNTIME_DEFAULTS;
bool on_flash = mcs_esp32_littlefs("storage", &cfg.fs_ops, &cfg.fs_ctx);   // before allocating the VM heap
if (!on_flash) cfg.ramfs_size = 32 * 1024;                                 // no partition: RAM disk
```

* **Partition**: `storage` in [`example/partitions.csv`](example/partitions.csv) (type `data`,
  subtype `spiffs`). It is formatted automatically on first boot. The example is laid out
  for **4 MB** of flash (1.5 MB app, 2.4 MB files). On bigger flash set *Serial flasher
  config → Flash size* in menuconfig (or `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` in
  `sdkconfig.defaults`) and grow `storage` to the end of the chip:

  | Flash | `storage` size | Space for files |
  |---|---|---|
  | 4 MB | `0x270000` | 2.4 MB |
  | 8 MB | `0x670000` | 6.4 MB |
  | 16 MB | `0xE70000` | 14.4 MB |

  The boot log prints `MicroCS: LittleFS on flash, N of M KB used`.
* **C code sees the same files**: the partition is mounted in the ESP-IDF VFS at `/mcs`
  (`MCS_ESP32_FS_PATH`), so `fopen("/mcs/config.json", "r")` reads what a script wrote to
  `/config.json`.
* **From C#**: `File.WriteAllText("/log.txt", ...)`, `File.ReadAllText`, `File.AppendAllText`,
  `Directory.CreateDirectory`, ... — see [FILESYSTEM.md](../../docs/FILESYSTEM.md#c-api).
  Free space: `new DriveInfo("/").AvailableFreeSpace` (bytes; also `TotalSize`).
* **From the PC**:
  `python tools/mcs_remote.py --port COM18 put app.cs /main.cs + ls` — `/main.cs` then runs
  at every boot. `get`, `rm`, `mkdir`, `mv` work the same way; `df` prints size, used and free.
* **Wipe everything**: `idf.py erase-flash` (or `rm` the files); the next boot formats the
  partition again.

Flashing a new firmware with `idf.py flash` writes only the app and partition table, so your
files stay — unless you change `partitions.csv` so that `storage` moves.

## Pins

The example sets no pins: `GPIO`, `ADC`, `Timer`, `Watchdog` and `RTC` work without any.
For `UART` 1/2, `I2C`, `SPI`, `PWM`, `I2S` and `CAN`, tell the port which GPIOs to use:

```c
mcs_esp32_cfg_t pins = MCS_ESP32_CFG_DEFAULT;                 // everything -1 = unused / default
pins.uart[1] = (mcs_esp32_uart_pins_t){ 17, 18, -1, -1 };     // TX, RX, RTS, CTS
pins.i2c[0]  = (mcs_esp32_i2c_pins_t){ 8, 9 };                 // SDA, SCL
pins.spi[0]  = (mcs_esp32_spi_pins_t){ 12, 11, 13, -1, -1 };  // SCLK, MOSI, MISO, WP, HD (QSPI)
pins.pwm[0]  = 2;                                              // LEDC channel 0 -> GPIO2
pins.i2s[0]  = (mcs_esp32_i2s_pins_t){ -1, 4, 5, 6, 7 };       // MCLK, BCLK, WS, DOUT, DIN
pins.can     = (mcs_esp32_can_pins_t){ 15, 16 };               // TWAI TX, RX (needs a transceiver)
mcs_esp32_hal_init(&hal, &pins);
```

| C# | ESP-IDF driver |
|---|---|
| `GPIO` / `Pin`, `OnChange` | `gpio_*`, ISR service → FreeRTOS queue (safe on dual core) |
| `UART` | `uart_driver_install` / `uart_read_bytes` |
| `I2C` | `i2c_master` (IDF ≥ 5.2) or the legacy driver |
| `SPI`, `SpiDevice`, `QSPI` | `spi_master` (quad mode via `spi_transaction_ext_t`) |
| `ADC.Read(gpio)` / `ReadMillivolts` | `adc_oneshot` + eFuse calibration |
| `DAC` | `dac_oneshot` (ESP32, S2) |
| `PWM` | LEDC (13-bit duty) |
| `Timer` | `gptimer` |
| `I2S` | `i2s_std` |
| `CAN` | TWAI |
| `Watchdog` | task watchdog |
| `RTC` | `gettimeofday` / `settimeofday` |
| `Hal.UniqueId` | eFuse MAC |

Console: `mcs_esp32_console()` (UART0 + USB-Serial-JTAG where the chip has it, input from
either), `mcs_esp32_console_usb()` (USB-Serial-JTAG only) or `mcs_esp32_console_uart(0, 115200)`.

## Memory

The example allocates up to 192 KB for the VM with `heap_caps_malloc`, clamped to the largest
free block minus a 32 KB reserve for drivers and FreeRTOS (~120 KB on the C2); a RAM disk (a
sixth of the heap) is only used when there is no `storage` partition; 96 KB is plenty
for the REPL on the C3. On boards with PSRAM you can pass `MALLOC_CAP_SPIRAM`.

## Build profile per chip

`idf.py menuconfig` → *Component config* → *MicroCS* → *Build profile*. Every chip defaults
to the full build except the ESP32-C2, which defaults to *Embedded* (compiler + REPL +
modules, 32-bit `long`, no disassembler). *MCU*, *Low-RAM* and *Minimal* are for your own
application running precompiled images (the example's REPL needs the compiler). The HAL is
compiled in every profile.

## ESP32-C2 (ESP8684)

```sh
idf.py set-target esp32c2 && idf.py build flash monitor
```

* **Memory**: 272 KB SRAM (~180 KB free after boot) → embedded profile
  (`sdkconfig.defaults.esp32c2`), ~120 KB VM heap incl. a ~20 KB RAM disk.
* **printf**: IDF enables the ROM "nano" formatter on the C2 by default
  (`CONFIG_NEWLIB_NANO_FORMAT`), which cannot print floats — MicroCS formats `double` with
  `snprintf`, so the example turns it off. The port prints a `#warning` if it is on with
  floats enabled.
* **Crystal**: many ESP8684 modules use a 26 MHz crystal — set `CONFIG_XTAL_FREQ_26=y`
  (commented line in `sdkconfig.defaults.esp32c2`) or the UART baud rate and timers are off.
* **Peripherals**: GPIO 0–20, `UART` 0–1 (console on UART0, GPIO 20/19), one `I2C` bus,
  `SPI` bus 0 (SPI2), `ADC` on GPIO 0–4 (12-bit), `PWM` = 6 LEDC channels clocked from the
  60 MHz PLL divider (`MCS_ESP32_LEDC_CLK_HZ`), `Timer` = 1 general-purpose timer
  (`MCS_ESP32_HW_TIMERS` is clamped to what the chip has). Not on the C2: `DAC`, `I2S`,
  `CAN` (TWAI), USB-Serial-JTAG (the console uses UART0) — those C# classes report
  `NotSupportedException`.
* **Pins** that suit an ESP8684-DevKitC: UART1 TX 7 / RX 10, I²C SDA 5 / SCL 6,
  SPI SCLK 4 / MOSI 3 / MISO 2, PWM 0 on GPIO 1.

## Checked in CI

The example is built the way a user builds it (copied to a new folder, MicroCS in
`components/MicroCS`) with the `espressif/idf:v5.3.2` image for ESP32, ESP32-S3, ESP32-C2,
ESP32-C3 and ESP32-C6.
