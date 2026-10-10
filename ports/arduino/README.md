# MicroCS for Arduino

Any 32-bit Arduino core with ~64 KB of free RAM: ESP32 (all variants), RP2040 / RP2350
(Earle Philhower's Arduino-Pico or Mbed), SAMD51, nRF52840, STM32duino, Teensy, Renesas
(UNO R4 is tight), Portenta. AVR boards are too small.

CI packages the library with `tools/make_arduino.py` and compiles every example for ESP32,
ESP32-S3, ESP32-C3, Raspberry Pi Pico, Pico 2 and the Nano 33 BLE (nRF52840, Mbed).

## Install

```sh
python3 tools/make_arduino.py      # → dist/arduino/MicroCS-1.9.1.zip
```

Arduino IDE: *Sketch → Include Library → Add .ZIP Library…*. PlatformIO:
`lib_deps = https://github.com/amir1387aht/MicroCS` with `framework = arduino`.

## Configuration

The Arduino IDE cannot pass `-D` options to a library, so the package carries its
configuration in **`src/mcs_user_config.h`** (a copy of
[`config/mcs_user_config.h`](../../config/mcs_user_config.h): every option set to its
default value; with `--profile NAME`, to that profile's values, written by
`tools/gen_config.py`). Edit it in the installed library (`Arduino/libraries/MicroCS/src/`), or bake
your own settings into the package:

```sh
python3 tools/make_arduino.py --config my_mcs_user_config.h   # your header instead of the template
python3 tools/make_arduino.py --profile lowram --define MCS_ENABLE_LINQ=0 --no-ws2812
```

`--profile`, `--define`, `--no-ws2812`, `--no-servo` and `--fs` (`littlefs`, `yaffs2`, or the
built-in `tinyfs`) are written at the top of that file. Port
options such as `MCS_ARDUINO_NO_WIRE` go there too. PlatformIO projects use the project's
`include/mcs_user_config.h` or `build_flags = -D…` instead ([CONFIGURATION.md](../../docs/CONFIGURATION.md)).

## Examples

* [`MicroCS_REPL`](examples/MicroCS_REPL/MicroCS_REPL.ino) — **the board becomes a complete
  MicroCS device**: C# REPL on `Serial`, files on LittleFS (ESP32 / RP2040), `/boot.cs`,
  `/jobs.cfg` and `/main.cs` at start-up, and the protocol
  [MicroCS Studio](https://amir1387aht.github.io/MicroCS/) and `tools/mcs_remote.py` use.
  ESP32: choose a *Partition Scheme* with SPIFFS/LittleFS (the default has one). RP2040: choose
  a *Flash Size* with an FS part (e.g. "2MB (Sketch: 1MB, FS: 1MB)"), otherwise files are kept
  in RAM.
* [`MicroCS_SD`](examples/MicroCS_SD/MicroCS_SD.ino) — the same with the files on an SD card
  (any core with the SD library).
* [`MicroCS_Embed`](examples/MicroCS_Embed/MicroCS_Embed.ino) — your sketch keeps `loop()`
  and runs C# from it.

## Mapping

```cpp
mcs_arduino_cfg_t cfg = MCS_ARDUINO_CFG_DEFAULT;      // UART 0 = Serial, I2C 0 = Wire, SPI 0 = SPI
cfg.uart[1] = MCS_ARDUINO_UART(Serial1);               // any HardwareSerial / Stream class
cfg.i2c[1] = &Wire1;
mcs_arduino_hal_init(&hal, &cfg);
mcs_arduino_can_pins(5, 4);                            // ESP32: CAN.Open(0, 500000) (TWAI + transceiver)
mcs_arduino_i2s_pins(0, 26, 25, 22, 35);               // I2S.Open(0, ...): bclk, ws, dout, din
```

| C# | Arduino |
|---|---|
| pins | Arduino pin numbers; `GPIO.Pin("A0")`, `"LED"` = `LED_BUILTIN` |
| `GPIO.OnChange` | `attachInterrupt` (8 pins at a time) |
| `UART` | `Serial`, `Serial1`… (any `Stream`) |
| `I2C` / `SPI` | `Wire` / `SPI` (define `MCS_ARDUINO_NO_WIRE` / `MCS_ARDUINO_NO_SPI` to drop them) |
| `ADC` | `analogRead`; `ADC.ReadMillivolts` calibrated on ESP32 (`analogReadMilliVolts`) |
| `DAC` | ESP32 / ESP32-S2 DAC pins, SAMD / Due `DAC0` |
| `PWM` | `analogWrite` + frequency (12-bit duty on ESP32, 16-bit on RP2040 / Teensy) |
| `LedStrip` | `ws2812` driver (`tools/make_arduino.py --no-ws2812` leaves it out): WS2812 on any pin: RMT (ESP32 core 3), PIO (Arduino-Pico), otherwise Adafruit_NeoPixel when the sketch includes `<Adafruit_NeoPixel.h>`; `"NEOPIXEL"` = `PIN_NEOPIXEL` / `PIN_RGB_LED` |
| `I2S` | ESP32 (IDF standard-mode driver, TX / RX / duplex), RP2040 (`I2S` library, WS = BCLK + 1) |
| `CAN` | ESP32 TWAI (25 k … 1 Mbit/s) |
| `Watchdog` | ESP32 task watchdog, RP2040, AVR |
| `Timer` | software timers, run from the REPL loop / `Hal.Poll()` |
| `RTC` | software clock from `millis()` |
| `Hal.UniqueId` | ESP32 MAC, RP2040 flash ID, nRF52 FICR, SAMD serial, STM32 UID |

## Files

```cpp
#include <LittleFS.h>
LittleFS.begin(true);                                       // ESP32: format on first use
static mcs_arduino_fs_t files = MCS_ARDUINO_FS(LittleFS, "littlefs");
cfg.fs_ops = &mcs_arduino_fs_ops; cfg.fs_ctx = &files;      // mcs_runtime_cfg_t
```

`MCS_ARDUINO_FS(obj, format)` takes any `fs::FS` of the ESP32 and RP2040 cores — `LittleFS`,
`SPIFFS`, `FFat`, `SD`, `SD_MMC`, `SDFS`. On other cores include `<SD.h>` in the sketch and use
`MCS_ARDUINO_SD_FS(SD)` after `SD.begin(cs)` (the SD library cannot rename, so `File.Move` /
`mv` copy the file and delete the original; directories cannot be moved there).
