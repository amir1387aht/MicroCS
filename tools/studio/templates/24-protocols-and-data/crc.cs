// CRC algorithms used in embedded protocols
int Crc8(byte[] data, int poly = 0x31, int init = 0xFF)          // Sensirion / Dallas style
{
    int crc = init;
    foreach (byte b in data) { crc ^= b; for (int i = 0; i < 8; i++) crc = (crc & 0x80) != 0 ? ((crc << 1) ^ poly) & 0xFF : (crc << 1) & 0xFF; }
    return crc;
}
int Crc16Modbus(byte[] data)
{
    int crc = 0xFFFF;
    foreach (byte b in data) { crc ^= b; for (int i = 0; i < 8; i++) crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xA001 : crc >> 1; }
    return crc;
}
int Crc32(byte[] data)                      // MicroCS integers are 32-bit signed: >>> is the logical shift
{
    int crc = -1;
    foreach (byte b in data) { crc ^= b; for (int i = 0; i < 8; i++) crc = (crc & 1) != 0 ? (crc >>> 1) ^ unchecked((int)0xEDB88320) : crc >>> 1; }
    return ~crc;
}

var text = Encoding.ASCII.GetBytes("123456789");                 // the standard check string
Console.WriteLine($"CRC-8   0x{Crc8(new byte[] { 0xBE, 0xEF }):X2}  (Sensirion example: 0x92)");
Console.WriteLine($"CRC-16  0x{Crc16Modbus(text):X4}  (Modbus check: 0x4B37)");
Console.WriteLine($"CRC-32  0x{Crc32(text):X8}  (check: 0xCBF43926)");
