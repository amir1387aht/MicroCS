# MicroCS for ESP32 (ESP-IDF 5.x)

ESP32, ESP32-S2, S3, C2, C3, C5, C6, C61, H2 and P4: every peripheral, plus **Wi-Fi and networking**
from C# on the chips with a radio (`Wifi`, ESP-NOW, SNTP, mDNS, ping, TCP/UDP sockets, HTTP +
HTTPS) — see [NETWORK.md](../../docs/NETWORK.md). Chips without Wi-Fi (H2, P4) build the same
port without it. BLE-capable release targets include NimBLE; S2 and P4 have no onboard BLE.
See [Bluetooth LE](../../docs/BLE.md).

ESP32-C6 example builds enable ESP-IDF's `-msave-restore` size optimization
(`sdkconfig.defaults.esp32c6`) so Wi-Fi, TLS, BLE and CAN fit the existing 2 MB app
partition. This adds small register-save/restore call overhead; no features are
removed and the script-storage partition stays at its existing offset. For an
already configured project, enable `CONFIG_COMPILER_SAVE_RESTORE_LIBCALLS` in
menuconfig or regenerate `sdkconfig` after backing up custom settings.

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
├── partitions.csv          # 2 MB app (Wi-Fi + TLS) + LittleFS "storage" partition for your scripts
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
  for **4 MB** of flash (2 MB app with Wi-Fi and TLS, ~1.9 MB files). On bigger flash set *Serial flasher
  config → Flash size* in menuconfig (or `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` in
  `sdkconfig.defaults`) and grow `storage` to the end of the chip:

  | Flash | `storage` size | Space for files |
  |---|---|---|
  | 4 MB | `0x1F0000` | 1.9 MB |
  | 8 MB | `0x5F0000` | 5.9 MB |
  | 16 MB | `0xDF0000` | 13.9 MB |

  Building without radios (`CONFIG_MICROCS_WIFI=n`, `CONFIG_MICROCS_BLE=n`)?
  A 1.5 MB app is enough — use
  [`example/partitions_nowifi.csv`](example/partitions_nowifi.csv)
  (`CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_nowifi.csv"`): `factory` `0x180000`,
  `storage` `0x270000` (2.4 MB) at `0x190000`, as older no-Wi-Fi releases did.

  The boot log prints `MicroCS: LittleFS on flash, N of M KB used`.
* **TinyFS instead** (built in, nothing to fetch): menuconfig → MicroCS → *Filesystem on the
  "storage" partition* → TinyFS (`CONFIG_MICROCS_FS_TINYFS=y`); `mcs_esp32_flash_fs()` mounts it.
* **YAFFS2 instead**: menuconfig → MicroCS → *Filesystem on the "storage" partition* →
  YAFFS2 (or `CONFIG_MICROCS_FS_YAFFS2=y` in `sdkconfig.defaults`). The yaffs2 sources are
  not bundled: set `MICROCS_YAFFS2_DIR` (environment or `idf.py -DMICROCS_YAFFS2_DIR=...`), keep a
  checkout in `<project>/third_party/yaffs2`, or allow the pinned download with
  `MICROCS_FS_DOWNLOAD=1 idf.py build`; then `mcs_esp32_flash_fs("storage", &cfg.fs_ops, &cfg.fs_ctx)` mounts it through
  `mcs_esp32_partition_flash()` (any data partition as an `mcs_flash_t`). YAFFS2 is
  GPLv2 (or commercial licence): linking it puts your firmware under those terms. C code
  then uses the files through MicroCS, not `fopen()`. The example calls
  `mcs_esp32_flash_fs()`, which picks whichever filesystem is configured.
* **C code sees the same files** (LittleFS): the partition is mounted in the ESP-IDF VFS at `/mcs`
  (`MCS_ESP32_FS_PATH`), so `fopen("/mcs/config.json", "r")` reads what a script wrote to
  `/config.json`.
