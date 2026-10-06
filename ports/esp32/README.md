# MicroCS for ESP32 (ESP-IDF 5.x)

ESP32, ESP32-S2, S3, C2, C3, C5, C6, H2 and P4. Peripherals only — Wi-Fi and Bluetooth are not
used (and not required), so the port works on every variant including the H2 and P4.

## Quick start

```sh
cd ports/esp32/example
idf.py set-target esp32s3          # or esp32, esp32c2, esp32c3, esp32c6, ...
idf.py build flash monitor         # type C# at the "> " prompt
```

The example's top-level `CMakeLists.txt` adds the MicroCS repository as an extra component.
In your own project either clone MicroCS into `components/` or set
`EXTRA_COMPONENT_DIRS` — the repository root is an ESP-IDF component.

## Pins

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

Console: `mcs_esp32_console_usb()` (USB-Serial-JTAG on S3/C3/C6/H2/P4, UART0 otherwise) or
`mcs_esp32_console_uart(0, 115200)`.

## Memory

The example allocates 192 KB for the VM with `heap_caps_malloc` (128 KB on the C2), clamped
to the largest free block minus a 32 KB reserve for drivers and FreeRTOS; 96 KB is plenty
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

* **Memory**: 272 KB SRAM (~180 KB free after boot) → embedded profile, 128 KB VM heap,
  16 KB RAM disk (`sdkconfig.defaults.esp32c2`).
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
* **Pins in the example** (ESP8684-DevKitC): UART1 TX 7 / RX 10, I²C SDA 5 / SCL 6,
  SPI SCLK 4 / MOSI 3 / MISO 2, PWM 0 on GPIO 1.

## Checked in CI

The example is built with the `espressif/idf:v5.3.2` image for ESP32, ESP32-S3, ESP32-C2,
ESP32-C3 and ESP32-C6.
