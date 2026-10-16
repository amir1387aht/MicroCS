# Sleep, wake pins and shutdown

`Power` is registered with the HAL (`MCS_ENABLE_HAL`). It separates a cooperative VM
pause from a hardware-wide power transition. Use `Hal.Has(...)` on the **connected
firmware**, not the chip name, to decide what is available. Unsupported hardware modes
throw `NotSupportedException`; they never silently become an ordinary delay.

## API

| Member | Meaning |
|---|---|
| `Power.Sleep(milliseconds)` | Cooperative pause, like `Thread.Sleep`: timers, VM idle hooks and cancellation can run. Does **not** promise a hardware low-power state. Nonnegative signed 32-bit milliseconds. |
| `Power.SetWakePin(pin, high)` | Select one wake input and its active **level** (`true` high, `false` low); number or port pin name. Validates without changing hardware. Replaces the previous selection only on success. |
| `Power.ClearWakePin()` | Clear the VM's selection; does not reconfigure GPIO or native SDK wake sources immediately. |
| `Power.WakePin`, `Power.WakeHigh` | Selected numeric input (`-1` if none) and active level. Per VM; reset when the HAL is reopened / firmware restarts. |
| `Power.LightSleep(milliseconds = 0)` | Enter the port's light/CPU-idle mode; resumes after a timeout or active selected pin. `0` means pin-only, **not** an immediate return. |
| `Power.DeepSleep(milliseconds = 0)` | Enter reset-on-wake deep sleep / standby. RAM, VM state, scheduler and normal variables are lost. Physical hardware does not resume the next line. |
| `Power.Shutdown()` | Enter the port's actual shutdown/poweroff state, not a deep-sleep alias. Recovery requires the board's supported wake/reset/power cycle. No timer argument. |

`LightSleep` and `DeepSleep` require a positive timeout or a selected wake pin. Negative
or greater-than-2147483647 millisecond values are rejected. When both sources are
supported and selected, either may wake the board. `Shutdown` can be reset-only when
the backend supports that; STM32's adapter instead requires its supported wake pin.
These calls return `void` and do not report a wake reason.

### Capability names

- `Power`, `Power.Sleep`: cooperative API present.
- `Power.LightSleep`, `Power.DeepSleep`, `Power.Shutdown`: implemented hardware modes.
- Append `.Timer` to light/deep sleep, or `.Pin` to any mode, for supported wake sources, for example
  `Power.DeepSleep.Timer` and `Power.LightSleep.Pin`.

A `.Pin` capability means **some** inputs work, not every GPIO. `SetWakePin` checks
whether a pin is usable for at least one mode; the mode call validates it again. An
ESP32 digital-only pin can be valid for light sleep but rejected for deep sleep.

## Port support

| Port | Light sleep | Deep sleep | Shutdown |
|---|---|---|---|
| ESP-IDF, ESP32 family | SDK light sleep, timer and digital GPIO level wake, subject to `SOC_LIGHT_SLEEP_SUPPORTED` | SDK deep sleep, timer; GPIO only where ext0/ext1 or deep-GPIO wake exists | Not exposed: deep sleep is not true shutdown |
| Arduino-ESP32 core 3.x (IDF 5) | Same shared SDK adapter and SoC guards | Same | Unsupported |
| pico-sdk, RP2040 / RP2350 (including W / Zero boards) | SDK `sleep_ms` / WFE CPU idle; timer and GPIO **sampled every ~1 ms** | Not exposed | Not exposed |
| STM32 with Cube `HAL_PWR_MODULE_ENABLED` and SLEEP support | CPU SLEEP/WFI, live HAL tick; GPIO **sampled on interrupts/ticks** | F4/L4/G4 only: standby, **PA0 / WKUP1, high only**, no timer wrapper | L4 only: hardware shutdown, same PA0/high requirement |
| Zephyr | Not exposed; the kernel manages idle power policy | Not exposed | `sys_poweroff()` only with `CONFIG_POWEROFF`; wake/reset is board-specific, no portable pin/timer wrapper |
| Other Arduino, template, custom HAL, Cortex-M demo | Only if the application fills the v4 callbacks/masks | Same | Same |
| Host simulator (`--sim`) | Records request and advances virtual board time | Records request and **returns**, no reset | Records request and **returns**, no poweroff |

All ports with the HAL get cooperative `Power.Sleep`, even if every hardware mode is
unsupported. Older Arduino-ESP32 cores without the required SoC capability macros
may expose only cooperative sleep; the SDK 5 / core 3.x adapter is the supported target. RP2 light sleep is **not dormant mode**, does not stop clocks, and does not
promise that the other core or radio sleeps. STM32 light sleep is **not STOP mode**:
clocks and the tick remain available. Short GPIO pulses can be missed on these sampled
backends; hold the input at the wake level. Deep sleep and shutdown are deliberately
unavailable on ports that lack a safe, tested clock/USB/wake restoration wrapper, even
if the silicon itself has additional low-power states.

STM32 applications must provide a working interrupt-driven HAL tick; do not suspend
SysTick / your HAL timebase before calling `LightSleep`. Standby/shutdown require a
bonded PA0/WKUP1 and the board's electrical wake conditions. Other native RTC, reset
or wake sources configured by the application may also wake the device.

## Before entering hardware sleep

1. Save/close files, finish transfers, stop periodic jobs and quiesce attached devices.
   GPIO outputs, LEDs, regulators and USB circuitry can dominate current consumption.
