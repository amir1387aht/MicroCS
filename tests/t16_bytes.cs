// args: --sim --sim-virtual
// Encoding + BitConverter helpers (registered with the HAL)
byte[] b = Encoding.UTF8.GetBytes("héllo");
Console.WriteLine($"{b.Length} {BitConverter.ToString(b)} {Encoding.UTF8.GetString(b)} {Encoding.UTF8.GetString(b, 3, 3)}");
Console.WriteLine(BitConverter.ToString(Encoding.ASCII.GetBytes("héllo")) + " " + Encoding.ASCII.GetString(b));
var x = BitConverter.GetBytes(0x12345678);
Console.WriteLine($"{BitConverter.ToString(x)} {BitConverter.ToInt32(x, 0):X} {BitConverter.ToInt16(x, 2):X} {BitConverter.ToUInt16(new byte[]{0xFF,0xFF})} {BitConverter.ToInt16(new byte[]{0xFF,0xFF})}");
Console.WriteLine($"{BitConverter.ToSingle(BitConverter.GetBytes(1.5), 4)} {BitConverter.ToDouble(BitConverter.GetBytes(2.25))} {BitConverter.IsLittleEndian} {BitConverter.GetBytes(true).Length} {BitConverter.ToString(new byte[0])}|");
try { BitConverter.ToInt32(new byte[]{1,2}, 0); } catch (ArgumentOutOfRangeException e) { Console.WriteLine(e.Message); }
