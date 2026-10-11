# Peripheral settings: pins, pulls, speed and board options

`GPIO.Mode(pin, GPIO.Output)` and `SPI.Open(bus, hz)` cover most projects. When you need more
— a sensor on other pins, a pull-up turned off, a stronger LED driver, an inverted UART — every
peripheral takes a **configuration object** with all the settings. This guide shows how to use
them step by step. The full list of properties is in [Peripheral configuration](../PERIPHERALS.md).

You need MicroCS 1.12 or newer on a board, or just the PC simulator
([Try MicroCS on the PC](PC_SIMULATOR.md)): `mcs --sim --sim-virtual script.cs` accepts and checks
every setting.

In [MicroCS Studio](../STANDALONE.md) open **Templates** (`Alt+T`) → *Peripheral settings* for
ten ready-to-run versions of the examples below (button, LED, open drain, I2C / SPI / UART on
your own pins, PWM, ADC, board options), or type the snippets `gpiocfg`, `i2ccfg`, `spicfg`,
`uartcfg`.

## 1. Ask the board what it supports

```csharp
Console.WriteLine(Hal.Board);
Console.WriteLine($"GPIO {Hal.Has("GPIO.Configure")}  SPI {Hal.Has("SPI.Configure")}  I2C {Hal.Has("I2C.Configure")}");
foreach (var o in Hal.Options()) Console.WriteLine(o);     // settings only this chip has
```

`true` means the board honours the whole object. On boards that print `false` the basic
settings (frequency, mode, baud …) still work and anything else throws `NotSupportedException`
telling you which property — nothing is ever silently ignored.

## 2. A GPIO with exactly the electrical setup you want

```csharp
// button to ground: pull-up on, input hysteresis on
var button = new Pin(9, new GpioConfig(GPIO.Input, GPIO.PullUp) { Schmitt = true });

// LED: start ON without a glitch, strong drive, fast edges
var led = new Pin("LED", new GpioConfig(GPIO.Output) { Initial = 1, DriveMilliamps = 12, Speed = GPIO.SpeedHigh });

// change it later
led.Configure(new GpioConfig(GPIO.OpenDrain) { Pull = GPIO.PullNone });
```

Properties you do not set stay at the board default. If the hardware cannot do something, the
message says why:

```text
GPIO.Configure: invalid argument - DriveMa: 2, 4, 8 or 12 mA          (RP2)
GPIO.Configure: not supported - Hold: not available on STM32 GPIOs
```

## 3. Buses on your own pins

On ESP32 and RP2 the bus pins are chosen at run time:

```csharp
// I2C on GPIO 8 / 9 at 400 kHz, external pull-ups on the board
I2C.Open(0, new I2cConfig(400_000, 8, 9) { PullUps = false, TimeoutMs = 20 });
Console.WriteLine(string.Join(", ", I2C.Scan(0)));

// SPI display: SCK 6, MOSI 7, no MISO needed, chip select 10 handled by MicroCS
SPI.Open(1, new SpiConfig(20_000_000, 0, 6, 7) { Cs = 10 });
SPI.Write(1, new byte[] { 0xAF });

// UART for a GPS at 9600 baud on TX 17 / RX 16 with a bigger receive buffer
UART.Open(1, new UartConfig(9600, 17, 16) { RxBuffer = 2048 });
```

On STM32 the pins come from CubeMX; leave them unset there.

## 4. Reuse and tweak settings

Configuration objects are ordinary values:

```csharp
var fast = new SpiConfig(40_000_000, 0);
var slow = new SpiConfig(1_000_000, 3);
var flash = new SpiDevice(1, 5, fast);      // CS 5, re-configures the bus for every access
var adc = new SpiDevice(1, 4, slow);
Console.WriteLine(fast);                    // SpiConfig { Frequency = 40000000 }
fast.Frequency = 26_000_000;                // the object changes; Open / new SpiDevice again to apply
fast.Dma = null;                            // back to the board default
```

## 5. Board options

Settings only one chip family has are named options:

```csharp
foreach (var o in Hal.Options("SPI")) Console.WriteLine(o);
// SPI.queue_size (1..16): transactions the SPI driver can queue (default 4)
SPI.Open(1, new SpiConfig(40_000_000).Set("queue_size", 8));
```

`Set` checks the name and range against the board's list.

## 6. PWM and ADC

```csharp
// 10-bit, active-low PWM on GPIO 25 (ESP32: any pin; RP2: channel = GPIO)
PWM.Configure(0, new PwmConfig(20_000, 25) { ResolutionBits = 10, Inverted = true });
PWM.Set(0, 20_000, 0.25);

// ESP32: full 0..3.1 V range, 4 samples averaged per reading
ADC.Configure(0, new AdcConfig(ADC.Atten12dB) { Oversample = 4 });
Console.WriteLine(ADC.ReadMillivolts(0));
```

## Troubleshooting

* **`NotSupportedException: ... not supported - FilterNs`** — the board cannot do that setting;
  remove it, or check `Hal.Has("GPIO.Configure")` and the table in
  [Peripheral configuration](../PERIPHERALS.md#what-each-board-supports).
* **`ArgumentOutOfRangeException` when assigning** — the value is outside the property's range
  (e.g. `Mode = 4` on SPI).
* **`no I2C pins for this bus`** — the board has no default pins for that bus; set `Sda` / `Scl`
  (or `Sck` / `Mosi` / `Miso` for SPI).
* Next: move big transfers to the background with [DMA](DMA.md).
