# MicroCS for ESP32 (ESP-IDF 5.x)

ESP32, ESP32-S2, S3, C2, C3, C5, C6, H2 and P4. Peripherals only — Wi-Fi and Bluetooth are not
used (and not required), so the port works on every variant including the H2 and P4.

## Quick start

```sh
cd ports/esp32/example
idf.py set-target esp32s3          # or esp32, esp32c3, esp32c6, ...
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

The example allocates 192 KB for the VM with `heap_caps_malloc`; 96 KB is plenty for the
REPL on the C3. On boards with PSRAM you can pass `MALLOC_CAP_SPIRAM`.

## Checked in CI

The example is built with the `espressif/idf:v5.3.2` image for ESP32, ESP32-S3, ESP32-C3 and
ESP32-C6.
