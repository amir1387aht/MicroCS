// DmaBuffer: memory the DMA engine can reach (ESP32: internal RAM). Use it like a byte[].
var buf = new DmaBuffer(16);
buf.Fill(0x20);
buf.CopyFrom("MicroCS", 0);
buf[7] = (byte)'!';
Console.WriteLine($"{buf} '{buf.GetString(0, 8)}'");
Console.WriteLine(BitConverter.ToString(buf.ToArray(0, 8)));
var copy = new DmaBuffer(buf);               // a copy (also from byte[] or string)
Console.WriteLine($"copy length {copy.Length}, busy {copy.Busy}");
