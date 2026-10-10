# Examples

Every C# example runs on your PC against the simulator board (`--sim`) and, unchanged, on a
real board through its port. `make test` runs all of them.

## Hardware (`examples/hardware/`)

| # | File | What it shows | Run on the host |
|---|---|---|---|
| 01 | [`01_blink.cs`](hardware/01_blink.cs) | `Pin` object, pin names, `Thread.Sleep` | `./mcs --sim examples/hardware/01_blink.cs` |
| 02 | [`02_button_interrupt.cs`](hardware/02_button_interrupt.cs) | pull-up input, falling-edge interrupt callback | `./mcs --sim examples/hardware/02_button_interrupt.cs` |
| 03 | [`03_pwm_fade_servo_tone.cs`](hardware/03_pwm_fade_servo_tone.cs) | PWM duty, servo angles, buzzer melody | `./mcs --sim examples/hardware/03_pwm_fade_servo_tone.cs` |
| 04 | [`04_i2c_scan.cs`](hardware/04_i2c_scan.cs) | I²C bus scan with a `switch` expression to name devices | `./mcs --sim examples/hardware/04_i2c_scan.cs` |
| 05 | [`05_i2c_temperature.cs`](hardware/05_i2c_temperature.cs) | a TMP102 driver class over `I2cDevice` | `./mcs --sim examples/hardware/05_i2c_temperature.cs` |
| 06 | [`06_i2c_imu_registers.cs`](hardware/06_i2c_imu_registers.cs) | register-style device (MPU-6050): WHO_AM_I, burst read, int16 decode | `./mcs --sim examples/hardware/06_i2c_imu_registers.cs` |
| 07 | [`07_spi_device.cs`](hardware/07_spi_device.cs) | `SpiDevice` with automatic chip select | `./mcs --sim examples/hardware/07_spi_device.cs` |
| 08 | [`08_adc_voltmeter.cs`](hardware/08_adc_voltmeter.cs) | averaged ADC readings in millivolts, bar graph | `./mcs --sim examples/hardware/08_adc_voltmeter.cs` |
| 09 | [`09_dac_waveform.cs`](hardware/09_dac_waveform.cs) | DAC triangle wave, exact millivolt output | `./mcs --sim examples/hardware/09_dac_waveform.cs` |
| 10 | [`10_uart_echo.cs`](hardware/10_uart_echo.cs) | line protocol with `ReadLine` and a receive callback | `./mcs --sim examples/hardware/10_uart_echo.cs` |
| 11 | [`11_hardware_timer.cs`](hardware/11_hardware_timer.cs) | periodic and one-shot hardware timers | `./mcs --sim examples/hardware/11_hardware_timer.cs` |
| 12 | [`12_i2s_audio.cs`](hardware/12_i2s_audio.cs) | synthesize a sine tone, stream it over I²S, read a microphone | `./mcs --sim examples/hardware/12_i2s_audio.cs` |
| 13 | [`13_qspi_flash.cs`](hardware/13_qspi_flash.cs) | QSPI NOR flash: JEDEC id, page program, quad read, sector erase | `./mcs --sim examples/hardware/13_qspi_flash.cs` |
| 14 | [`14_can_bus.cs`](hardware/14_can_bus.cs) | CAN standard + extended frames, receive callback | `./mcs --sim examples/hardware/14_can_bus.cs` |
| 15 | [`15_watchdog_rtc.cs`](hardware/15_watchdog_rtc.cs) | watchdog, RTC, board information, feature detection | `./mcs --sim examples/hardware/15_watchdog_rtc.cs` |
| 16 | [`16_data_logger.cs`](hardware/16_data_logger.cs) | background jobs: sensor → CSV file + heartbeat LED | `./mcs --sim --ramfs 16384 --run-for 1200 examples/hardware/16_data_logger.cs` |

