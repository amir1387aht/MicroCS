// Save three readings as raw little-endian bytes and read them back.
var bytes = new List<byte>();
foreach (int v in new[] { 1000, -42, 123456 }) bytes.AddRange(BitConverter.GetBytes(v));
File.WriteAllBytes("/values.bin", bytes.ToArray());

byte[] back = File.ReadAllBytes("/values.bin");
for (int i = 0; i < back.Length; i += 4) Console.WriteLine(BitConverter.ToInt32(back, i));
Console.WriteLine(BitConverter.ToString(back));
