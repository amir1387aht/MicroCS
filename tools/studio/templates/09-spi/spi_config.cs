// SPI bus 0: 8 MHz, mode 0, SCK 18, MOSI 23, MISO 19; chip select 5 handled by MicroCS.
var spi = new SpiConfig(8_000_000, 0, 18, 23, 19) { Cs = 5 };
SPI.Open(0, spi);
byte[] rx = SPI.Transfer(0, new byte[] { 0x9F, 0, 0, 0 });   // CS from the config
Console.WriteLine("JEDEC id: " + BitConverter.ToString(rx, 1));
// one config per device: the bus is re-configured on every access
var sensor = new SpiDevice(0, 4, new SpiConfig(1_000_000, 3));
Console.WriteLine($"sensor on bus {sensor.Bus}, CS {sensor.ChipSelect}: " + BitConverter.ToString(sensor.Transfer(new byte[] { 0x80, 0 })));