On a board, upload any of them with
`python3 tools/mcs_remote.py --port /dev/ttyACM0 put examples/hardware/05_i2c_temperature.cs /main.cs + run /main.cs`,
or paste it into the REPL (Ctrl-E, paste, Ctrl-D). Adjust pin and bus numbers to your wiring.

## Displays with u8g2 (`examples/u8g2/`, optional)

These need a firmware built with u8g2 ([docs/U8G2.md](../docs/U8G2.md)); on the PC:
`make fetch-u8g2 mcs-u8g2`, then run them with `./build/mcs_u8g2 --oled …` — a simulated
SSD1306 draws every frame in the terminal.

| File | What it shows | Run on the host |
|---|---|---|
| [`hello.cs`](u8g2/hello.cs) | an I²C SSD1306: fonts, text, a frame | `./build/mcs_u8g2 --oled examples/u8g2/hello.cs` |
| [`graphics.cs`](u8g2/graphics.cs) | shapes, XOR, lines, arcs, polygon, XBM bitmap, an animation | `./build/mcs_u8g2 --oled examples/u8g2/graphics.cs` |
| [`page_mode.cs`](u8g2/page_mode.cs) | a 128-byte page buffer with `Draw(...)` / `FirstPage`/`NextPage`, rotation | `./build/mcs_u8g2 --oled examples/u8g2/page_mode.cs` |
| [`fonts.cs`](u8g2/fonts.cs) | built-in fonts and fonts loaded from `/fonts` files, UTF-8 symbols, icons | `./build/mcs_u8g2 --oled --fs build/u8g2fs examples/u8g2/fonts.cs` (after `make u8g2-test`) |
| [`u8x8_text.cs`](u8g2/u8x8_text.cs) | U8x8: text without a frame buffer, 2x2 text, inverse | `./build/mcs_u8g2 --oled examples/u8g2/u8x8_text.cs` |
| [`menu.cs`](u8g2/menu.cs) | u8g2 menus, message box and number input on three buttons | `./build/mcs_u8g2 --oled --time-limit 2000 examples/u8g2/menu.cs` |
| [`sensor_log.cs`](u8g2/sensor_log.cs) | a scrolling text log (u8log) with ADC readings | `./build/mcs_u8g2 --oled examples/u8g2/sensor_log.cs` |

## Language and applications

| File | What it shows | Run on the host |
|---|---|---|
| [`tour.cs`](tour.cs) | a quick tour of the language: classes, tuples, `out`, LINQ, ranges, patterns, closures, exceptions — output identical to .NET 8 | `./mcs examples/tour.cs` |
| [`blink.cs`](blink.cs) | GPIO output + debounced button input with the scheduler | `./mcs --sim --run-for 3000 examples/blink.cs` |
| [`sensor_logger.cs`](sensor_logger.cs) | I²C temperature sensor, switch-expression classification, CSV logging, statistics | `./mcs --sim --ramfs 32768 --run-for 5500 examples/sensor_logger.cs` |

## Embedding in C

| File | What it shows | Run on the host |
|---|---|---|
| [`quickstart_embed.c`](quickstart_embed.c) | Option 1 in 60 lines: pool heap, HAL, a C function called from C#, a C# function called from C | `make quickstart` |
| [`firmware_example.c`](firmware_example.c) | limits, a custom HAL table, C bindings, events, running a flash image (`app.cs` → `app_image.h`) | `make example` |
| [`lowram/`](lowram/) | firmware for a ~48 KB-RAM MCU: lowram profile from its `mcs_user_config.h`, image executed in place from flash, 32 KB pool | `make example-lowram` |

Complete firmware projects (Option 2, the REPL) live with the ports:
[rp2](../ports/rp2/example) · [esp32](../ports/esp32/example) · [stm32](../ports/stm32/example_main.c) ·
[zephyr](../ports/zephyr/example) · [arduino](../ports/arduino/examples).

`--sim` attaches the simulator board (its I²C sensor always reads 25 °C; UART, SPI, I²S and
CAN are loopbacks); add `--sim-log` to trace every peripheral access.
