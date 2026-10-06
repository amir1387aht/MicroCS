// 07 · SPI device with automatic chip select.
// SpiDevice(bus, csPin, frequencyHz, mode) drives CS low around every transfer.
// The simulator's SPI bus is a loopback (MISO = MOSI), so you get back what you send.
// Run: ./mcs --sim examples/hardware/07_spi_device.cs
var flash = new SpiDevice(0, 10, 8_000_000, 0);   // bus 0, CS on pin 10, 8 MHz, mode 0

byte[] reply = flash.Transfer(new byte[] { 0x9F, 0x00, 0x00, 0x00 });
Console.WriteLine("full-duplex: " + string.Join(" ", reply.Select(b => b.ToString("X2"))));

flash.Write(new byte[] { 0x06 });                  // e.g. WRITE ENABLE
byte[] status = flash.WriteRead(new byte[] { 0x05 }, 1);   // command, then read 1 byte
Console.WriteLine($"status byte read: {status.Length} byte(s)");
