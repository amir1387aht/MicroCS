# Hardware abstraction (modules/hal, `MCS_ENABLE_HAL`)

A board is one `mcs_hal_t` (see `include/mcs_hal.h`): a context pointer plus optional
function pointers for GPIO, UART, I2C, SPI, ADC and PWM. A NULL pointer means the
peripheral does not exist; the C# class is then not registered and `Hal.Has("SPI")`
returns false. Functions return ≥0 on success or `MCS_HAL_ERR` (-1), `MCS_HAL_ENOTSUP`
(-2), `MCS_HAL_ETIMEOUT` (-3), `MCS_HAL_ENODEV` (-4, e.g. I2C NACK), which become
`IOException`, `NotSupportedException`, `TimeoutException` in C#.

```c
static int my_gpio_write(void* ctx, int pin, int v) { HAL_GPIO_WritePin(..., v); return 0; }
static mcs_hal_t board = { .board = "my-board", .gpio_mode = my_gpio_mode,
                           .gpio_write = my_gpio_write, .gpio_read = my_gpio_read };
mcs_hal_open_lib(vm, &board);
```

## C# API
| Class | Members |
|---|---|
| `GPIO` | `Mode(pin, mode)`, `Write(pin, bool/int)`, `Read(pin)`, `Toggle(pin)`; `Input, Output, InputPullUp, InputPullDown, OpenDrain, High, Low` |
| `UART` | `Open(port, baud)`, `Write(port, string/byte[])`, `Read(port, max[, timeoutMs])` → byte[], `ReadString(port, max[, timeoutMs])`, `Available(port)` |
| `I2C` | `Write(bus, addr, byte[])`, `Read(bus, addr, n)`, `WriteRead(bus, addr, byte[], n)` |
| `SPI` | `Transfer(bus, byte[])` → byte[] (full duplex) |
| `ADC` | `Read(channel)` (raw counts), `Resolution` (bits) |
| `PWM` | `Set(channel, freqHz, duty 0..1)`, `SetPermille(channel, freqHz, 0..1000)` |
| `Hal` | `Board`, `Has(name)` |

Transfers are limited to `MCS_HAL_MAX_XFER` (256) bytes per call (C-stack buffers).
Calls are synchronous on the VM thread; interrupts/async callbacks are planned (use
`mcs_pin` + `mcs_call_value` from your main loop as in `examples/firmware_example.c`).

## Simulator board
`mcs_hal_sim_init(&hal, &sim)` — used by `./mcs --sim`, the tests and the Cortex-M
demo: 64 GPIO pins (outputs read back), UART loopback per port, I2C TMP102-style sensor
at 0x48 (25.00 °C) and a 256-byte EEPROM at 0x50, SPI loopback, ADC = channel*256+100
(12 bit), PWM values stored; optional log callback (`--sim-log`).

## Status
The API and simulator are tested. **No real board backend is included**; mapping it to
SiFli SDK / RT-Thread `rt_pin_*`, `rt_device_*` (UART), `rt_i2c_transfer`,
`rt_spi_transfer`, `rt_adc_read`, `rt_pwm_set` is a thin table — see PORTING.md.
CAN, I2S, RTC, watchdog, timers, display/LVGL and BLE classes are planned.
