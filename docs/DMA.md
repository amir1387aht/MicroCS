# DMA and asynchronous transfers (HAL API v3)

`DmaBuffer` is a block of DMA-capable memory. The `…Async` calls start a
transfer and return a `DmaTransfer` at once; the script keeps running while the
DMA engine moves the bytes, then waits for the result or gets a callback.

```csharp
var frame = new DmaBuffer(128 * 64 / 8);         // an OLED frame buffer
DrawInto(frame);
var t = SPI.WriteAsync(1, frame, 5);              // CS on GPIO5, returns immediately
ReadSensors();                                    // runs while the frame goes out
t.Wait();                                         // or t.OnComplete(n => ...)
```

The same script runs on **every board**: where the board has no DMA for an
operation, the call runs synchronously and the returned transfer is already
complete. `Hal.Has("DMA.SPI")` (`DMA`, `DMA.Memory`, `DMA.I2C`, `DMA.UART`,
`DMA.QSPI`) tells whether it really runs in the background.

A walk-through is in the [DMA guide](guides/DMA.md); bus settings
(`SpiConfig.Dma`, `MaxTransfer` …) are in [peripheral configuration](PERIPHERALS.md).

**Contents:** [DmaBuffer](#dmabuffer) · [Starting transfers](#starting-transfers) ·
[DmaTransfer](#dmatransfer) · [Dma](#dma) · [Rules](#rules) ·
[Boards](#boards) · [Ports: implementing DMA](#ports-implementing-dma)

## DmaBuffer

| Member | Description |
|---|---|
| `new DmaBuffer(size)` · `new DmaBuffer(byte[] / string / DmaBuffer)` | zero-filled, or a copy; up to 16 MiB |
| `buf[i]` (get / set) · `Length` | bytes |
| `Fill(value[, offset[, count]])` | |
| `CopyFrom(byte[] / string / DmaBuffer[, offset])` → bytes copied | |
| `ToArray([offset[, count]])` → `byte[]` · `GetString([offset[, count]])` | UTF-8 |
| `Busy` | a transfer is using the buffer |

The memory comes from the board's DMA allocator (ESP32: internal DMA-capable
RAM; elsewhere the VM heap, aligned to `Dma.Alignment`) and is freed with the
object. While a transfer uses a buffer, writing to it throws
`InvalidOperationException`; reading is allowed.

## Starting transfers

Data arguments take a `DmaBuffer`, or a `byte[]` / `string` that MicroCS copies
into a temporary buffer (receive buffers must be `DmaBuffer`s). The synchronous
calls (`SPI.Write(bus, buf)`, `SPI.Read(bus, buf)`, `QSPI.Read(…, buf)`) also
take `DmaBuffer`s, with no size limit.

| Call | Completes when |
|---|---|
| `SPI.TransferAsync(bus, tx, rx[, cs])` · `SPI.WriteAsync(bus, data[, cs])` · `SPI.ReadAsync(bus, rx[, cs])` | all bytes clocked; `cs` is driven around the transfer |
| `I2C.WriteAsync(bus, addr, data)` · `I2C.ReadAsync(bus, addr, rx)` · `I2C.WriteReadAsync(bus, addr, tx, rx)` | stop condition (a NACK fails the transfer) |
| `UART.WriteAsync(port, data)` · `UART.ReadAsync(port, rx)` | sent / `rx` is full |
| `QSPI.ReadAsync(bus, instr, addr, rx[, dummy[, lines[, addrBytes]]])` · `QSPI.WriteAsync(bus, instr, addr, data[, lines[, addrBytes]])` | command done |
| `Dma.CopyAsync(dst, src[, count[, dstOffset[, srcOffset]]])` · `Dma.FillAsync(dst, value[, offset[, count]])` | memory to memory |
| `I2cDevice.WriteAsync / ReadAsync / WriteReadAsync`, `SpiDevice.TransferAsync / WriteAsync / ReadAsync` | same, on the device's bus / address / CS |

## DmaTransfer

| Member | Description |
|---|---|
| `Wait([timeoutMs])` → `bool` | `true` when finished, `false` on timeout; throws `IOException` / `TimeoutException` if the transfer failed |
| `IsCompleted` · `Succeeded` · `Error` (text or `null`) | non-blocking state |
| `Result` | bytes transferred (waits for the end) |
| `OnComplete(fn)` · `OnComplete(n => …)` | runs on the VM thread between statements, in `Thread.Sleep`, `Hal.Run` … (at once when already finished) |
| `Cancel()` → `bool` | stop it (best effort); the transfer completes with an error |
| `Id` | the HAL transfer id |

## Dma

| Member | Description |
|---|---|
| `Dma.Copy(dst, src[, count[, dstOffset[, srcOffset]]])` → bytes · `Dma.Fill(dst, value[, offset[, count]])` | synchronous memory DMA (CPU copy where there is none) |
| `Dma.CopyAsync(...)` · `Dma.FillAsync(...)` | see above |
| `Dma.WaitAll([timeoutMs])` → `bool` · `Dma.Pending` | transfers in flight |
| `Dma.Available` · `Dma.Channels` · `Dma.Alignment` · `Dma.MaxTransfers` | board facts; `MaxTransfers` = `MCS_HAL_MAX_DMA` |

## Rules

- **Order per bus.** Transfers on one bus run in order: a new transfer, async
  or not, waits for the previous one on that bus. Different buses run in
  parallel.
- **In flight.** Up to `MCS_HAL_MAX_DMA` transfers (default 4, 1–8, in
  `mcs_user_config.h`); more wait for a free slot.
- **Lifetime.** The buffers and the `DmaTransfer` stay alive until the transfer
  ends, even if the script drops its references. Call `Dma.WaitAll()` before
  a script ends if the last transfers matter.
- **Errors** are reported when the transfer ends (`Wait` throws, `Succeeded`
  is false, `Error` holds the reason), not when it starts — except arguments
  that are wrong from the start (`ArgumentException`).
- **Callbacks** never run inside an interrupt: completion is an
  `MCS_HAL_EV_DMA` event dispatched like GPIO / timer events.
- `SpiConfig.Dma = false` keeps a bus on CPU transfers; `true` asks for DMA
  where the board decides per transfer.
- `MCS_ENABLE_HAL_DMA 0` (in `mcs_user_config.h`) leaves `DmaBuffer`, `Dma`,
  `DmaTransfer` and the `…Async` calls out of the firmware (~11 KB of flash on
  Cortex-M0); `Hal.Has("DMA")` is then `false`.

## Boards

| Board | Background DMA | Notes |
|---|---|---|
| ESP32 family | SPI, QSPI (all chips), memory copy (GDMA chips: C2/C3/C6/H2/S3/P4) | `DmaBuffer` in internal DMA-capable RAM; `SpiConfig.MaxTransfer` sizes the driver |
| RP2040 / RP2350 | SPI, memory copy / fill | two channels per SPI transfer, completion on `DMA_IRQ_1` |
| Simulator (`--sim`) | all | completions go through the same event path as on hardware, so scripts can be tested on a PC |
| STM32, Zephyr, Arduino, Cortex-M | — (synchronous) | the async API works and completes at once |

## Ports: implementing DMA

All members are optional (`mcs_hal.h`, API v3):

```c
hal->dma_channels = 8;                      /* > 0: Dma.Available, Hal.Has("DMA") */
hal->dma_align = 4;
hal->dma_alloc = my_alloc;                  /* DMA-capable memory for DmaBuffer, NULL = VM heap */
hal->dma_free = my_free;
hal->dma_copy = my_copy;                    /* (ctx, dst, src, n, flags MCS_DMA_SRC_FIXED/DST_FIXED, id) */
hal->dma_abort = my_abort;                  /* (ctx, id): no completion event follows */
hal->spi_transfer_async = my_spi_async;     /* (ctx, bus, tx, rx, n, id) */
```

- Start the transfer and return 0; when it ends, post
  `mcs_hal_post(MCS_HAL_EV_DMA, id, bytes_or_negative_error)` — usually from
  the DMA-complete interrupt. The buffers stay valid until then.
- Return `MCS_HAL_ENOTSUP` for a request the engine cannot do (a fill, an odd
  size, no free channel): the library runs the synchronous call instead and
  completes the transfer at once. `MCS_HAL_EBUSY` and other errors fail it.
- `spi_transfer_async` may get `tx` or `rx` NULL (send `0xFF` / discard).
- On cores with a data cache, clean `tx` before and invalidate `rx` after the
  transfer, or return memory from `dma_alloc` that is not cached.

See `ports/esp32/mcs_port_esp32.c` and `ports/rp2/mcs_port_rp2.c` for complete
implementations and `ports/template/mcs_port_template.c` for a skeleton.
