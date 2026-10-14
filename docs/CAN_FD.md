# CAN FD common HAL and software loopback

This interface adds a separate CAN FD data path. Existing `mcs_can_frame_t`
(`data[8]`), classic CAN callbacks, `CAN` and `CanFrame` are unchanged.

**Implemented backend: software loopback on simulator bus 0 only.** It copies
actual frame bytes through a bounded FIFO; it is not a physical CAN controller,
wire simulator, timing model, arbitration/error-confinement model or hardware
validation. No existing ESP32/TWAI, STM32/FDCAN, RP2, Arduino or Zephyr backend
acquires CAN FD support merely because this common interface exists. In
particular an STM32 peripheral named FDCAN may still be configured for classic
CAN. No physical traffic, device setup or provisioning is performed here.

## Payload and flag contract

CAN FD has payloads up to 64 bytes and independent nominal/data bitrates. Bosch
identifies the protocol as standardized by ISO 11898-1:2015. Linux documents
separate CAN and CAN FD frame structures and distinguishes byte lengths from
DLC. [^https://www.bosch-semiconductors.com/products/ip-modules/can-protocols/can-fd]
[^https://docs.kernel.org/networking/can.html]

Canonical DLC mapping used by this interface:

| DLC | 0–8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Wire bytes | same as DLC | 12 | 16 | 20 | 24 | 32 | 48 | 64 |

The Bosch receive-message documentation supplies the extended DLC/payload map.
[^https://www.bosch-semiconductors.com/media/ip_modules/pdf_2/x_can/xs_can_an001_rx_handling_fmm_v1-0.pdf]

`CanFdFrame` accepts application arrays/Lists containing **0–64 integer bytes**.
Noncanonical lengths are **zero-padded to the next canonical wire length before
native transmission**. Example: nine input bytes produce `Length == 12`,
`Dlc == 9` and three trailing zero bytes. `Data` and `Length` always expose the
canonical padded wire payload, including on loopback/receive; no hidden original
application length is reconstructed. Store that length in your application
protocol if needed. Input arrays are copied, never modified or retained; each
`Data` getter returns a new independent array. Values outside 0–255, floats,
strings and non-array/List objects are rejected, not truncated or string-encoded.
`null` means an empty payload.

Standard identifiers are 0–0x7FF; extended identifiers are 0–0x1FFFFFFF.
Identifier and integer arguments are range-checked before narrowing, including
INT64 builds. `extended` and `bitRateSwitch` must be actual booleans.

`BitRateSwitch` (BRS) indicates use of the configured data bitrate.
`ErrorStateIndicator` (ESI) is receive-only: a real controller derives it from
its transmitter's error state, not an arbitrary script setter. Linux's UAPI
explicitly describes this distinction. The loopback backend is error-active and
receives ESI false; a genuine backend may return ESI true. There is **no RTR or
error-frame mode**; `Remote` is always false. Unknown flags and noncanonical or
out-of-range backend metadata are rejected, never clamped to eight bytes.
[^https://raw.githubusercontent.com/torvalds/linux/v6.12/include/uapi/linux/can.h]
Remote frames are not part of CAN FD; NXP documents the RRS replacement for RTR.
[^https://community.nxp.com/t5/NXP-Tech-Blog/101-Controller-Area-Network-with-Flexible-Data-Rate-CAN-FD/ba-p/1270417/page/2]

## Script API

The `CANFD` introspection/error-reporting module and pure `CanFdFrame` value class
are available when the HAL library is attached, even on a classic-only HAL.
Their existence does not imply supported FD operations.

| API | Behavior |
| --- | --- |
| `CANFD.IsSupported(bus)` | Side-effect-free per-bus support query; false for an incomplete/incapable backend |
| `CANFD.SupportsBitRateSwitch(bus)` | Per-bus BRS query, requiring FD support too |
| `CANFD.Open(bus[, nominalBitrate = 500000, dataBitrate = 2000000])` | Exclusively acquires one bus for this VM |
| `CANFD.Send(bus, frame[, timeoutMs = 100])` | Synchronously copies a `CanFdFrame`; BRS requires backend support |
| `CANFD.Receive(bus[, timeoutMs = 0])` | Returns a validated `CanFdFrame`, or null on no frame/timeout |
| `CANFD.OnReceive(bus, callback)` | Rooted callback with one pending-count integer; null unregisters |
| `CANFD.Close(bus)` | Releases only this VM's acquired bus; idempotent if this VM owns nothing |
| `new CanFdFrame(id, data[, extended = false, bitRateSwitch = false])` | Constructs an immutable copied/padded frame |
| Frame properties | `Id`, `Extended`, `Remote`, `Length`, `Dlc`, `Data`, `BitRateSwitch`, `ErrorStateIndicator`, `ToString()` |

VM-facing bus indices are 0 through `MCS_HAL_MAX_BUSES - 1` (default 4).
Nominal/data rates are positive integer bit/s, at most UINT32_MAX on builds that
can represent it, with data >= nominal. If a backend cannot switch bitrates,
data > nominal and BRS transmission fail with `NotSupportedException`.
The separate data timing and data >= nominal policy follows Linux's documented
CAN FD configuration contract; a real backend must additionally reject rates,
bit timings or options its controller cannot honor.
[^https://docs.kernel.org/networking/can.html]

Timeouts are nonnegative integer milliseconds within UINT32_MAX. The simulator
returns immediately: full FIFO produces `IOException`/EBUSY, empty FIFO yields
null/ETIMEOUT, irrespective of requested timeout. It does not advance a clock
or pretend to implement timed hardware waiting.

`Hal.Has("CAN.FD")` means a **complete linked set of FD backend callbacks** is
installed. It does not probe/configure any bus or prove hardware attachment.
Always also use `CANFD.IsSupported(bus)`. Incapable/incomplete backends fail
operations explicitly with `NotSupportedException`; no classic fallback or
silent truncation occurs. Invalid argument/configuration fails before a native
operation. Operations on a bus not opened by this VM fail rather than borrowing
an embedding's or another VM's bus. A failed/busy Open acquires nothing.

## Ownership, callbacks and teardown

A successful Open belongs to the attached VM's private HAL state until explicit
Close, HAL detach/re-attachment or VM destruction. Duplicate Open from the same
VM fails and does not replace its owner or callback; another VM's Open must
receive backend EBUSY. Embedding-owned opens are never acquired or closed by a
VM after a failed Open. Backend acquisition must therefore be exclusive.

**End of a source run or Stop does not automatically close an FD bus.** This
matches persistent-session/embedding HAL behavior. A caught error or aborted run
can be followed by valid operations in the same session; applications should use
`try/finally` and Close on normal paths. Detach/free releases outstanding owned
buses after error/Stop as well. Backend close must be safe without executing VM
code; teardown is best-effort if a backend close itself fails. Explicit Close
failure leaves ownership/callbacks intact for retry. An embedding must handle
hardware-specific recovery if best-effort teardown cannot release a controller.

The attached HAL table and its context must remain valid and unchanged until
that VM detaches or is freed. No old-size binary HAL table compatibility is
promised: source initializers are append-compatible, but `mcs_hal_t` grows and
has no runtime size field. Rebuild embedding code/ports against the new header.
`MCS_HAL_API_VERSION` remains 3 because existing v3 APIs/behavior are retained;
feature detection is through the appended optional callbacks, not that number.

FD callbacks use a distinct FD event type; classic CAN callbacks/queues are not
reused. Notifications are advisory pending-count snapshots; drain Receive until
null instead of assuming the count is still current. Close unregisters its FD
callback and discards owned-bus global/deferred FD notifications. Generations
prevent already-held deferred snapshots replaying after close/reopen. Other-bus,
classic and user notifications preserve their order. Native backends must also
clear their own FD notifications on close. Callback roots survive GC and a
callback may safely close its own bus. The existing HAL event ring/threading
contract still applies; this does not establish a new multi-producer transport.

## Native interface and simulator

`mcs_can_fd_frame_t` has separate identifier, extended flag, BRS/ESI flags,
canonical byte length, zero reserved byte and `data[64]`. Header helpers map
length/DLC and validate frame metadata; invalid mapping arguments return 255.
Transmit validation rejects ESI. The five appended callbacks are:
`can_fd_caps`, `can_fd_open`, `can_fd_send`, `can_fd_recv`, `can_fd_close`.
`can_fd_caps` returns `MCS_CAN_FD_SUPPORTED`, optionally
`MCS_CAN_FD_SUPPORTS_BRS`, or a negative error. Queries are side-effect-free.
Frames passed to send/receive are synchronous caller-owned buffers; callbacks
must copy bytes and never retain stack pointers. Unsupported bus/configuration
returns ENOTSUP; failed Open must acquire no resource. There is no automatic
adapter from classic callbacks.

Simulator FD bus 0 owns a separate eight-frame FIFO by default. Classic FIFO
capacity/layout are unchanged. Set `MCS_HAL_SIM_CAN_FD_QUEUE=0` at compile time
to omit FD simulator queue storage and callbacks; FD capability becomes false
and operations explicitly unsupported. Positive capacity can be 1–255. Opening
an occupied FD bus fails EBUSY; close clears FD frames and rates, leaving classic
CAN state intact. This does not model bus collisions, physical delays or errors.

## Tests and support boundary

Run dedicated ordinary-source/native/trusted compiler-image tests:

```sh
python3 tests/can_fd/run.py
python3 tests/can_fd/run.py --sanitize
```

Tests cover DLC/padding/64-byte copying, strict arguments, metadata returned by
ordinary test backends, capability/unsupported behavior, queue/order/timeouts,
exclusive ownership, roots/GC, callbacks/Stop/cleanup/repeated use, and source,
optimized/noopt copied and XIP images (superinstructions-disabled builds use
the documented `MCS_IMAGE_NO_OPT` fixture). Images are emitted by the trusted compiler;
no handcrafted or mutated image is executed. Variant flags/provenance and raw
logs are retained in the selected build directory. Profile tests use explicitly
recorded host semantic-test heap/stack budgets, not minimum-board-RAM or hardware
support claims. Physical controller/transceiver/target tests remain necessary
before any backend advertises physical CAN FD support.

## First host footprint observation (not an MCU measurement)

With this checkout's x86-64 GCC ABI, `sizeof(mcs_hal_t)` grows from 544 to 584
bytes (five appended pointers); the classic frame remains 16 bytes with data at
offset 7. The FD frame is 72 bytes. Default simulator storage grows from 7,408
to 8,000 bytes for its separate FIFO and bookkeeping. With
`MCS_HAL_SIM_CAN_FD_QUEUE=0` the simulator remains 7,408 bytes. These are host
structure sizes, not heap budgets or MCU flash/RAM measurements. Private VM HAL
state also acquires per-bus ownership/generation bookkeeping; no RAM reduction
or universal performance gain is claimed.

The loopback API assumes serialized calls, as do its existing classic FIFO and
other simulator peripherals. Embeddings sharing a simulator HAL between OS
threads must serialize operations; this does not add a multi-thread-safe device
backend. Reinitializing or changing an attached HAL/context invalidates the
lifetime contract. A hardware port must provide real exclusive acquisition,
controller configuration, close/recovery and interrupt-queue cleanup before
advertising FD support.

Same-flag GCC `-Os` host object measurements (text section, bytes; not linked MCU
firmware):

| Profile / object | Before | CAN FD |
| --- | --- | --- |
| default HAL library | 55,329 | 61,635 |
| default simulator | 10,800 | 11,493 |
| lowram HAL library | 55,343 | 61,617 |
| lowram simulator | 10,800 | 11,493 |
| min HAL library (HAL disabled by profile) | 2,418 | 2,433 |
| min simulator (HAL disabled) | 48 | 48 |
| queue-off simulator | 10,800 | 10,800 |

Queue-off removes simulator FD storage/code, **not** the common FD bindings when
HAL itself is enabled. A dedicated min-plus-HAL variant tests explicit opt-in;
the ordinary min profile's disabled-HAL capability check must not be confused
with testing FD execution on a minimum-sized MCU. Exact flags, compiler hashes
and source provenance belong with the raw test evidence.
