// Packet: 0xAA, length, payload..., checksum (sum of payload bytes & 0xFF)
byte[] Frame(byte[] payload)
{
    var p = new List<byte> { 0xAA, (byte)payload.Length };
    int sum = 0;
    foreach (var b in payload) { p.Add(b); sum += b; }
    p.Add((byte)(sum & 0xFF));
    return p.ToArray();
}
var data = new List<byte>();
data.AddRange(BitConverter.GetBytes(1234));          // int32, little-endian
data.AddRange(BitConverter.GetBytes((short)-5));
byte[] pkt = Frame(data.ToArray());
Console.WriteLine(BitConverter.ToString(pkt));

UART.Open(1, 115200);
UART.Write(1, pkt);
Console.WriteLine($"sent {pkt.Length} bytes; value back = {BitConverter.ToInt32(pkt, 2)}");
