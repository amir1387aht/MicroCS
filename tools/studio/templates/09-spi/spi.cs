// SPI bus 0 at 1 MHz, mode 0, chip select on GPIO 10.
SPI.Open(0, 1000000, 0);
byte[] rx = SPI.Transfer(0, new byte[] { 0x9F, 0, 0, 0 }, 10);
Console.WriteLine($"received {BitConverter.ToString(rx)}");
