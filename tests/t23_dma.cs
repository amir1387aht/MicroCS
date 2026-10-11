// args: --sim --sim-virtual
// HAL v3: DmaBuffer, asynchronous (DMA) transfers, Dma.Copy / Fill
Console.WriteLine($"dma={Hal.Has("DMA")} spi={Hal.Has("DMA.SPI")} mem={Hal.Has("DMA.Memory")} channels={Dma.Channels} align={Dma.Alignment} max={Dma.MaxTransfers}");

var buf = new DmaBuffer(8);
Console.WriteLine($"{buf} len={buf.Length} [0]={buf[0]}");
buf[0] = 0xAB; buf.Fill(1, 1, 3);
Console.WriteLine(string.Join(",", buf.ToArray()));
Console.WriteLine(buf.CopyFrom(new byte[] { 5, 6 }, 6) + " " + string.Join(",", buf.ToArray(4)));
try { buf[8] = 1; } catch (IndexOutOfRangeException e) { Console.WriteLine(e.Message); }
var text = new DmaBuffer("hello");
Console.WriteLine(text.GetString() + " " + text.GetString(1, 3));

// SPI: start, keep working, finish
SPI.Open(0, 4_000_000);
var tx = new DmaBuffer(new byte[] { 1, 2, 3, 4 });
var rx = new DmaBuffer(4);
var t = SPI.TransferAsync(0, tx, rx);
Console.WriteLine($"pending={Dma.Pending} busy={tx.Busy}");
try { tx[0] = 9; } catch (InvalidOperationException e) { Console.WriteLine(e.Message); }
t.OnComplete(n => Console.WriteLine($"spi done: {n} bytes"));
Console.WriteLine($"wait={t.Wait()} ok={t.Succeeded} result={t.Result} rx={string.Join(",", rx.ToArray())} busy={tx.Busy}");

// a large frame in one transfer (bigger than the 256-byte stack transfers)
var frame = new DmaBuffer(4096);
frame.Fill(0x55);
var w = SPI.WriteAsync(0, frame, 5);
var queued = SPI.WriteAsync(0, new byte[] { 7, 7 });   // waits for the first one: transfers on a bus stay in order
Console.WriteLine($"queued: first done={w.IsCompleted} second result={queued.Result}");
SPI.Write(0, frame);                                     // synchronous calls take DmaBuffers too
Console.WriteLine("big write ok " + SPI.Read(0, new DmaBuffer(300)).Length);

// callbacks run between statements / while sleeping
int fired = 0;
var a = Dma.CopyAsync(new DmaBuffer(16), new DmaBuffer(new byte[16]));
a.OnComplete(() => fired++);
Thread.Sleep(5);
Console.WriteLine($"copy callback fired={fired} completed={a.IsCompleted}");

// memory to memory
var dst = new DmaBuffer(6);
Console.WriteLine("copied " + Dma.Copy(dst, new byte[] { 1, 2, 3 }, 3, 2));
Dma.Fill(dst, 0xEE, 0, 2);
Console.WriteLine(string.Join(",", dst.ToArray()));
var f = Dma.FillAsync(dst, 0);
Console.WriteLine($"fill {f.Wait(100)} {string.Join(",", dst.ToArray())}");

// I2C: a NACK is reported when the transfer ends
var reg = new DmaBuffer(1);
var ok = I2C.WriteReadAsync(0, 0x68, new byte[] { 0x75 }, reg);
ok.Wait();
Console.WriteLine($"WHO_AM_I 0x{reg[0]:X2}");
var nack = new I2cDevice(0, 0x33).WriteAsync(new byte[] { 1 });
try { nack.Wait(); } catch (IOException e) { Console.WriteLine("nack: " + e.Message); }
Console.WriteLine($"succeeded={nack.Succeeded} error={nack.Error}");

// UART: ReadAsync completes when the buffer is full
UART.Open(1, 115200);
var line = new DmaBuffer(5);
var r = UART.ReadAsync(1, line);
UART.WriteAsync(1, "abc").Wait();
Console.WriteLine($"after 3 bytes: {r.IsCompleted}");
UART.Write(1, "de");
Console.WriteLine($"after 5 bytes: {r.Wait(100)} '{line.GetString()}'");
var never = UART.ReadAsync(1, new DmaBuffer(4));
Console.WriteLine($"timeout={never.Wait(10)} cancel={never.Cancel()} done={never.IsCompleted} pending={Dma.Pending}");

// QSPI flash read straight into a buffer
var id = new DmaBuffer(3);
QSPI.ReadAsync(0, 0x9F, -1, id).Wait();
Console.WriteLine("jedec " + string.Join(",", id.ToArray()));
QSPI.Read(0, 0x03, 0, id);
Console.WriteLine("flash " + string.Join(",", id.ToArray()));

// many at once: more than MCS_HAL_MAX_DMA in flight waits for a free slot
var list = new List<DmaTransfer>();
for (int i = 0; i < 10; i++) list.Add(Dma.FillAsync(new DmaBuffer(32), i));
Console.WriteLine($"all={Dma.WaitAll(1000)} pending={Dma.Pending} last={list[9].Result}");
