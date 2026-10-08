// Byte utilities that are always available (no HAL): Base64, hex, Encoding,
// BitConverter, BinaryPrimitives. Output is checked against .NET 8.
using System;
using System.Text;
using System.Buffers.Binary;

byte[] hello = Encoding.UTF8.GetBytes("Hello, MicroCS!");
string b64 = Convert.ToBase64String(hello);
Console.WriteLine(b64);
Console.WriteLine(Encoding.UTF8.GetString(Convert.FromBase64String(b64)));
Console.WriteLine(Convert.ToBase64String(hello, 7, 5));

// padding: 0, 1 and 2 '=' characters, empty input
string[] samples = { "", "f", "fo", "foo", "foob", "fooba", "foobar" };
foreach (var s in samples) {
    string e = Convert.ToBase64String(Encoding.ASCII.GetBytes(s));
    Console.WriteLine($"'{s}' -> '{e}' -> '{Encoding.ASCII.GetString(Convert.FromBase64String(e))}'");
}

// every byte value survives a round trip
byte[] all = new byte[256];
for (int i = 0; i < 256; i++) all[i] = (byte)i;
byte[] back = Convert.FromBase64String(Convert.ToBase64String(all));
bool same = back.Length == 256;
for (int i = 0; i < back.Length; i++) if (back[i] != i) same = false;
Console.WriteLine($"256 bytes round trip: {same}, base64 length {Convert.ToBase64String(all).Length}");
Console.WriteLine(Convert.ToBase64String(new byte[] { 0xFB, 0xFF, 0xBF }));

// whitespace is ignored when decoding
Console.WriteLine(Encoding.UTF8.GetString(Convert.FromBase64String(" SGVs\r\nbG8=\t")));

string[] bad = { "abc", "ab=c", "a===", "ab$d", "QQ==QQ==" };
foreach (var s in bad) {
    try { Convert.FromBase64String(s); Console.WriteLine("no error?"); }
    catch (FormatException e) { Console.WriteLine($"'{s}': {e.Message}"); }
}

// hex
Console.WriteLine(Convert.ToHexString(new byte[] { 0x00, 0x0F, 0xA5, 0xFF }));
Console.WriteLine(Convert.ToHexString(hello, 0, 5));
byte[] h = Convert.FromHexString("DEADbeef00");
Console.WriteLine($"{h.Length} {h[0]} {h[1]} {h[2]} {h[3]} {h[4]}");
Console.WriteLine(Convert.FromHexString("").Length);
foreach (var s in new[] { "ABC", "GG" }) {
    try { Convert.FromHexString(s); }
    catch (FormatException e) { Console.WriteLine(e.Message); }
}

// BitConverter
Console.WriteLine(BitConverter.ToString(hello, 7));
Console.WriteLine(BitConverter.ToString(hello, 7, 3));
Console.WriteLine(BitConverter.ToInt32(new byte[] { 0x78, 0x56, 0x34, 0x12 }, 0).ToString("X"));

// BinaryPrimitives (network byte order <-> little endian)
byte[] buf = new byte[8];
BinaryPrimitives.WriteInt32BigEndian(buf, 0x11223344);
Console.WriteLine(BitConverter.ToString(buf));
BinaryPrimitives.WriteInt16LittleEndian(buf, -2);
Console.WriteLine(BitConverter.ToString(buf));
Console.WriteLine(BinaryPrimitives.ReadInt32BigEndian(new byte[] { 0x11, 0x22, 0x33, 0x44 }).ToString("X"));
Console.WriteLine(BinaryPrimitives.ReadInt32LittleEndian(new byte[] { 0x11, 0x22, 0x33, 0x44 }).ToString("X"));
Console.WriteLine(BinaryPrimitives.ReadUInt16BigEndian(new byte[] { 0xBE, 0xEF }));
Console.WriteLine(BinaryPrimitives.ReadInt16BigEndian(new byte[] { 0xFF, 0xFE }));
Console.WriteLine(BinaryPrimitives.ReadUInt16LittleEndian(new byte[] { 0xBE, 0xEF }));
