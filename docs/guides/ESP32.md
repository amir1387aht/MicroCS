# ESP32 from zero

For every Espressif chip ESP-IDF 5 supports: ESP32, S2, S3, C2, C3, C5, C6, H2, P4.
Path A takes two minutes and needs no build tools; path B is your own firmware, with the
options (U8g2 displays, pins, filesystem, profile) you choose.

## Path A — flash the release firmware

1. Install esptool: `pip install esptool`.
2. Download `microcs-<version>-<chip>.bin` for your chip (`esp32`, `esp32s3`, `esp32c2`,
   `esp32c3`, `esp32c6`) from the [latest release](https://github.com/amir1387aht/MicroCS/releases/latest).
3. Connect the board by USB and flash the whole image at address 0
   (`--port COM5` on Windows, `/dev/ttyUSB0` or `/dev/ttyACM0` on Linux, `/dev/cu.usbmodem…` on macOS):

   ```sh
   esptool.py --chip esp32c3 --port /dev/ttyACM0 write_flash 0 microcs-1.11.0-esp32c3.bin
   ```

   If esptool cannot connect: hold **BOOT**, tap **RESET** (EN), release BOOT, try again.
4. Open a terminal on the same port at 115200 baud (`python3 -m serial.tools.miniterm /dev/ttyACM0 115200`,
   PuTTY, the Arduino serial monitor, or [MicroCS Studio](https://amir1387aht.github.io/MicroCS/)
   in Chrome/Edge → **Connect**) and press Enter. You get the `> ` prompt:

   ```text
   > Console.WriteLine("Hello from " + Hal.Board);
   Hello from esp32c3
   > GPIO.Mode(8, GPIO.Output); GPIO.Write(8, true);
   ```

The release firmware has every built-in peripheral class but no pins assigned to buses
(I²C, SPI…) and no U8g2 — for those, build it yourself (path B).

## Path B — build your own firmware (ESP-IDF)

### 1. Install ESP-IDF 5.x

Use Espressif's [installer](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/)
(Windows: *ESP-IDF Tools Installer*; Linux/macOS: `install.sh`), or the VS Code *ESP-IDF*
extension. Open the *ESP-IDF terminal* (or run `. $IDF_PATH/export.sh`) so `idf.py` works.

### 2. Make the project

```sh
git clone https://github.com/amir1387aht/MicroCS
cp -r MicroCS/ports/esp32/example my_board
cd my_board
git clone https://github.com/amir1387aht/MicroCS components/MicroCS    # or: mv ../MicroCS components/
idf.py set-target esp32c3            # your chip: esp32, esp32s3, esp32c6, ...
```

The example is the complete firmware: `main/main.c` (~45 lines: filesystem, heap, console,
REPL), `partitions.csv` (1.5 MB app + 2.4 MB for your files on a 4 MB chip) and
`sdkconfig.defaults`.

### 3. Give the buses their pins

Open `main/main.c` and set the pins you wired, before `mcs_esp32_hal_init`:

```c
mcs_esp32_cfg_t pins = MCS_ESP32_CFG_DEFAULT;                 // -1 = not used
pins.i2c[0]  = (mcs_esp32_i2c_pins_t){ 5, 6 };                // SDA, SCL  -> I2C bus 0
pins.spi[0]  = (mcs_esp32_spi_pins_t){ 4, 7, 2, -1, -1 };     // SCLK, MOSI, MISO -> SPI bus 0
pins.uart[1] = (mcs_esp32_uart_pins_t){ 21, 20, -1, -1 };     // TX, RX -> UART 1
pins.pwm[0]  = 10;                                            // PWM channel 0 on GPIO10
pins.led     = 8;                                             // the name "LED" in C#
mcs_esp32_hal_init(&hal, &pins);
```

`GPIO`, `ADC`, `Timer`, `Watchdog` and `RTC` need no setup. Avoid the chip's strapping and
flash pins (ESP32-C3: GPIO 2, 8, 9 at boot, 11–17 flash; ESP32: 0, 2, 5, 12, 15, 6–11 flash).

### 4. Options (optional)

`idf.py menuconfig` → *Component config* → *MicroCS*: build profile, filesystem
(LittleFS / TinyFS / YAFFS2), WS2812 and servo drivers, U8g2 displays, threads. Settings you
want to keep go into `sdkconfig.defaults` (delete `sdkconfig` afterwards so they are read
again). Everything else — VM limits, library switches — goes into a `mcs_user_config.h` in
the project folder ([CONFIGURATION.md](../CONFIGURATION.md)).

### 5. Build, flash, talk

```sh
idf.py build flash monitor          # Ctrl-] leaves the monitor
```

Press Enter for the `> ` prompt.

## Your program on the board

Scripts live in the flash filesystem and survive resets; `/main.cs` runs at every boot.

```sh
python3 components/MicroCS/tools/mcs_remote.py --port /dev/ttyACM0 put blink.cs /main.cs + run /main.cs
python3 components/MicroCS/tools/mcs_remote.py --port /dev/ttyACM0 ls + df
```

or use [MicroCS Studio](https://amir1387aht.github.io/MicroCS/): **Connect**, write, **Save & Run**.
A blink to start with:

```csharp
var led = new Pin(8, GPIO.Output);      // the LED pin of your board
while (true) { led.Toggle(); Thread.Sleep(500); }
```

Ctrl-C in the terminal (or *Stop* in Studio) stops a running program — also a `/main.cs`
that runs forever — and you are back at the prompt ([STANDALONE.md](../STANDALONE.md)).

## Troubleshooting

| Symptom | Fix |
|---|---|
| nothing on the console | the C3/C6/S3 DevKits have two USB ports; the firmware prints on both — try the other one, press Enter |
| `I2C.Scan: not supported` (or `I2C.Write: …`) | the bus has no pins: set `pins.i2c[0]` in `main.c` (step 3) |
| `A fatal error occurred: Failed to connect` | hold BOOT while resetting; use a data USB cable |
| reboot loop after flashing an old `sdkconfig` | `idf.py fullclean`, delete `sdkconfig`, build again |
| `region 'iram0_0_seg' overflowed` / app too big | menuconfig → MicroCS → *Build profile* → Embedded, or turn off what you do not use |

More: [ports/esp32/README.md](../../ports/esp32/README.md) (files, pins, memory, threads,
ESP32-C2) · displays: [ESP32-C3 + OLED](ESP32_C3_OLED.md).
