# CAN bus

MicroCS exposes **classic CAN 2.0** (11-bit / 29-bit IDs, 0–8 data bytes, data and
remote-request frames) through `CAN` and `CanFrame`. FDCAN / TWAI-FD peripherals
are used in **classic mode**; CAN FD payloads and bit-rate switching are not
implemented. Do not connect a classic-only controller to a bus carrying FD frames.

## Availability

| Port / MCU | Native backend | External controller |
|---|---|---|
| ESP-IDF / Arduino-ESP32 | TWAI on chips declaring `SOC_TWAI_SUPPORTED`; legacy driver bus 0; FD-capable chips such as C5 use IDF 5.5+ node API, with logical buses up to the hardware controller count | MCP2515 over SPI |
| STM32 | bxCAN or FDCAN in classic mode, when enabled in the HAL and supplied in `board.can[]` / `board.fdcan[]` | MCP2515 over SPI, including STM32 parts without CAN |
| RP2040 / RP2350 (Pico, Pico 2, W variants, RP2040 Zero…) | **No on-chip CAN controller** | MCP2515 over SPI |
| Zephyr | `CONFIG_CAN=y`, enabled device at alias `mcs-can0` / `mcs-can1` (bus 0 falls back to chosen `zephyr,canbus`) | MCP2515 over a MicroCS SPI/GPIO bus, or Zephyr's CAN driver with a configured external device |
| Other Arduino cores | No generic native CAN wrapper; even if the MCU has a CAN peripheral, its core-specific native library is not wrapped | MCP2515 over SPI |
| Simulator (`mcs --sim`) | CAN loopback, bus 0 (no electrical bus) | Transport tests use a separate MCP2515 register model, not the simulator's ordinary SPI loopback |
| Custom / template ports | Supply `can_open` or `can_configure`, `can_send`, `can_recv`, and preferably `can_close` | MCP2515 when the HAL supplies SPI, GPIO and microsecond delay |

Capabilities are side-effect-free:

- `Hal.Has("CAN")`: a native send/receive/open backend is compiled in. It does
  **not** guarantee that a particular bus has been wired or a transceiver attached.
- `Hal.Has("CAN.Configure")`: native `CanConfig` settings can be requested.
- `Hal.Has("CAN.Mcp2515")`: the firmware supplies SPI open/transfer, GPIO mode/write
  and microsecond delay. This does **not** probe for an attached MCP2515. Studio
  shows the external-controller template using this capability, even without native CAN.

The CAN class is registered if either native CAN or the MCP2515 transport is
available. `CAN.Open` never silently switches to an external controller.

## Wiring first

A native controller needs a **CAN transceiver** (for example a 3.3 V-compatible
SN65HVD230) between MCU TX/RX and CANH/CANL. A transceiver alone does not add CAN
to RP2040/RP2350: those need a controller **and** a transceiver, such as an MCP2515
module with suitable transceiver and logic levels. Do not connect a 5 V module's
SPI output directly to 3.3 V GPIO unless its level shifting is appropriate.

Use a common ground, twisted CANH/CANL pair, and 120 Ω termination at **each end**
of the bus (not at every node). Both nodes must use the same nominal bitrate.
Normal transmissions need another active node to acknowledge; listen-only nodes
do not acknowledge. Disconnect equipment before changing wiring. Test on an
isolated bench bus, not a vehicle's live control bus.

## Native CAN

```csharp
// ESP32 example wiring only: choose free, valid pins for your board.
CAN.Open(0, new CanConfig { Bitrate = 500000, Tx = 4, Rx = 5 });
try
{
    CAN.Send(0, 0x123, new byte[] { 1, 2, 3 });
    var f = CAN.Receive(0, 100);  // null after 100 ms with no frame
    if (f != null) Console.WriteLine(f);
}
finally { CAN.Close(0); }
```

`CAN.Open(bus)` defaults to 500000 bit/s. `CAN.Open(bus, bitrate)` uses board wiring
and normal mode. `CanConfig` has `Bitrate` (1–1000000), `Tx` / `Rx` (default -1:
board wiring), and `Mode` (`CAN.Normal`, `CAN.ListenOnly`, `CAN.Loopback`). Pin names
work as in `SpiConfig`. Unavailable settings fail explicitly, not silently.

