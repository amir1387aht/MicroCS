# Arduino IDE / PlatformIO

For 32-bit Arduino cores with about 64 KB of free RAM: ESP32 (every variant), RP2040 / RP2350
(Arduino-Pico or Mbed), nRF52840, SAMD51, STM32duino, Teensy, Portenta. AVR boards (UNO,
Mega) are too small.

## Arduino IDE

### 1. Get the library

Either download `MicroCS-<version>.zip` from the
[latest release](https://github.com/amir1387aht/MicroCS/releases/latest), or build the
package yourself (lets you bake in options, see step 4):

```sh
git clone https://github.com/amir1387aht/MicroCS && cd MicroCS
python3 tools/make_arduino.py            # -> dist/arduino/MicroCS-<version>.zip
```

In the IDE: *Sketch → Include Library → Add .ZIP Library…* and pick the zip.

### 2. Open the REPL example

*File → Examples → MicroCS → MicroCS_REPL*. It turns the board into a complete MicroCS
device: C# prompt on `Serial`, files in the flash, `/main.cs` at start-up, MicroCS Studio and
`mcs_remote.py` support.

Board settings that matter:

* **ESP32:** *Tools → Partition Scheme* with a SPIFFS/LittleFS part (the default has one).
* **RP2040 / RP2350 (Arduino-Pico):** *Tools → Flash Size* with an FS part, e.g.
  "2MB (Sketch: 1MB, FS: 1MB)" — otherwise files live in RAM.

### 3. Upload and talk

Upload, open the *Serial Monitor* at **115200** baud (line ending: *Newline*) or
[MicroCS Studio](https://amir1387aht.github.io/MicroCS/), and type C#:

```text
> var led = new Pin("LED", GPIO.Output);   // LED_BUILTIN
> led.Toggle();
> Console.WriteLine(string.Join(",", I2C.Scan(0)));   // devices on Wire
```

C# bus 0 is `Serial` / `Wire` / `SPI`. More buses — in the sketch, before `mcs_arduino_hal_init`:

```cpp
mcs_arduino_cfg_t cfg = MCS_ARDUINO_CFG_DEFAULT;
cfg.uart[1] = MCS_ARDUINO_UART(Serial1);    // UART.Open(1, ...)
cfg.i2c[1]  = &Wire1;                       // I2C bus 1
```

Set the pins of `Wire` / `SPI` the Arduino way before that (ESP32: `Wire.begin(SDA, SCL)`,
`SPI.begin(SCK, MISO, MOSI)`; Arduino-Pico: `Wire.setSDA()` / `setSCL()`).

### 4. Options

The IDE cannot pass `-D` flags to libraries, so the package carries
`src/mcs_user_config.h`. Edit it in `Arduino/libraries/MicroCS/src/`, or build the package with
your settings:

```sh
python3 tools/make_arduino.py --profile embedded --no-ws2812
python3 tools/make_arduino.py --u8g2 --u8g2-displays ssd1306_i2c_128x64_noname   # U8g2 displays
python3 tools/make_arduino.py --config my_mcs_user_config.h
```

(`--u8g2` also needs the **U8g2** library from the Library Manager.)

## PlatformIO

```ini
[env:esp32c3]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
lib_deps = https://github.com/amir1387aht/MicroCS
build_flags = -DMCS_ENABLE_WS2812=1
monitor_speed = 115200
```

Copy `ports/arduino/examples/MicroCS_REPL/MicroCS_REPL.ino` to `src/main.cpp`
(add `#include <Arduino.h>` at the top), then `pio run -t upload -t monitor`. Options go
into `build_flags` or `include/mcs_user_config.h`.

## Your program on the board

```sh
python3 tools/mcs_remote.py --port /dev/ttyUSB0 put blink.cs /main.cs + run /main.cs
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| `region 'dram0_0_seg' overflowed` / not enough RAM | a smaller profile: `--profile embedded` (or `lowram`) |
| files disappear after reset | the board has no filesystem partition (step 2), or `LittleFS.begin(true)` failed |
| characters echo twice / not at all | Serial Monitor line ending *Newline*; or use Studio |
| `Wire.h: No such file` on a core without Wire | add `#define MCS_ARDUINO_NO_WIRE` to `src/mcs_user_config.h` |

More: [ports/arduino/README.md](../../ports/arduino/README.md) (mapping, files, SD cards, embedding in your own sketch).
