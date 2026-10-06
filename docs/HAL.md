# Hardware API (modules/hal, `MCS_ENABLE_HAL`)

MicroCS talks to hardware through one C table, `mcs_hal_t` ([`include/mcs_hal.h`](../include/mcs_hal.h)).
A port fills in the functions its chip has; MicroCS turns them into C# classes. You normally
do not write the table yourself — pick a port:

| Port | Init call | Docs |
|---|---|---|
| STM32 (Cube HAL, every family) | `mcs_stm32_hal_init(&hal, &board)` | [ports/stm32](../ports/stm32/README.md) |
| ESP32 / S2 / S3 / C3 / C6 / H2 / P4 (ESP-IDF 5) | `mcs_esp32_hal_init(&hal, &pins)` | [ports/esp32](../ports/esp32/README.md) |
| RP2040 / RP2350 (pico-sdk) | `mcs_rp2_hal_init(&hal, &pins)` | [ports/rp2](../ports/rp2/README.md) |
| Zephyr (devicetree) | `mcs_zephyr_hal_init(&hal, NULL)` | [ports/zephyr](../ports/zephyr/README.md) |
| Arduino (any 32-bit core) | `mcs_arduino_hal_init(&hal, &cfg)` | [ports/arduino](../ports/arduino/README.md) |
| Simulator (host, tests) | `mcs_hal_sim_init(&hal, &sim)` | [below](#simulator-board) |
| Your own | fill `mcs_hal_t` | [below](#writing-a-board-table) |

Then `mcs_hal_open_lib(vm, &hal)` (library use) or `cfg.hal = &hal` (`mcs_runtime`).

## Concepts

* **Static and object APIs.** Every peripheral has static methods (`GPIO.Write(13, 1)`,
  `I2C.Read(0, 0x48, 2)`); the common ones also have objects that remember their
  settings (`new Pin("PA5", GPIO.Output)`, `new I2cDevice(0, 0x48)`, `new SpiDevice(0, 10, 8_000_000, 0)`).
* **Pins** are numbers or names. Names are resolved by the port (`"PA5"`, `"GPIO21"`,
  `"GP15"`, `"P0.13"`, `"LED"`, `"A0"`), so the same script reads naturally on each board.
  `GPIO.Pin("PC13")` returns the number.
* **Only what exists is registered.** If the board has no CAN, there is no `CAN` class and
  `Hal.Has("CAN")` is false — scripts can adapt: `if (Hal.Has("DAC")) DAC.Write(0, 2048);`.
  Names for `Has`: `GPIO GPIO.IRQ UART I2C SPI ADC DAC PWM Timer I2S QSPI CAN Watchdog RTC`.
* **Errors are exceptions.** Driver results map to `IOException` (bus error, NACK),
  `TimeoutException`, `NotSupportedException` (pin/bus not available) and
  `ArgumentException` / `ArgumentOutOfRangeException` (bad values). Catch them like in .NET.
* **Transfers** are up to `MCS_HAL_MAX_XFER` (256) bytes per call; data can be `byte[]`,
  `List<byte>` or a `string`.

## Interrupts, timers and events

Callbacks never run inside the ISR. The driver queues an event (`mcs_hal_post()`, lock-free
and ISR-safe) and the VM runs the C# callback at the next safe point — between statements,
during `Thread.Sleep`, in the REPL loop, or when you call `Hal.Poll()`. So a handler may
allocate, print, use I²C, throw… without any of MicroPython's hard-IRQ restrictions.

```csharp
var button = new Pin(2, GPIO.InputPullUp);
button.OnChange(GPIO.Falling, (bool level) => Console.WriteLine("pressed"));   // 1 arg: level
GPIO.OnChange(3, GPIO.Both, (int pin, bool level) => { /* 2 args: pin + level */ });
GPIO.Off(3);                                                 // disable

Timer.Start(0, 1000, () => samples.Add(ADC.Read(0)));         // every 1000 µs
Timer.Once(1, 500_000, () => Console.WriteLine("0.5 s later"));
UART.OnReceive(1, (int available) => Console.Write(UART.ReadString(1, available)));
CAN.OnReceive(0, (int pending) => { CanFrame f; while ((f = CAN.Receive(0)) != null) Handle(f); });

Hal.OnEvent(5, (int source, int value) => { });               // your own C events:
// C side: mcs_hal_post(MCS_HAL_EV_USER + 5, source, value);  (from any ISR)
Hal.Run(10_000);                                              // dispatch events for 10 s
```

Up to `MCS_HAL_MAX_CALLBACKS` (16) handlers; the queue holds `MCS_HAL_EVENT_QUEUE` (32)
events, overflow is counted in `Hal.DroppedEvents`. When you drive the VM yourself, call
`mcs_hal_poll(vm)` from your main loop (the runtime, the shell and the scheduler already do).

## C# reference

Optional arguments are shown in `[brackets]`.

### GPIO and Pin
| Member | Description |
|---|---|
| `GPIO.Mode(pin, mode)` | `GPIO.Input`, `Output`, `InputPullUp`, `InputPullDown`, `OpenDrain`, `Analog` |
| `GPIO.Write(pin, value)` | `bool` or `int` (0 = low) |
| `GPIO.Read(pin)` → `bool` · `GPIO.Toggle(pin)` | |
| `GPIO.Pin(name)` → `int` | resolve `"PA5"`, `"GPIO21"`, `"LED"` … |
| `GPIO.OnChange(pin, edge, callback)` · `GPIO.Off(pin)` | edges `GPIO.Rising`, `Falling`, `Both` |
| `GPIO.PulseIn(pin, level[, timeoutUs])` → µs | length of a pulse (ultrasonic sensors, IR) |
| `new Pin(pin[, mode])` | `Write(v)`, `Read()`, `Toggle()`, `High()`, `Low()`, `SetMode(m)`, `OnChange(edge, fn)`, `Value` (get/set), `Number` |

### UART
| Member | Description |
|---|---|
| `UART.Open(port, baud[, dataBits, parity, stopBits])` | parity `UART.ParityNone`, `ParityOdd`, `ParityEven` |
| `UART.Write(port, data)` · `UART.WriteLine(port[, text])` | string or bytes |
| `UART.Read(port, max[, timeoutMs])` → `byte[]` · `UART.ReadString(...)` → `string` | returns what arrived |
| `UART.ReadLine(port[, timeoutMs = 1000])` → `string` or `null` | without the line ending |
| `UART.Available(port)` · `UART.OnReceive(port, fn(int available))` · `UART.Close(port)` | |

### I²C
| Member | Description |
|---|---|
| `I2C.Open(bus[, hz = 100000])` | |
| `I2C.Write(bus, addr, data)` · `I2C.Read(bus, addr, n)` → `byte[]` | 7-bit addresses |
| `I2C.WriteRead(bus, addr, data, n)` → `byte[]` | repeated start |
| `I2C.ReadRegister(bus, addr, reg)` · `ReadRegisters(bus, addr, reg, n)` · `WriteRegister(bus, addr, reg, value or bytes)` | register-style devices |
| `I2C.Scan(bus)` → `List<int>` | responding addresses |
| `new I2cDevice(bus, addr)` | `Write`, `Read`, `WriteRead`, `ReadRegister`, `ReadRegisters`, `WriteRegister`, `Address`, `Bus` |

### SPI
| Member | Description |
|---|---|
| `SPI.Open(bus[, hz = 1 MHz, mode = 0, lsbFirst = 0])` | mode 0–3 |
| `SPI.Transfer(bus, data[, csPin])` → `byte[]` | full duplex |
| `SPI.Write(bus, data[, csPin])` · `SPI.Read(bus, n[, csPin])` | |
| `new SpiDevice(bus, csPin, hz, mode)` | `Transfer(data)`, `Write(data)`, `Read(n)`, `WriteRead(cmd, n)` — CS handled, bus re-configured per device |

### ADC, DAC, PWM
| Member | Description |
|---|---|
| `ADC.Read(ch)` → raw · `ADC.ReadMillivolts(ch)` · `ADC.ReadVoltage(ch)` → `double` | calibrated where the chip supports it (ESP32) |
| `ADC.ReadAverage(ch[, n = 16])` · `ADC.Resolution` · `ADC.ReferenceMillivolts` | |
| `DAC.Write(ch, value)` · `DAC.WriteMillivolts(ch, mV)` · `DAC.Resolution` | |
| `PWM.Set(ch, hz, duty 0.0–1.0)` · `PWM.SetPermille(ch, hz, 0–1000)` | 16-bit duty where the port supports it |
| `PWM.SetPulse(ch, hz, pulseUs)` · `PWM.Servo(ch, degrees[, minUs = 500, maxUs = 2500])` | servos at 50 Hz |
| `PWM.Tone(ch, hz)` · `PWM.Stop(ch)` | buzzers |

### Timer, Watchdog, RTC, Hal
| Member | Description |
|---|---|
| `Timer.Start(id, periodUs, fn)` · `Timer.Once(id, delayUs, fn)` · `Timer.Stop(id)` | hardware timers |
| `Watchdog.Start(timeoutMs)` · `Watchdog.Feed()` | once started, the script must feed it |
| `RTC.Now` → Unix seconds · `RTC.Set(seconds)` | |
| `Hal.Board`, `Hal.ApiVersion`, `Hal.CpuHz`, `Hal.UniqueId` (hex string), `Hal.Micros` | |
| `Hal.Has(name)`, `Hal.Poll()`, `Hal.Run([ms])`, `Hal.DelayMicroseconds(us)`, `Hal.Reset()` | |
| `Hal.OnEvent(n, fn(source, value))`, `Hal.Post(n[, source, value])`, `Hal.DroppedEvents` | user events |

### I²S, QSPI, CAN
| Member | Description |
|---|---|
| `I2S.Open(bus, rate, bits, channels[, direction = I2S.Tx, format])` | `I2S.Tx`, `Rx`, `Duplex` |
| `I2S.WriteSamples(bus, int[])` · `I2S.ReadSamples(bus, n)` → `int[]` | signed samples, packed to `bits` |
| `I2S.Write(bus, bytes[, timeoutMs])` · `I2S.Read(bus, n[, timeoutMs])` · `I2S.Close(bus)` | raw bytes |
| `QSPI.Open(bus[, hz])` · `QSPI.Command(bus, instr[, address])` | NOR-flash style commands |
| `QSPI.Read(bus, instr, address, n[, dummy, dataLines, addrBytes])` · `QSPI.Write(bus, instr, address, data[, dataLines, addrBytes])` | address `-1` = no address phase |
| `QSPI.Transfer(bus, instr, instrLines, address, addrBytes, addrLines, dummy, dataLines, bytes or count)` | every phase explicit (OSPI, displays, PSRAM) |
| `CAN.Open(bus[, bitrate = 500000])` · `CAN.Send(bus, id, data[, extended])` · `CAN.Send(bus, frame)` | classic CAN / TWAI |
| `CAN.Receive(bus[, timeoutMs])` → `CanFrame` or `null` · `CAN.OnReceive(bus, fn(int pending))` | |
| `new CanFrame(id, data[, extended])` | `Id`, `Extended`, `Remote`, `Length`, `Data`, `ToString()` |

### Byte helpers
`Encoding.UTF8` / `Encoding.ASCII` (`GetBytes`, `GetString(bytes[, index, count])`,
`GetByteCount`) and little-endian `BitConverter` (`GetBytes`, `ToInt16`, `ToUInt16`,
`ToInt32`, `ToUInt32`, `ToInt64`, `ToSingle`, `ToDouble`, `ToBoolean`, `ToString` →
`"01-AB-FF"`) are registered together with the HAL — they are what drivers need for packets.

## Writing a board table

Fill only what your hardware has; every member is optional. Functions return `>= 0` on success
or a negative `MCS_HAL_E*` code.

```c
#include "mcs_hal.h"

static int my_mode(void* ctx, int pin, int mode) { gpio_config(pin, mode); return 0; }
static int my_write(void* ctx, int pin, int v)   { gpio_set(pin, v); return 0; }
static int my_read(void* ctx, int pin)           { return gpio_get(pin); }
static int my_irq(void* ctx, int pin, int edge)  { exti_enable(pin, edge); return 0; }

void EXTI_IRQHandler(void) {                      /* your interrupt */
    int pin = exti_pending_pin();
    mcs_hal_post(MCS_HAL_EV_GPIO, pin, gpio_get(pin));   /* ISR-safe */
}

static const mcs_hal_t board = {
    .board = "my-board",
    .gpio_mode = my_mode, .gpio_write = my_write, .gpio_read = my_read, .gpio_irq = my_irq,
    .i2c_write = my_i2c_write, .i2c_read = my_i2c_read,
    .adc_read = my_adc, .adc_bits = 12,
};
mcs_hal_open_lib(vm, &board);
```

Events can also be delivered from an RTOS queue via `.poll_event` instead of `mcs_hal_post`
(the ESP32 port does this). [`ports/template/mcs_port_template.c`](../ports/template/mcs_port_template.c)
lists every member with a stub. Board tables written for HAL v1 (1.3) compile unchanged.

## Simulator board

`./mcs --sim` and the tests use `mcs_hal_sim_init()`: 64 GPIOs (outputs read back; an output
with an interrupt fires on its own edges), UART loopback, I²C devices at 0x48 (TMP102, 25 °C),
0x50 (256-byte EEPROM) and 0x68 (register file, `WHO_AM_I` = 0x68), SPI loopback, 12-bit
ADC/DAC, PWM, 4 timers, I²S loopback, CAN loopback and a 4 KB QSPI NOR flash (JEDEC
`EF 40 16`). `--sim-log` traces every access; `--sim-virtual` uses a deterministic clock.
