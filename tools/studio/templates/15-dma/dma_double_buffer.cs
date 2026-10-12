// Two frame buffers: render into one while DMA sends the other.
SPI.Open(0, new SpiConfig(20_000_000, 0) { Cs = 5 });
var frames = new[] { new DmaBuffer(1024), new DmaBuffer(1024) };
DmaTransfer sending = null;
var sw = Stopwatch.StartNew();
for (int f = 0; f < 30; f++)
{
    var draw = frames[f % 2];
    draw.Fill(0);
    draw[f * 8 % 1024] = 0xFF;               // ... render here
    sending?.Wait();                         // the previous frame is out
    sending = SPI.WriteAsync(0, draw);       // writing to a busy buffer would throw
}
sending.Wait();
Console.WriteLine($"30 frames in {sw.ElapsedMilliseconds} ms");
