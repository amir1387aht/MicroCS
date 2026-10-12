// A 1 KB display frame sent with DMA while the CPU keeps working.
// Boards without DMA run the same call synchronously - the script does not change.
SPI.Open(0, new SpiConfig(20_000_000, 0) { Cs = 5 });
var frame = new DmaBuffer(1024);
frame.Fill(0xAA);
var t = SPI.WriteAsync(0, frame);
int work = 0;
while (!t.IsCompleted) work++;               // free CPU time
Console.WriteLine($"{t.Result} bytes sent, {work} loops meanwhile (DMA.SPI = {Hal.Has("DMA.SPI")})");
SPI.WriteAsync(0, frame).OnComplete(n => Console.WriteLine($"callback: {n} bytes"));
Dma.WaitAll();
Hal.Poll();                                  // run the pending callback now
