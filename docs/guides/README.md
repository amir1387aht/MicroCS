# Step-by-step guides

Each guide starts from nothing and ends with your C# running on the board. Pick yours:

| You have | Guide | Time |
|---|---|---|
| just a PC | [Try MicroCS on the PC](PC_SIMULATOR.md) — compiler, REPL, simulated GPIO / I²C / SPI / OLED | 5 min |
| an ESP32, S2, S3, C2, C3, C6, H2 or P4 | [ESP32 from zero](ESP32.md) — release firmware in one command, or your own ESP-IDF build | 10–30 min |
| an ESP32-C3 (or any ESP32) **with an OLED** | [ESP32-C3 + OLED with U8g2](ESP32_C3_OLED.md) — turn U8g2 on, pick display and fonts, draw | 30 min |
| a Raspberry Pi Pico / Pico 2 / RP2040 board | [Raspberry Pi Pico](PICO.md) — drag-and-drop UF2, or build with the pico-sdk | 5–20 min |
| an STM32 Nucleo, Black Pill or your own STM32 board | [STM32](STM32.md) — ready firmware, or MicroCS inside a CubeMX project | 5–45 min |
| an Arduino-compatible board (ESP32, RP2040, nRF52840, SAMD51, Teensy…) | [Arduino IDE / PlatformIO](ARDUINO.md) | 15 min |
| an nRF52/53, NXP, STM32… board with Zephyr | [Zephyr](ZEPHYR.md) | 30 min |

After the first program:

* [Hardware API](../HAL.md) — every C# peripheral class (`GPIO`, `I2C`, `SPI`, `UART`, `PWM`…).
* [MicroCS Studio](../../tools/studio/README.md) — the browser IDE: editor, files and REPL over USB.
* [Examples](../../examples/README.md) — blink, buttons, sensors, displays, data loggers.
* [The C# subset](../LANGUAGE.md) — what MicroCS supports from C#.

Something does not work? Every guide ends with a *Troubleshooting* section; if that does not
help, open an [issue](https://github.com/amir1387aht/MicroCS/issues) with the board, the
build commands and what the console printed.