- **ESP32:** pins can come from `CanConfig`, `pins.can` (ESP-IDF), or
  `mcs_arduino_can_pins(tx, rx)` (Arduino). Legacy TWAI accepts 25k, 50k, 100k,
  125k, 250k, 500k, 800k and 1M bit/s. FD-capable TWAI nodes use SDK bit-timing
  calculation. Chips without TWAI, including C2, must use an external controller.
  Legacy loopback uses no-ACK + self reception and still requires RX to see TX
  (through a transceiver or a bench TX-to-RX loop); it is not an internal wire.
- **STM32:** ready-to-flash Nucleo F446/L476/G474 firmware wires bus 0 to PA11 RX /
  PA12 TX; H743 uses PD0 RX / PD1 TX. F401/F411 have no native controller. All
  STM32 firmware board definitions provide SPI bus 0 (SPI2: PB13 SCK / PB14 MISO / PB15 MOSI)
  for an external MCP2515. Choose a free CS pin in the script. For custom firmware,
  CubeMX owns pins and AF configuration, so leave `Tx` / `Rx` at -1.
  Enable CAN/FDCAN and clocks, initialize the handle, and supply it in the board
  table. The requested bitrate is calculated from the actual kernel clock rather
  than ignored. `board.can_clock_hz[bus]` can override the RCC source clock in Hz;
  otherwise bxCAN uses APB1, FDCAN queries `RCC_PERIPHCLK_FDCAN` where available.
  The peripheral divider (`ClockDivider` on newer HALs, shared CCU divider on H7)
  is applied to that source clock.
  An unavailable clock or inexact bitrate fails. Supply nonzero RX FIFO0 / TX FIFO
  capacity in the CubeMX FDCAN initialization on HAL families with configurable
  message RAM. All IDs and remote frames are accepted into FIFO0. Changing shared
  FDCAN kernel clocks/dividers remains the firmware application's responsibility.
- **Zephyr:** devicetree/pinctrl owns pins, so leave `Tx` / `Rx` at -1. Mode support
  depends on the device. Reopen releases filters and clears the receive queue;
  partial filter allocation is rolled back. Software CAN loopback devices use
  loopback mode automatically. `CONFIG_CAN_ACCEPT_RTR=y` is implied by the
  MicroCS port; do not disable it if receiving remote-request frames is needed.

For an isolated controller test, request `CAN.Loopback`. Use `CAN.ListenOnly` to
observe traffic without transmitting or acknowledging. Sending in listen-only
mode throws `NotSupportedException`.

## MCP2515: CAN on MCUs without a controller

No extra library or build flag is needed when the HAL module is enabled. Configure
the SPI **pins** first with `SpiConfig` or the port's board configuration. The CAN
backend selects 1 MHz, mode 0, MSB-first, 8-bit SPI for each operation; the bus can
be shared with other devices with separate chip selects. Do not rewire an active
SPI bus or let external native tasks use it concurrently.

```csharp
// Pico / RP2040 Zero example wiring. Adapt pins for your board/module.
SPI.Open(0, new SpiConfig { Sck = 2, Mosi = 3, Miso = 4 });
// logical CAN bus, SPI bus, CS pin, controller crystal Hz, CAN bitrate, mode
CAN.OpenMcp2515(0, 0, 5, 16000000, 500000, CAN.Normal);
try
{
    CAN.Send(0, new CanFrame(0x18FF50E5, new byte[] { 1, 2 }, true));
    var frame = CAN.Receive(0, 100);
    if (frame != null) Console.WriteLine(frame);
}
finally { CAN.Close(0); }
```

`CAN.OpenMcp2515(bus, spiBus, csPin[, oscillatorHz = 16000000[, bitrate = 500000[, mode = CAN.Normal]]])`
accepts 8, 16 and 20 MHz crystals. **Read the module's crystal marking**: using the
wrong value produces the wrong bus bitrate. Only exactly representable bitrates
are accepted; the driver chooses nominal timing near an 87.5% sample point (80%
above 500 kbit/s). At 8 MHz, 1 Mbit/s is not supported by this timing policy.
There are three hardware TX buffers and two RX buffers with rollover. Polling
needs no INT wire, but high receive rates can overflow those small hardware buffers:
call `Receive` frequently; do not expect lossless capture on a saturated bus.

