# Rotary encoders: hardware first, software fallback

`RotaryEncoder` uses a native counter peripheral **before** considering GPIO
software decoding. The same C# API supports EC11 / KY-040-style A/B controls and
an optional active-low push button.

| Port | Preferred backend | When GPIO fallback is used |
|---|---|---|
| ESP-IDF ESP32 family | **PCNT** quadrature channels, x4 counting; SDK overflow accumulation on IDF ≥5.1 | SoC has no PCNT (for example ESP32-C2). |
| Arduino-ESP32 | **PCNT**, including the legacy IDF 4.x driver in Arduino-ESP32 2.x | SoC has no PCNT. |
| STM32 Cube HAL | **TIM encoder mode TI12**, using a CubeMX-bound CH1/CH2 pair | No native timer binding for the requested pair. |
| RP2040 / RP2350, Arduino-Pico (non-mbed) | **PIO** state machine maintains a 32-bit quadrature count independently of the CPU | Pins are not a consecutive pair in GPIO 0–31, or no PIO backend is compiled. |
| Zephyr / other Arduino cores / custom ports | Board-supplied native HAL counter hooks, if present | No native backend is bound. The supplied generic Zephyr/other-core adapters currently use GPIO fallback. |
| Simulator | GPIO fallback | No physical counter peripheral. |

PIO is programmable hardware offload, not a dedicated QEI peripheral and **not**
a CPU GPIO ISR. Native PCNT/TIM/PIO paths do not enable A/B GPIO interrupts or run
C# callbacks on every transition. PCNT and 16-bit TIM use infrequent counter-limit
interrupts for count extension; PIO and 32-bit TIM need no rollover ISR.

## C# API and wiring

Connect common to GND, A/CLK and B/DT to suitable MCU pins, and optionally SW to
another input. Use the MCU's logic voltage; do not feed 5 V to non-tolerant pins.
PCNT / PIO / GPIO fallback configure pull-ups. For TIM, configure pull-up alternate
function inputs in CubeMX: the constructor preserves those AF settings.

