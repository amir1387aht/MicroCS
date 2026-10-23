# Rotary encoders (A/B quadrature)

`RotaryEncoder` supports standard two-channel mechanical encoders such as EC11 and
KY-040, with an optional active-low push button. It is a native GPIO-interrupt
decoder, **not** an ADC input or a hardware PCNT/timer encoder peripheral.

## Wiring and C# API

Connect the encoder common terminal to GND, A/CLK and B/DT to two interrupt-capable
MCU inputs, and optionally SW to another input. The constructor enables internal
pull-ups. Use the board's logic voltage; do not feed 5 V into non-5-V-tolerant pins.
Module resistor networks and long wires may need external pull-ups / filtering.

```cs
// ESP32 example; select suitable pins for your actual board.
var encoder = new RotaryEncoder(32, 33, 4, 25);
try
{
    int previous = encoder.Position;
    for (int i = 0; i < 1000; i++)
    {
        int position = encoder.Position;
        if (position != previous)
        {
            Console.WriteLine($"position:{position}");
            previous = position;
        }
        Thread.Sleep(10);
    }
}
finally { encoder.Dispose(); }
```

Constructor: `new RotaryEncoder(a, b, steps = 4, button = -1, reverse = false)`.
Pins accept numbers or board names, as with `Pin`. A and B must be different;
the optional button must differ from both. `steps` is **transitions per reported
count**, not the encoder's advertised pulses/revolution. Choose 1, 2 or 4 to match
the encoder's detent pattern; the usual starting point is 4. No button is `-1`.
Swap A/B or pass `true` as the fifth argument to reverse direction. The positive
sequence is A/B `11 → 10 → 00 → 01 → 11`.

| Member | Behavior |
|---|---|
| `Position` | Atomic signed 32-bit count. Saturates at −2,147,483,648 / 2,147,483,647; motion away from the limit still counts. |
| `Reset(position = 0)` | Atomically sets the count and clears the incomplete transition accumulator. It does not drive the pins. |
| `ButtonPressed` | Raw active-low input; false when no button was supplied. This property is **not debounced**. |
| `Dispose()` | Disables both A/B IRQs and releases all reserved pins. Safe to call again. Other members then throw `ObjectDisposedException`. |

The Studio **Input devices → Rotary encoder** template includes simple push-button
handling requiring 30 ms continuously pressed before reset. Adjust this for your
switch; do not run application code in an ISR.

## Counting, ownership and memory

Both channels are sampled immediately in the GPIO ISR. Valid Gray-code transitions
accumulate; back-and-forth contact chatter cancels, and a two-bit jump clears the
partial count and resynchronizes. Counting does not depend on `Hal.Poll`, a C#
callback, or free slots in the event queue. Read `Position` from the main VM at the
rate your UI needs. There is no per-edge allocation and no C# encoder callback.

At most `MCS_HAL_MAX_ENCODERS` (default 4, process-wide) may be open. Each live
encoder uses one small pool-accounted native object and one pin handle; there is
no upfront encoder array in each VM. It remains pinned until `Dispose()` or HAL
shutdown. Use `try/finally` rather than relying on GC. Failed initialization
releases its pin handle and disables any IRQ it successfully acquired; inputs may
remain configured with pull-ups. Counting starts at the final constructor sample,
not during initialization.

GPIO/Pin mode changes, writes and IRQ changes on reserved pins are rejected.
An existing GPIO callback also prevents opening an encoder. Do not give the same
pins to SPI, PWM, a display driver, native SDK code or another VM's GPIO callback:
those interfaces are not a global pin multiplexer. Only the main event VM may
open an encoder; `MCS_HAL_NO_EVENTS` worker VMs cannot. `mcs_hal_close_lib`, HAL
rebinding and VM destruction release the encoder IRQs.

## Port support and limits

| Port | A/B capture | Pin constraints |
|---|---|---|
| ESP-IDF ESP32 family | GPIO ISR, before the private FreeRTOS event queue | Two direct interrupt-capable GPIOs; avoid flash/PSRAM/reserved pins. |
| RP2040 / RP2350 | Pico SDK bank-0 GPIO callback | Direct bank-0 pins, not CYW43 Wi-Fi GPIOs. Bare-metal IRQs must run on the VM core. |
| STM32 Cube HAL | EXTI callback | Two **different EXTI line numbers**, e.g. PA0 + PA1, not PA0 + PB0. |
| Arduino | `attachInterrupt(..., CHANGE)` on both pins | Both must map through `digitalPinToInterrupt`; consumes two IRQ slots. |
| Zephyr | GPIO callback before event enqueue | Direct SoC GPIOs supporting both edges and ISR-safe reads; not I2C/SPI expanders. |
| Host / Cortex-M reference simulator | `mcs_hal_sim_set_input` | Deterministic direct simulated GPIO edges; no physical encoder connected. |

This is intended for human-operated rotary controls. Interrupt masking, flash/cache
stalls and pulses faster than the port can service may lose transitions. The
decoder rejects observed illegal transitions, but cannot reconstruct unobserved
full cycles or guarantee lossless high-speed motor feedback. ESP32 support does not
claim an IRAM-safe interrupt chain. Use a native hardware counter backend when
those requirements apply. C# execution delays alone do not lose counts.

## Build and custom ports

`MCS_ENABLE_ENCODER` defaults to `MCS_ENABLE_HAL`; set it to 0 to omit the class.
CMake: `-DMICROCS_ENCODER=OFF`. ESP-IDF menuconfig: **MicroCS → Rotary encoders**.
Query `Hal.Has("RotaryEncoder")` before using the class on unknown firmware.
Older board tables remain compatible but do not advertise encoder capture.

HAL API v6 appends `gpio_capture`. Set it true only when `gpio_mode`, `gpio_read`
and `gpio_irq` are available and the GPIO producer performs this **inside the ISR**:

```c
if (!mcs_hal_gpio_edge(hal->ctx, pin))
    mcs_hal_post(MCS_HAL_EV_GPIO, pin, level);
```

The same rule applies to ports with a private SDK event queue. The helper samples
A/B using the board's `gpio_read`, which must be nonblocking and ISR-safe for those
pins. The exact HAL `ctx` identifies the board. A consumed edge must not also be
queued as an ordinary GPIO callback. Do not decode while draining `poll_event`.

The decoder shares the HAL critical-section lock. OS builds use the OS IRQ lock;
ESP32/Zephyr also have SDK locks without MicroCS threads, Pico bare-metal and
Cortex-M preserve IRQ state, and AVR preserves SREG. Other bare-metal targets must
supply `MCS_HAL_CRITICAL_ENTER/EXIT`. In a bare-metal multicore application, deliver
IRQs on the VM core or provide a common IRQ-safe multicore lock. Board/HAL tables
must outlive their VMs; close the HAL before tearing down the board SDK.
