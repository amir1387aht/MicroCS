// QSPI NOR flash on bus 0.
QSPI.Open(0, 10000000);
byte[] id = QSPI.Read(0, 0x9F, -1, 3);                  // -1 = no address phase
Console.WriteLine($"JEDEC {BitConverter.ToString(id)}");
byte[] data = QSPI.Read(0, 0x03, 0, 16);                 // READ at address 0
Console.WriteLine($"first bytes {BitConverter.ToString(data)}");
