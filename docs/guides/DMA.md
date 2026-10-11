# DMA: send and receive in the background

Writing a full OLED frame or reading a block of flash keeps the CPU busy for milliseconds. With
DMA the hardware moves the bytes while your C# keeps running. This guide goes from the first
`DmaBuffer` to a display loop that draws the next frame while the last one is sent. The
reference is [DMA and asynchronous transfers](../DMA.md).

Works on every board running MicroCS 1.12+: ESP32 and RP2 really run SPI (and memory copies) in
the background; elsewhere the same calls finish immediately, so the script does not change.
The PC simulator (`mcs --sim --sim-virtual`) runs every example below.

## 1. Check what runs in the background

```csharp
Console.WriteLine($"DMA {Hal.Has("DMA")}  SPI {Hal.Has("DMA.SPI")}  memory {Hal.Has("DMA.Memory")}");
Console.WriteLine($"channels {Dma.Channels}, alignment {Dma.Alignment}, in flight {Dma.MaxTransfers}");
```

## 2. A DMA buffer

```csharp
var frame = new DmaBuffer(1024);   // 128 x 64 pixels, 1 bit each
frame.Fill(0x00);
frame[0] = 0xFF;
frame.CopyFrom(new byte[] { 1, 2, 3 }, 10);
Console.WriteLine(frame);          // DmaBuffer[1024]
```

A `DmaBuffer` lives in memory the DMA engine can reach (on ESP32: internal RAM). Use it like a
`byte[]`.

## 3. Start a transfer, do something else, finish

```csharp
SPI.Open(1, new SpiConfig(20_000_000, 0, 6, 7) { Cs = 10 });

var t = SPI.WriteAsync(1, frame);       // returns at once
int work = 0;
while (!t.IsCompleted) work++;          // the CPU is free meanwhile
Console.WriteLine($"{t.Result} bytes sent, {work} loops done meanwhile");
```

Instead of polling you can wait (`t.Wait()`, `t.Wait(100)` with a timeout) or get a callback:

```csharp
SPI.WriteAsync(1, frame).OnComplete(n => Console.WriteLine($"frame out ({n} bytes)"));
```

Callbacks run between statements and while the script sleeps — never inside an interrupt.

## 4. Double buffering: draw while sending

```csharp
var a = new DmaBuffer(1024);
var b = new DmaBuffer(1024);
DmaTransfer sending = null;
for (int f = 0; f < 100; f++) {
    var draw = (f % 2 == 0) ? a : b;
    draw.Fill(0);
    draw[f % 1024] = 0xFF;               // ... render the next frame here
    if (sending != null) sending.Wait(); // the previous frame is out
    sending = SPI.WriteAsync(1, draw);
}
sending.Wait();
```

While a transfer uses a buffer, writing to it throws `InvalidOperationException` — that is why
the loop draws into the *other* buffer.

## 5. Reading: sensors, flash, UART

```csharp
var regs = new DmaBuffer(6);
I2C.WriteReadAsync(0, 0x68, new byte[] { 0x3B }, regs).Wait();     // MPU-6050 accelerometer

var id = new DmaBuffer(3);
QSPI.ReadAsync(0, 0x9F, -1, id).Wait();                             // flash JEDEC id

UART.Open(1, 9600);
var gps = new DmaBuffer(64);
var line = UART.ReadAsync(1, gps);                                  // completes when 64 bytes arrived
if (!line.Wait(2000)) line.Cancel();
```

A failed transfer (an I²C NACK, a timeout) throws when you `Wait()` for it; `Succeeded` and
`Error` tell you without an exception.

## 6. Memory to memory

```csharp
var copy = new DmaBuffer(1024);
Dma.Copy(copy, frame);             // DMA engine where there is one, CPU copy elsewhere
Dma.Fill(copy, 0x55, 0, 512);
Dma.CopyAsync(copy, frame).Wait();
```

## Troubleshooting

* **`DmaBuffer is in use by a DMA transfer`** — `Wait()` for the transfer before writing to
  its buffer, or use a second buffer (step 4).
* **`too many DMA transfers in flight`** — more than `Dma.MaxTransfers` (`MCS_HAL_MAX_DMA`)
  were started without waiting; `Dma.WaitAll()` or raise `MCS_HAL_MAX_DMA` (1–8).
* **`OutOfMemoryException: no DMA-capable memory`** — smaller buffers; ESP32 DMA memory is the
  internal RAM.
* **Nothing is faster** — `Hal.Has("DMA.SPI")` is `false` on this board: transfers are
  synchronous there.
* Bus settings (pins, `Dma = false`, `MaxTransfer`): [peripheral settings guide](PERIPHERALS.md).