```cs
// ESP32 example: choose pins that are actually available on your board.
var encoder = new RotaryEncoder(32, 33, 4, 25);
try
{
    Console.WriteLine($"Encoder backend: {encoder.Backend}"); // PCNT, TIM, PIO or Software
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
Pins are numbers or board names. A/B/button must differ. `steps` is **transitions
per reported count**: 1, 2 or 4, not the encoder's advertised pulses/revolution.
Start with 4 for a typical mechanical encoder. `-1` omits the button. Direction
also depends on wiring / TIM input polarity; `reverse = true` flips the result.

| Member | Behavior |
|---|---|
| `Backend` | Actual selection: `PCNT`, `TIM`, `PIO`, `Software`, or a custom HAL backend name. No guessing from the chip name. |
| `Position` | Signed 32-bit position. Native raw x4 counts are scaled with a retained partial remainder. Results saturate at signed 32-bit limits when sampled. |
| `Reset(position = 0)` | Rebaselines the native counter and clears the partial remainder without stopping or clearing the peripheral. No transitions are lost to a stop/clear/restart sequence. |
| `ButtonPressed` | Raw active-low optional input, false if omitted. **Debounce in the application**; Studio's template includes a simple stable-press reset. |
| `Dispose()` | Stops/releases the native counter or fallback IRQs and releases pin reservations. Idempotent. Other members then throw `ObjectDisposedException`. |

`Hal.Has("RotaryEncoder")` reports API availability; `Backend` identifies the
counter actually selected for that instance. A **busy counter, allocation failure,
invalid configuration or read failure raises an error**. These errors never
silently downgrade to software. Only “not supported for this pair / board”
selects fallback. Incomplete native HAL hook sets also raise an error.

## STM32 setup

CubeMX must configure a suitable timer in **Encoder Mode TI1 and TI2 (TI12)**,
CH1=A and CH2=B, prescaler 0, appropriate AF inputs/pull-ups and input filtering.
The port uses the timer's full 16/32-bit counter range. The handle must not already
be running. Bind the exact MicroCS pin pair in the board table:

```c
// Example only: check TIM2 CH1/CH2 mapping for your exact MCU/package.
MX_TIM2_Init();
board.encoder[0] = (mcs_stm32_encoder_t){ .h = &htim2, .a = 0, .b = 1 }; // PA0 / PA1
```

For a **16-bit** timer, enable its update/global IRQ in CubeMX, have the generated
IRQ handler call `HAL_TIM_IRQHandler`, and forward `HAL_TIM_PeriodElapsedCallback`
to `mcs_stm32_tim_elapsed(htim)` (or set `MCS_STM32_DEFINE_TIM_CALLBACK=1` when the
port owns that callback). This extends wraps without a CPU interrupt per A/B
edge. A 32-bit timer, e.g. TIM2/TIM5 where available, does not need update IRQs for
count extension. Native TIM pins do **not** need different EXTI lines; that
restriction applies only to GPIO fallback. No binding means fallback, not an
automatically guessed pinmux.

Timer/PWM start, configure and stop operations reject a timer currently owned by
an encoder, including aliases of the same peripheral. Native open rejects an
already running timer. Use a dedicated encoder handle and don't reconfigure it
from native SDK code while open. The port never calls `GPIO.Mode` on native TIM
A/B pins, which would otherwise destroy their alternate-function configuration.

## Resource and memory rules

At most `MCS_HAL_MAX_ENCODERS` (default 4, process-wide) may be open. Each uses one
small pool-accounted userdata and one VM pin handle, allocated only when opened.
Native SDK resources are separate, reflected in device heap telemetry; they are
not a second VM pool. There is no per-transition VM allocation or callback queue.
Always use `try/finally`: live encoders remain pinned until disposed or HAL/VM
shutdown. Failed initialization releases acquired resources. HAL close/rebind
and VM destruction also release the encoder.

PCNT claims its SDK unit/channels; PIO claims an SM and a shared 24-instruction
program at offset 0, trying available PIO blocks without evicting other programs.
Busy peripherals report an error. On legacy IDF 4.x, reserve PCNT units
`0 .. min(MCS_HAL_MAX_ENCODERS, PCNT_UNIT_MAX)-1` for MicroCS: that SDK has no
public unit-claim allocator. Its shared ISR service is initialized once, not once
per upload. Do not reuse those units from native code.

GPIO/Pin mode changes, writes and IRQ changes on reserved pins are rejected;
an existing GPIO callback prevents encoder open. Other peripheral APIs are not
a global pin multiplexer: don't give encoder pins to SPI, PWM or native code.
Only the main event VM may own encoders, not `MCS_HAL_NO_EVENTS` workers.

## Counting limits and fallback behavior

- PCNT counts in hardware while C# is busy. Its 16-bit limit events must still be
  serviced before another 32,767-transition limit interval can be lost. IDF ≥5.1
  uses the SDK's accumulator; older SDKs extend at limit IRQs, not GPIO edges.
- A 16-bit TIM must service overflow/underflow within half a counter lap (32,768
  transitions). The read path accounts for a pending update flag without double
  counting. This cannot reconstruct multiple wraps while interrupts are masked.
- PIO maintains a modulo-32-bit count and uses a nonblocking snapshot FIFO: a full
  FIFO does not stall counting. Sampling is set for up to roughly 100k transitions/s;
  pulses below the sampling interval can be missed. Supported native pairs are
  consecutive pins in GPIO 0–31 with PIO GPIO base 0.
- Read native `Position` / call `Reset` at least once per 2^31 raw transitions so
  signed modular deltas remain unambiguous. This is about 536 million detents at
  four transitions/count. Low-power modes that gate the peripheral clock can stop
  counting; normal `Thread.Sleep` does not reconfigure that clock.
- GPIO fallback samples both pins in the producer ISR, before event enqueue.
  Valid Gray transitions cancel back-and-forth chatter; illegal two-bit jumps
  resynchronize. It needs IRQ-capable pins (distinct STM32 EXTI lines), ISR-safe
  direct GPIO reads, and timely servicing on each edge. It is not high-speed
  hardware counting. PCNT/TIM filters follow their peripheral behavior instead
  of claiming software Gray-table invalid-transition detection.

The template is intended for human-operated controls. This is not a blanket
lossless motor-feedback guarantee during flash/cache stalls or long masked IRQs.
ESP32 native PCNT setup/filtering and STM32 encoder input filtering also differ
from application push-button debounce.

## Build and custom HAL backends

`MCS_ENABLE_ENCODER` defaults to `MCS_ENABLE_HAL`. CMake:
`-DMICROCS_ENCODER=OFF`; ESP-IDF menuconfig: **MicroCS → Rotary encoders**.
`MCS_ENABLE_ENCODER_HW` defaults on with the encoder feature. Setting it to 0
explicitly forces GPIO-only firmware. The Cortex-M0 reference emulator has no
physical counter and builds GPIO fallback only, keeping its fixed flash limit.

HAL API v6 appends native `encoder_open/read/close` and an optional `encoder_name`.
`open(ctx, a, b, &handle)` owns native A/B electrical/AF setup. Return `ENOTSUP`
only when unavailable for the pair, with `handle=NULL` and no retained resources.
Other failures propagate. `read` returns a free-running modulo-32-bit **x4** raw
transition total without clearing it. `close` stops and releases all owned native
resources. Native hooks may be supplied by a custom board, including Zephyr QDEC
adapters; none are falsely synthesized from chip capabilities.

For fallback, set `gpio_capture=true` only with ISR-safe direct GPIO reads and
invoke this **in the GPIO ISR**, before a shared or private SDK event queue:

```c
if (!mcs_hal_gpio_edge(hal->ctx, pin))
    mcs_hal_post(MCS_HAL_EV_GPIO, pin, level);
```

The exact HAL context identifies the board. Bare-metal fallback IRQs must run on
the VM core or share an IRQ-safe multicore lock. Board tables must outlive VMs.