2. Stop radios explicitly. ESP32 rejects entry while its configured Wi-Fi mode or BT
   controller is active: call `Wifi.Off()` (also stops ESP-NOW) and `Ble.Reset()` when
   those modules are in use. Arduino users must also stop native radio stacks
   (`WiFi.mode(WIFI_OFF)`). Hardware sleep does **not** reconnect radios afterwards.
3. Configure the selected GPIO as a dedicated input with a safe pull. The active
   level is the physical/raw input level, not `GpioConfig.Invert`'s software inversion.
   Call `GPIO.Mode` first to reset any pin-mux overrides. Do not share the wake pin
   with an IRQ handler: MicroCS rejects its own `GPIO.OnChange` registrations there;
   native handlers must be removed by the application (ESP light wake changes the pad's
   interrupt type). Use external resistors
   for deep sleep where the chip's powered-down domains cannot retain internal pulls.
   Never assume the LED, BOOT button, flash, USB or radio pin is a safe wake input.
4. Use a timeout for initial light-sleep tests. Hardware entry is synchronous: no C#
   callbacks, Studio Stop, shell commands or uploads are serviced **until it wakes**.
   Pin-only sleep can therefore make Studio appear stuck. This differs from `Power.Sleep`.
5. Expect ESP light sleep to disrupt USB/serial and deep sleep/shutdown to disconnect it;
   reconnect Studio after wake. A watchdog or native firmware task may still reset the
   board. These APIs do not disable watchdogs or suspend arbitrary application tasks.

Hardware-wide modes reject worker VMs and in-flight MicroCS DMA. Builds with real
threads conservatively reject entry once MicroCS worker threads have been started
(the thread subsystem's shared-resource flag remains set); reboot before a sleep-only
session. Cooperative `Power.Sleep` remains available to workers. Native application
threads/peripherals are the embedding application's responsibility.

ESP entry **owns/replaces the SDK wake configuration**: it disables existing wake
sources, arms the requested ones, and clears its sources after light wake or an error.
Do not mix it with independently managed ULP/touch/UART wake configurations. An already
active wake level can cause immediate wake or an SDK sleep-rejected error. SDK errors
propagate as hardware exceptions; a rejected pin/mode does not start sleeping.

Do not put an unconditional deep-sleep/shutdown call in `/boot.cs`, `/main.cs` or a
repeating startup job: wake runs the firmware boot sequence again and can create a
sleep loop. Test from a manually run script first; keep the board's recovery procedure
(BOOTSEL / bootloader / reset and filesystem recovery) available.

## Examples

A bounded, portable pause:

```csharp
if (!Hal.Has("Power.Sleep")) return;
Console.WriteLine("Pause without blocking Studio cancellation");
Power.Sleep(250);
```

Timer-only hardware light sleep (stop radios first if they were started):

```csharp
if (!Hal.Has("Power.LightSleep.Timer")) return;
Power.ClearWakePin();
Power.LightSleep(1000);
Console.WriteLine("Resumed");
```

Select a **board-specific** pin, with a timeout safety net. Replace `-1` only after
checking the board's pinout; on sampled backends hold the input at the active level.

```csharp
const int WAKE_PIN = -1; // choose a safe, supported input
if (WAKE_PIN < 0 || !Hal.Has("Power.LightSleep.Pin") || !Hal.Has("Power.LightSleep.Timer")) return;
GPIO.Mode(WAKE_PIN, GPIO.InputPullUp);
Power.SetWakePin(WAKE_PIN, false);
Power.LightSleep(5000);
Power.ClearWakePin();
```

Studio has six guarded templates in **Power management**, including timer and pin deep
sleep and shutdown. Destructive/resetting templates require explicit opt-in before entry;
mode and wake-source requirements hide them on incompatible firmware.

## Implementing a port (HAL API v4)

The append-only `mcs_hal_t` fields are `power_modes`, `power_timer_modes`,
`power_pin_modes`, `power_validate`, `power_enter`. Each mask uses
`MCS_POWER_LIGHT`, `MCS_POWER_DEEP`, `MCS_POWER_SHUTDOWN`. Zero masks/NULL callbacks
preserve old ports. Recompile applications with the new header; this is source-level,
not binary struct-layout compatibility.

`power_validate(ctx, mode, ms, pin, high)` must have no hardware side effects. Mode `0`
checks a prospective pin against at least one supported mode; `pin == -1` means none.
For real entry validate the specific mode, duration, pin and polarity. Return
`MCS_HAL_ENOTSUP` for an unimplemented mode/source, `MCS_HAL_EINVAL` for invalid
arguments and `MCS_HAL_EBUSY` for unsafe entry. `power_enter` must recheck native
application/radio state and implement the requested mode, not a delay fallback.
Return nonnegative after light sleep, negative on failure; physical deep/shutdown
normally never return. The simulator is explicitly the exception. Do not advertise
wake-source bits your backend cannot honor. Timer wake bits describe source support,
not a promise about elapsed wall-clock accuracy or retained peripherals.

Vendor references: [ESP-IDF sleep modes](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/sleep_modes.html),
[pico-sdk time](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#group_pico_time),
[STM32Cube HAL](https://github.com/STMicroelectronics/STM32CubeL4),
[Zephyr poweroff](https://docs.zephyrproject.org/latest/services/pm/api/index.html).
