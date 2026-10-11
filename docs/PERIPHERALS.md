# Peripheral configuration (HAL API v3)

Every peripheral can be opened with a **configuration object** that carries the
pins, pulls, drive strength, filters, DMA and board-specific options — the
settings a vendor SDK would take in its init struct. Nothing has to be set:
every property starts at *board default*, and only what a script sets is sent
to the board.

```csharp
var oled = new SpiConfig(20_000_000, 0, 6, 7) { Cs = 10, Dma = true };
SPI.Open(1, oled);

GPIO.Configure(4, new GpioConfig(GPIO.Input, GPIO.PullUp) { Schmitt = true, FilterNs = 100 });
var led = new Pin("LED", new GpioConfig(GPIO.Output) { DriveMilliamps = 12, Initial = 1 });

UART.Open(1, new UartConfig(9600, 17, 16) { Parity = UART.ParityEven, RxBuffer = 1024 });
I2C.Open(0, new I2cConfig(400_000, 21, 22) { PullUps = false, TimeoutMs = 50 });
PWM.Configure(2, new PwmConfig(20_000, 25) { ResolutionBits = 10, Inverted = true });
ADC.Configure(0, new AdcConfig(ADC.Atten12dB) { Oversample = 4 });
```

A step-by-step introduction is in the [peripheral settings guide](guides/PERIPHERALS.md);
asynchronous transfers and `DmaBuffer` are in [DMA](DMA.md).

