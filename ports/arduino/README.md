# MicroCS for Arduino

Any 32-bit Arduino core with ~48 KB of free RAM: ESP32, RP2040 (Earle Philhower / Mbed),
SAMD21/51, nRF52, STM32duino, Teensy, Renesas (UNO R4), Portenta. AVR boards are too small.
Status: beta.

## Install

```sh
python3 tools/make_arduino.py      # → dist/arduino/MicroCS-1.4.0.zip
```

Arduino IDE: *Sketch → Include Library → Add .ZIP Library…*. PlatformIO:
`lib_deps = https://github.com/amir1387aht/MicroCS` with `framework = arduino`.

## Examples

* [`MicroCS_REPL`](examples/MicroCS_REPL/MicroCS_REPL.ino) — the board becomes a C# REPL on
  `Serial` (Option 2 in the main README).
* [`MicroCS_Embed`](examples/MicroCS_Embed/MicroCS_Embed.ino) — your sketch keeps `loop()`
  and runs C# from it (Option 1).

## Mapping

```cpp
mcs_arduino_cfg_t cfg = MCS_ARDUINO_CFG_DEFAULT;      // Serial, Wire, SPI
cfg.uart[1] = MCS_ARDUINO_UART(Serial1);               // any HardwareSerial / Stream class
mcs_arduino_hal_init(&hal, &cfg);
```

| C# | Arduino |
|---|---|
| pins | Arduino pin numbers; `GPIO.Pin("A0")`, `"LED"` = `LED_BUILTIN` |
| `GPIO.OnChange` | `attachInterrupt` (8 pins at a time) |
| `UART` | `Serial`, `Serial1`… |
| `I2C` / `SPI` | `Wire` / `SPI` (define `MCS_ARDUINO_NO_WIRE` / `MCS_ARDUINO_NO_SPI` to drop them) |
| `ADC` / `DAC` / `PWM` | `analogRead` / `analogWrite` on DAC pins / `analogWrite` |
| `Timer` | software timers, run from the REPL loop / `Hal.Poll()` |
| `RTC` | software clock from `millis()` |
