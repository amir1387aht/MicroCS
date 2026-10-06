# MicroCS for Zephyr RTOS

One port for every board Zephyr supports (nRF52/53/54, NXP i.MX RT / Kinetis / LPC, STM32,
Atmel SAM, RP2040, ESP32, Renesas, Silicon Labs…), using Zephyr's portable driver APIs.
Status: beta.

## Quick start

```sh
west build -b nrf52840dk/nrf52840 ports/zephyr/example -- -DZEPHYR_EXTRA_MODULES=$PWD
west flash
```

or add MicroCS to your `west.yml` and set `CONFIG_MICROCS=y` in `prj.conf`.

## Devices come from the devicetree

| C# | Devicetree |
|---|---|
| `GPIO` | every `gpio0`, `gpio1`… / `gpioa`, `gpiob`… node; pin = port * 32 + pin, names `"P0.13"`, `"PA5"` |
| `UART.Open(n)` | alias `mcs-uartN` (UART 0 defaults to `zephyr,console`) |
| `I2C` / `SPI` | aliases `mcs-i2cN` / `mcs-spiN` (fallback: `i2c0`, `spi1`…) |
| `ADC.Read(n)` | n-th entry of `io-channels` in `/zephyr,user` |
| `PWM.Set(n, ...)` | n-th entry of `pwms` in `/zephyr,user` |
| `DAC` | alias `mcs-dac` |
| `CAN` | chosen `zephyr,canbus` |
| `Watchdog` | alias `watchdog0` |

See [`example/app.overlay`](example/app.overlay) for a template.
