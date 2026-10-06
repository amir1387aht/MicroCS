# MicroCS for Raspberry Pi RP2040 / RP2350 (pico-sdk)

Raspberry Pi Pico, Pico W, Pico 2 (Arm or RISC-V cores) and any RP2040/RP2350 board.

## Quick start

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S ports/rp2/example -B build/pico -DPICO_BOARD=pico      # or pico2, pico_w
cmake --build build/pico
# copy build/pico/microcs_pico.uf2 to the board (hold BOOTSEL while plugging in)
```

Open the USB serial port (any baud rate) and type C# at the `> ` prompt. The whole firmware
is [`example/main.c`](example/main.c), ~30 lines.

## Pins

```c
mcs_rp2_cfg_t pins = MCS_RP2_CFG_DEFAULT;     // Pico defaults:
// UART0 GP0/GP1, UART1 GP4/GP5, I2C0 GP8/GP9, I2C1 GP6/GP7,
// SPI0 GP18/19/16 (SCK/MOSI/MISO), SPI1 GP10/11/12
pins.i2c[0] = (mcs_rp2_i2c_pins_t){ 4, 5 };   // change any bus
mcs_rp2_hal_init(&hal, &pins);
```

| C# | RP2 |
|---|---|
| pins | GPIO numbers; `"GP15"`, `"GPIO15"`, `"LED"` (on-board LED where defined) |
| `GPIO.OnChange` | GPIO IRQ callback |
| `UART` | interrupt-driven receive ring |
| `PWM.Set(gpio, ...)` | every GPIO can do PWM (channel = GPIO number) |
| `ADC.Read(0..3)` | GP26–GP29; `ADC.Read(4)` = internal temperature sensor |
| `Timer` | hardware alarm pool |
| `Watchdog`, `RTC`, `Hal.UniqueId` | hardware watchdog, software clock on the 64-bit µs timer (set it with `RTC.Set`), flash unique id |

Not available on this chip: DAC, CAN, QSPI for user devices; I²S needs PIO (planned).

## Checked in CI

The example firmware is built for `pico` (RP2040) and `pico2` (RP2350).