The same SPI/GPIO backend works on RP2, STM32, ESP32, Arduino and Zephyr. It does
not add native controller hardware, a transceiver, or CAN FD support.

## Frames, timeouts and callbacks

```csharp
var standard = new CanFrame(0x123, new byte[] { 0xAA });
var extended = new CanFrame(0x18FF50E5, new byte[8], true);
// RTR requesting 4 bytes; data content is ignored, Length preserves the DLC.
var request = new CanFrame(0x123, new byte[4], false, true);
CAN.Send(0, request, 50);                       // up to 50 ms to enqueue
CAN.Send(0, 0x123, new byte[] { 1 }, false, 50);
```

Frame properties: `Id`, `Extended`, `Remote`, `Length`, `Data`. A received RTR has
an empty `Data` array, while `Length` is the requested DLC. Data is copied; mutating
the original array cannot change an already-created frame. IDs outside 11/29 bits
and payloads over eight bytes are rejected (including on 64-bit hosts).

`Send(bus, frame[, timeoutMs = 100])` and
`Send(bus, id, data[, extended = false[, timeoutMs = 100]])` wait for **queue space**,
not wire delivery / acknowledgment. `Receive(bus[, timeoutMs = 0])` returns a frame
or null if empty/expired. Negative timeouts are rejected; zero means nonblocking.
Waits service VM safepoints, cancellation and other callbacks instead of blocking
the Studio connection for a long SDK timeout. CAN operations belong to the main
VM, not worker VMs. Native applications must serialize access to controllers.

```csharp
CAN.OnReceive(0, (int pending) =>
{
    // pending is a lower bound (one-frame lookahead), not the SDK FIFO depth.
    CanFrame f;
    while ((f = CAN.Receive(0)) != null) Console.WriteLine(f);
});
Hal.Run(1000);                         // or other code that services HAL polling
CAN.OnReceive(0, null);                // unsubscribe
```

Callbacks are consistent across all backends and run on the VM thread, never in
an ISR. The library holds at most one prefetched frame per logical bus; it does
not grow the VM heap with traffic. A callback must drain available frames (with
nonblocking `Receive`) to get subsequent notifications. Do not mix an independent
blocking receive loop and a draining callback on the same bus.

`CAN.Close(bus)` is idempotent, clears lookahead, unregisters the script callback,
and stops/releases the controller (MCP2515 enters sleep and aborts queued TX).
VM teardown also closes buses it opened. Close all CAN buses before requesting
hardware light/deep sleep or shutdown (active CAN makes these modes fail as busy). Reopening clears stale data. Errors,
including bus-off, are surfaced as exceptions; correct the wiring/bitrate and
close/reopen to reset the controller. There is no guaranteed delivery or automatic
application-level retransmission. Native SDK receive queues and hardware FIFOs
are finite and may drop frames under load.

## Firmware integration and validation

HAL API v5 appends `can_configure(ctx, bus, cfg)` and `can_close(ctx, bus)` without
reordering older callbacks. Old custom native ports can keep `can_open`; advanced
settings are rejected without `can_configure`. Implement `can_close` to support
cleanup/reopen; otherwise explicit close/reopen returns `NotSupportedException`.
For nonblocking calls use `MCS_HAL_ETIMEOUT` for empty/full and never enqueue a
frame before returning timeout: the library may retry. A successful send means
accepted by the driver, not necessarily acknowledged on the wire. `can_recv` must
reject FD frames, not silently truncate them. HAL CAN ISR event hints are no
longer needed; subscribed receive polling is handled by the library.

`make can-test` tests the API, nominal timing and an MCP2515 SPI/register model.
SDK integration builds exercise native port compilation; none substitutes for
real hardware. Validate two physical nodes with transceivers, matching rates,
standard/extended/RTR traffic, receive callbacks, disconnect/reconnect and bus-off
recovery before deployment. Physical CAN bus tests have not been performed here.
