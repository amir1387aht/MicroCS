// W25Qxx on QSPI bus 0: JEDEC id (0x9F, no address), then 256 bytes with quad read (0x6B).
QSPI.Open(0, 40_000_000);
var id = new DmaBuffer(3);
QSPI.ReadAsync(0, 0x9F, -1, id).Wait();
Console.WriteLine("JEDEC " + BitConverter.ToString(id.ToArray()));
var page = new DmaBuffer(256);
QSPI.ReadAsync(0, 0x6B, 0x000000, page, 8, 4).Wait();   // 8 dummy cycles, 4 data lines
Console.WriteLine("first bytes " + BitConverter.ToString(page.ToArray(0, 8)));
