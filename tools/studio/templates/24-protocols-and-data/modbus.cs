// Modbus RTU: read holding registers (function 3) from slave 1 over RS-485 (UART 1)
const int PORT = 1;
UART.Open(PORT, 9600);

int Crc16(List<byte> d)
{
    int crc = 0xFFFF;
    foreach (byte b in d) { crc ^= b; for (int i = 0; i < 8; i++) crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xA001 : crc >> 1; }
    return crc;
}

int[] ReadHolding(int slave, int start, int count)
{
    var req = new List<byte> { (byte)slave, 3, (byte)(start >> 8), (byte)start, (byte)(count >> 8), (byte)count };
    int crc = Crc16(req);
    req.Add((byte)crc); req.Add((byte)(crc >> 8));
    UART.Write(PORT, req.ToArray());
    byte[] resp = UART.Read(PORT, 5 + 2 * count, 300);
    if (resp.Length < 5 + 2 * count) throw new TimeoutException($"Modbus: {resp.Length} bytes received");
    var body = new List<byte>(resp.Take(resp.Length - 2));
    if ((Crc16(body) & 0xFFFF) != (resp[resp.Length - 2] | (resp[resp.Length - 1] << 8))) throw new IOException("Modbus: bad CRC");
    var regs = new int[count];
    for (int i = 0; i < count; i++) regs[i] = (resp[3 + 2 * i] << 8) | resp[4 + 2 * i];
    return regs;
}

try { Console.WriteLine("registers: " + string.Join(", ", ReadHolding(1, 0, 4))); }
catch (Exception e) { Console.WriteLine(e.Message); }