**Contents:** [Rules](#rules) · [Classes](#classes) · [Board options](#board-options) ·
[What each board supports](#what-each-board-supports) · [Ports: implementing v3](#ports-implementing-v3)

## Rules

- **Defaults.** Numbers start at `0` (= board default), pins at `-1`
  (`MCS_PIN_DEFAULT` = the board's pin), tri-state properties (`Dma`,
  `Schmitt`, `PullUps` …) at `null` (= board default; `true` / `false` force
  it). Assign `null` to go back to the default; `Reset()` clears everything.
- **Validation.** Values outside the documented range throw
  `ArgumentOutOfRangeException` when they are assigned. The board validates the
  combination when the object is used and throws `ArgumentException`
  (`invalid argument`) or `NotSupportedException` (`not supported`) with a short
  explanation, e.g. `GPIO.Configure: invalid argument - DriveMilliamps: 2, 4, 8 or 12`.
  A setting is **never silently ignored**.
- **Boards without full configuration.** `Hal.Has("SPI.Configure")` (and
  `GPIO.`, `UART.`, `I2C.`, `QSPI.`, `PWM.`, `ADC.`) tells whether the board
  honours the whole object. Where it does not, the basic fields still work
  (GPIO `Mode` / `Pull` / `Initial`; UART `Baud` / `DataBits` / `Parity` /
  `StopBits`; SPI `Frequency` / `Mode` / `Bits` / `LsbFirst` / `Cs` /
  `CsActiveHigh`; I2C, QSPI, PWM `Frequency`) and any other property that is
  set throws `NotSupportedException` naming it.
- **Pins** accept numbers or names (`"GPIO21"`, `"PA5"`, `"LED"`), like every
  other HAL call.
- **Objects are plain values**: build one, reuse it for several buses, change
  a property and open again. `ToString()` lists the properties that are set.
- **Common members:** `Set(name, value)` (a property or a board option, returns
  the object so calls chain), `Get(name)`, `Reset()`, `ToString()`.

## Classes

Constructor arguments are positional shortcuts for the first properties;
object initializers (`{ Cs = 5 }`) set the rest.

### GpioConfig — `GPIO.Configure(pin, cfg)`, `new Pin(pin, cfg)`, `pin.Configure(cfg)`
`new GpioConfig([mode, pull])`

| Property | Values |
|---|---|
| `Mode` | `GPIO.Input`, `Output`, `InputPullUp`, `InputPullDown`, `OpenDrain`, `Analog` |
| `Pull` | `GPIO.PullDefault` (what the mode implies), `PullNone`, `PullUp`, `PullDown`, `PullBoth` (bus keeper) |
| `Speed` | slew rate / output speed: `GPIO.SpeedDefault`, `SpeedLow`, `SpeedMedium`, `SpeedHigh`, `SpeedVeryHigh` |
| `DriveMilliamps` | drive strength in mA, `0` = default |
| `Schmitt` | tri-state: input hysteresis |
| `Hold` | tri-state: latch the pad state through deep sleep / reset |
| `Invert` | tri-state: invert input and output in hardware |
| `Initial` | `0` / `1`: level set *before* the pin starts driving (no glitch), `-1` = keep |
| `Alternate` | alternate function / pad mux number (STM32 AF, RP2 `GPIO_FUNC_*`), `-1` = GPIO |
| `FilterNs` | input glitch filter in ns, `0` = off |

### UartConfig — `UART.Open(port, cfg)`
`new UartConfig([baud, tx, rx])`

| Property | Values |
|---|---|
| `Baud`, `DataBits` (5–9), `Parity` (`UART.ParityNone/Odd/Even`), `StopBits` (1–2), `FlowControl` | frame |
| `Tx`, `Rx`, `Rts`, `Cts` | pins |
| `RxBuffer`, `TxBuffer` | driver buffer sizes in bytes, `0` = default |
| `InvertTx`, `InvertRx` | inverted line levels |
| `Rs485` | half duplex, RTS drives the transceiver DE pin |
| `Dma` | tri-state |
| `RxTimeout` | idle time in symbols before buffered bytes are reported |

### I2cConfig — `I2C.Open(bus, cfg)`
`new I2cConfig([frequency, sda, scl])`

| Property | Values |
|---|---|
| `Frequency` | up to 1 MHz, `100000` default |
| `Sda`, `Scl` | pins |
| `PullUps` | tri-state: internal pull-ups (`false` with external resistors) |
| `Dma` | tri-state |
| `TimeoutMs` | per transfer, `0` = default |
| `GlitchFilter` | digital filter length in clock cycles |
| `AnalogFilter` | tri-state |

### SpiConfig — `SPI.Open(bus, cfg)`, `new SpiDevice(bus, cs, cfg)`
`new SpiConfig([frequency, mode, sck, mosi, miso])`

| Property | Values |
|---|---|
| `Frequency`, `Mode` (0–3), `Bits`, `LsbFirst` | bus format |
| `Sck`, `Mosi`, `Miso` | pins |
| `Cs`, `CsActiveHigh` | chip select driven by MicroCS for `SPI.Transfer/Write/Read` without a `cs` argument |
| `ThreeWire` | half duplex on one data line (MOSI): use `SPI.Write` / `SPI.Read` |
| `Dma` | tri-state; `false` forces CPU transfers |
| `InputDelayNs` | MISO sampling delay compensation |
| `MaxTransfer` | largest single transfer in bytes (sizes driver DMA descriptors) |

### QspiConfig — `QSPI.Open(bus, cfg)`
`new QspiConfig([frequency])`

| Property | Values |
|---|---|
| `Frequency`, `Mode` (0 or 3), `Ddr` | bus format |
| `Dma` | tri-state |
| `CsHighCycles`, `SampleShift`, `FlashSizeLog2` | STM32 QUADSPI / OCTOSPI timing and memory size (2^n bytes) |
| `Sck`, `Cs`, `Io0` … `Io3` | pins |

### PwmConfig — `PWM.Configure(ch, cfg)`
`new PwmConfig([frequency, pin])`. `PWM.Set` / `SetPulse` / `Servo` keep the configuration.

| Property | Values |
|---|---|
| `Frequency` | `0` = keep (set by the next `PWM.Set`) |
| `Pin` | output pin for the channel |
| `ResolutionBits` | duty resolution, `0` = finest for the frequency |
| `Inverted` | active-low output |
| `CenterAligned` | phase-correct / centre-aligned counting (same frequency) |

### AdcConfig — `ADC.Configure(ch, cfg)`
`new AdcConfig([attenuation, resolutionBits])`

| Property | Values |
|---|---|
| `ResolutionBits` | `0` = native; `ADC.Resolution` follows it |
| `Attenuation` | input range: `ADC.AttenDefault`, `Atten0dB`, `Atten2_5dB`, `Atten6dB`, `Atten12dB` |
| `Oversample` | samples averaged per reading (power of two) |
| `SampleTime` | sampling time in ADC clock cycles |

## Board options

Settings that only one chip family has are **board options**: named integers
with a range, listed by the board itself.

```csharp
foreach (var o in Hal.Options("SPI")) Console.WriteLine(o);   // SPI.queue_size (1..16): transactions queued in the driver
var s = new SpiConfig(40_000_000).Set("queue_size", 8);
```

`Hal.Options()` lists all of them. `Set` checks the name and range against
the board's table (`ArgumentException` for unknown names); each object holds up
to `MCS_HAL_MAX_OPTS` options (default 4).

## What each board supports

| | GPIO | UART | I²C | SPI | QSPI | PWM | ADC | DMA |
|---|---|---|---|---|---|---|---|---|
| **ESP32** (all chips) | pull, drive, hold, invert, filter¹ | pins, buffers, invert, RS-485, RX timeout | pins, pull-ups, filter, timeout | pins, 3-wire, input delay, max transfer | pins, mode | pin, resolution, invert | resolution², attenuation, oversample | SPI, QSPI, memory³ |
| **RP2040 / RP2350** | pull, drive, slew, Schmitt, invert, alt function | pins, invert, RTS/CTS | pins, pull-ups, spike filter, timeout | pins | — | resolution, invert, centre-aligned | oversample | SPI, memory |
| **STM32** | pull, speed, open drain, AF (not F1) | basic | basic | basic | basic | invert, centre-aligned | basic | — |
| Zephyr, Arduino, Cortex-M | basic | basic | basic | basic | basic | basic | basic | — |
| Simulator (`--sim`) | everything (validated, recorded) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | everything |

¹ glitch filter where the chip has one (ESP32-C3/C6/H2/S3/P4 …). ² ESP32 9–12 bit,
ESP32-S2 13 bit. ³ async memcpy (GDMA chips). "basic" = the fields listed under
[Rules](#rules). Peripheral pins on STM32 come from CubeMX.

## Ports: implementing v3

All v3 members of `mcs_hal_t` are optional; `MCS_HAL_API_VERSION` is 3.

```c
hal->spi_configure = my_spi_configure;   /* int (*)(void* ctx, int bus, const mcs_spi_config_t* cfg) */
hal->options = my_options;               /* { "SPI", "queue_size", 1, 16, "help" }, ..., { NULL } */
```

- Initialise structs with `mcs_*_config_init()`; read board options with
  `mcs_hal_opt(hal->options, &cfg->opts, "SPI", "queue_size", 4)`.
- Return `MCS_HAL_ENOTSUP` / `MCS_HAL_EINVAL` for anything the hardware cannot
  do and call `mcs_hal_error("ThreeWire: full duplex only")` first so the
  script sees why. Never ignore a field that is set.
- `three_wire`: on a bus opened with `ThreeWire = true`, `spi_transfer` (and
  `spi_transfer_async`) get exactly one of `tx` / `rx`, the other NULL, of any
  length. Everywhere else the v1/v2 contract holds: both buffers are set
  (one-way transfers go through the stack in `MCS_HAL_MAX_XFER` pieces). A
  full-duplex `SPI.Transfer` on a three-wire bus throws `NotSupportedException`.
- `*_configure` replaces `*_open` for that bus; the library still calls
  `spi_open` / `i2c_open` for the plain `Open(bus, hz)` overloads.

See [PORTING](PORTING.md) and `ports/template/mcs_port_template.c`.