* **From C#**: `File.WriteAllText("/log.txt", ...)`, `File.ReadAllText`, `File.AppendAllText`,
  `Directory.CreateDirectory`, ... — see [FILESYSTEM.md](../../docs/FILESYSTEM.md#c-api).
  Free space: `new DriveInfo("/").AvailableFreeSpace` (bytes; also `TotalSize`).
* **From the browser**: open [MicroCS Studio](https://amir1387aht.github.io/MicroCS/) (or `tools/studio/index.html`) in Chrome/Edge → **Connect** — file
  manager, C# editor (save + run on the board) and REPL in one page
  ([STANDALONE.md](../../docs/STANDALONE.md#6-in-the-browser-microcs-studio)). Connecting does
  not reset the board; use the console port the firmware prints on (UART bridge or the native
  USB-Serial-JTAG - the example serves both), at 115200 baud.
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
| `LedStrip` | `ws2812` driver (menuconfig → MicroCS → *WS2812 / NeoPixel driver*): RMT TX on any GPIO (up to 2 strips; none on ESP32-C2); `"NEOPIXEL"` = on-board RGB LED (S3 GPIO48, C3/C6/H2 GPIO8, override `MCS_ESP32_RGB_LED`) |
| `Timer` | `gptimer` |
| `I2S` | `i2s_std` |
| `CAN` | Classic CAN through legacy TWAI or IDF 5.5+ TWAI nodes (including C5); native pins/modes in `CanConfig`, or external MCP2515 over SPI ([setup](../../docs/CAN.md)) |
| `Watchdog` | task watchdog |
| `RTC` | `gettimeofday` / `settimeofday` |
| `Hal.UniqueId` | eFuse MAC |

Console: `mcs_esp32_console()` (UART0 + USB-Serial-JTAG where the chip has it, input from
either), `mcs_esp32_console_usb()` (USB-Serial-JTAG only) or `mcs_esp32_console_uart(0, 115200)`.
ESP-IDF's USB-Serial-JTAG driver drops received bytes that do not fit its buffer (USB itself
would wait), so MicroCS installs it with a 4 KB receive buffer (`MCS_ESP32_USB_RXBUF`, TX:
`MCS_ESP32_USB_TXBUF`) and Studio / `mcs_remote.py` upload with flow control (`put … <window>`,
[STANDALONE.md](../../docs/STANDALONE.md)). If your app installs that driver itself, give it a
receive buffer of at least `MCS_SHELL_PUT_WINDOW` (256) bytes.

## Displays (U8g2)

Optional C# `U8g2` / `U8x8` for OLED and LCD panels: menuconfig → *MicroCS* → *U8g2 displays*
(`CONFIG_MICROCS_U8G2=y`, plus the displays and fonts to build in). Step by step:
[ESP32-C3 + OLED](../../docs/guides/ESP32_C3_OLED.md); reference: [U8G2.md](../../docs/U8G2.md).

## Wi-Fi and networking

On by default wherever the chip has Wi-Fi when you build the example: menuconfig → *MicroCS* →
*Wi-Fi and networking* (`CONFIG_MICROCS_WIFI`). The ready-to-flash release images
include Wi-Fi + ESP-NOW on every Wi-Fi-capable target. H2 / P4 have no onboard Wi-Fi.
See the release migration warning below before updating old no-Wi-Fi firmware.

```csharp
Wifi.Connect("MyNetwork", "secret-password");
Console.WriteLine(Wifi.Ip + " " + Http.Get("https://example.com/").StatusCode);
```

* **Connect at boot**: set *network name* / *password* in menuconfig
  (`CONFIG_MICROCS_WIFI_SSID` / `_PASSWORD`), or save one once from C# with
  `Wifi.Save(ssid, password)` (NVS) — `CONFIG_MICROCS_WIFI_AUTOCONNECT` then joins it before
  `/main.cs` runs. Your own firmware calls `mcs_esp32_wifi_autoconnect(ssid, password)`.
* **RAM**: `CONFIG_MICROCS_WIFI_RESERVE_KB` (72 KB, 56 KB on the C2) stays outside the VM heap
  for the Wi-Fi driver, lwIP and TLS. The driver itself starts only when a script first uses
  Wi-Fi; `Wifi.Off()` stops it again.
* **Flash**: Wi-Fi + TLS make the image ~1.6 MB, hence the 2 MB app partition.
* **mDNS**: `idf.py add-dependency espressif/mdns` — the component is detected and `Mdns`
  appears (`Net.Has("Mdns")`).
* **HTTPS** uses esp-tls with the ESP-IDF certificate bundle; ESP-NOW, SNTP and ping need
  nothing extra. WPA2/WPA3-Enterprise needs IDF 5.1+.

## Release firmware and stored scripts

Release images enable real threads and each chip's onboard radios: Wi-Fi + ESP-NOW
where Wi-Fi is supported, and NimBLE where BLE is supported. H2 is BLE-only; S2 is
Wi-Fi-only; P4 has neither onboard radio. C5 / C61 use ESP-IDF 5.5.2, others 5.3.2.

**Back up scripts before upgrading from the old no-Wi-Fi release images.** The app
partition grows from 1.5 MB to 2 MB (2.5 MB on C5 / C61), moving `storage`. The new
filesystem will not preserve files at the old offset. Custom `partitions_nowifi.csv`
builds are still available for users who deliberately disable networking.

## Bluetooth LE

Included in BLE-capable release images; opt-in for custom builds:
[`sdkconfig.ble`](example/sdkconfig.ble) turns on NimBLE and
`CONFIG_MICROCS_BLE` (C# `Ble`, `BleService`, `BleConnection`, `BleUart`; [BLE.md](../../docs/BLE.md)):

```sh
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ble" set-target esp32s3 build flash monitor
```

### menuconfig

`idf.py menuconfig` → **Component config → MicroCS → Bluetooth LE** is visible on chips
with an onboard BLE radio, even before Bluetooth is enabled. It shows the missing
prerequisites instead of silently hiding the switch:

1. **Component config → Bluetooth → Bluetooth**: enable.
2. **Host**: **NimBLE - BLE only**; **Controller**: **Enabled**.
3. **NimBLE Options**: leave **extended advertising** off (MicroCS uses legacy advertising).
4. **MicroCS → Bluetooth LE**: enable the C# API; configure the default name, event queue,
   GATT services / attributes / value pool, scan / discovery results, subscriptions, timeouts
   and UART buffer. RAM reservation defaults to 48 KB. Larger pools consume more static RAM.
5. **Bluetooth → NimBLE Options**: set connections, preferred MTU, bonding / security and NVS
   persistence. The MicroCS connection table follows the NimBLE connection limit (C2: at most
   two). On ESP32 also match the controller's BLE connection count; on S3 / C3 allow enough
   controller activities for connections plus scanning / advertising.

Menuconfig choices override the corresponding `MCS_BLE_*` values in `mcs_user_config.h`.
Bonds survive reset when `CONFIG_BT_NIMBLE_NVS_PERSIST=y`. The supplied BLE defaults enable
this and use two connections so they are valid on C2 as well as the larger chips.

| Chip | Onboard BLE | ESP-IDF build coverage |
| --- | --- | --- |
| ESP32, S3, C2, C3, C6, H2 | yes | 5.3.2, BLE-enabled CI builds |
| C5, C61 | yes | 5.5.2, BLE-enabled CI builds |
| S2, P4 | no | BLE unavailable; non-BLE CI builds |

Wi-Fi and BLE can coexist on chips that have both radios; H2 has BLE but no Wi-Fi.
S2 / P4 cannot run this onboard NimBLE backend (an external / hosted radio is not implemented).
C5 / C61 target defaults use `partitions_large.csv`: a 2.5 MB app and 1472 KB storage
on 4 MB flash (IDF 5.5 Wi-Fi + TLS + BLE exceed the older 2 MB app). S3 and older-chip
layouts are unchanged. When changing a partition table on an already flashed device,
back up scripts first: the storage offset changes; restore them after formatting the new
filesystem. In an existing C5 / C61 configuration select `partitions_large.csv` under
*Partition Table → Custom partition CSV file*.

These are compile / simulator checks, not real-board radio validation.
Backend: [`mcs_port_esp32_ble.c`](mcs_port_esp32_ble.c).

The backend is [`mcs_port_esp32_net.c`](mcs_port_esp32_net.c); the C# API, events, options and
limits are in [docs/NETWORK.md](../../docs/NETWORK.md).

## Memory

The example grows a bounded multi-region VM pool on demand, up to
`CONFIG_MICROCS_VM_HEAP_KB` (192 KiB by default). No region addresses are needed.
`mcs_esp32_heap_init()` supplies an ESP-IDF provider using internal byte-addressable
RAM. Each growth request keeps a 32 KiB base native reserve plus configured Wi-Fi
and BLE reserves, and leaves an 8 KiB contiguous allocation margin. The pool cap
includes all acquired region backing and metadata; one object still needs one
contiguous block. Empty owned regions can be returned on application replacement.
A RAM disk (one sixth of the cap) is used only without a `storage` partition.

This avoids the old whole-pool single-block limit, not the chip's physical RAM
limit. Enabling BLE uses native/static RAM outside the pool as well as managed
C# objects inside it. Reserves are headroom, not extra VM allocations. SDK heap
samples cannot guarantee future concurrent native allocations will succeed.
PSRAM is not automatically selected; use an explicit custom provider if required.
See [automatic regions and accounting](../../docs/MEMORY.md#automatic-bounded-regions-all-ports).

## Build profile per chip

`idf.py menuconfig` → *Component config* → *MicroCS* → *Build profile*. Every chip defaults
to the full build except the ESP32-C2, which defaults to *Embedded* (compiler + REPL +
modules, 32-bit `long`, no disassembler). *MCU*, *Low-RAM* and *Minimal* are for your own
application running precompiled images (the example's REPL needs the compiler). The HAL is
compiled in every profile.

Every other option (VM limits, stdlib switches, `MCS_ESP32_FS_PATH`, …) goes into a
`mcs_user_config.h` in the project folder or `main/` (template:
[`config/mcs_user_config.h`](../../config/mcs_user_config.h)), found automatically; *Project config
header* in the same menu names another file. The menuconfig choices win over the header,
except the profile: `MCS_PROFILE` in the header wins. See [CONFIGURATION.md](../../docs/CONFIGURATION.md).

## ESP32-C2 (ESP8684)

```sh
idf.py set-target esp32c2 && idf.py build flash monitor
```

* **Memory**: 272 KB SRAM (~180 KB free after boot) → embedded profile
  (`sdkconfig.defaults.esp32c2`), ~90 KB VM heap with Wi-Fi (~120 KB with
  `CONFIG_MICROCS_WIFI=n`).
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

## Threads on both cores

The ready-to-flash release images have threads on. For your own build:
`idf.py menuconfig` → Component config → MicroCS → **Real threads on FreeRTOS tasks**
(`CONFIG_MICROCS_THREADS=y`). The REPL keeps core 0, and C# can use core 1:

```csharp
var w = Thread.RunOn(1, "/dsp.cs", "Filter", 128);   // a function on core 1
Console.WriteLine(w.Result);
var loop = Thread.Every(10, "/control.cs", "Tick", 1); // OS-timed, every 10 ms on core 1
```

`jobs.cfg` file jobs then run on threads by default: `every 10ms /control.cs core=1`
(`main` keeps a job on the main VM). Each thread has its own C#
heap (32 KB, taken from the VM heap) and a 12 KB task stack. Off (the default), the firmware
is single-threaded, as before. See [docs/THREADS.md](../../docs/THREADS.md).

## Checked in CI

The example is built the way a user builds it (copied to a new folder, MicroCS in
`components/MicroCS`) with the `espressif/idf:v5.3.2` image for ESP32, ESP32-S3, ESP32-C2,
ESP32-C3 and ESP32-C6 — Wi-Fi and networking included.

## Power management

See [sleep, wake pins and shutdown](../../docs/POWER.md) for the exact support matrix,
`Hal.Has("Power.LightSleep")` / `Power.DeepSleep.Pin` checks, GPIO restrictions and
Studio/recovery precautions. Unsupported modes throw; they do not fall back to delay.

### VM pool ceiling and device memory

The example firmware exposes `MICROCS_VM_HEAP_KB`, `MICROCS_VM_STACK_SLOTS` and
`MICROCS_VM_MAX_FRAMES` in menuconfig. The pool ceiling remains 192 KiB; slots and frames grow on demand. Zero stack/frame settings
preserve profile defaults (full: 1024/64, embedded: 512/64). For the representative 40 KiB OLED/menu Project use **40 / 0 / 0**
and a flash filesystem. This bounds the pool while retaining normal profile execution ceilings, not a smaller build profile.
The example prints its ceiling and native reserve. Shell/Studio report currently
committed capacity, backing bytes, ceiling and region count. It never exceeds the
backing ceiling or silently falls back to unbounded malloc on growth failure.
Custom entrypoints must set/check these values themselves. HAL initialization
also installs device-heap telemetry for the shell/Studio automatically.
See [memory budgets](../../docs/MEMORY.md): the device heap includes the reserved
pool and SDK costs, but is not total physical SRAM. No ESP32 hardware measurement
or arbitrary-app 40 KiB guarantee is implied by the host regression.

## Rotary encoders

[RotaryEncoder](../../docs/ENCODER.md) prefers PCNT,
with software GPIO decoding only when no native backend supports the selected
pins. `encoder.Backend` reports the actual selection. See the guide for pin
bindings, resource ownership, setup and counter-service limits.

If the VM reports substantial pool free but a much smaller `largest`, update to
a build containing empty-region pressure recovery. On failed growth it returns
entirely empty regions and retries a suitably sized native allocation, without
lowering native/BLE reserves. Rebuild and flash the firmware; a Studio-only update
does not change allocation behavior. Keep the same configuration for the first
comparison and retry the same script. Record its full error and `mem` report if
it still fails. Recovery cannot release regions that contain live objects, or
guarantee success against native fragmentation. See
[pressure recovery](../../docs/MEMORY.md#automatic-recovery-of-empty-regions-under-allocation-pressure).
