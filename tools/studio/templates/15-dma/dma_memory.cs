// Memory to memory: the DMA engine where there is one, the CPU elsewhere.
var src = new DmaBuffer(4096);
for (int i = 0; i < src.Length; i++) src[i] = (byte)i;
var dst = new DmaBuffer(4096);
Console.WriteLine($"copied {Dma.Copy(dst, src)} bytes, dst[300] = {dst[300]}");
Dma.Fill(dst, 0xEE, 0, 16);
Console.WriteLine(BitConverter.ToString(dst.ToArray(0, 4)));
var jobs = new List<DmaTransfer>();
for (int k = 0; k < 6; k++) jobs.Add(Dma.FillAsync(new DmaBuffer(256), k));
Console.WriteLine($"all done: {Dma.WaitAll(1000)}, pending {Dma.Pending}, last {jobs[5].Result} bytes");
