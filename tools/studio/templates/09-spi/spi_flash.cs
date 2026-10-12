// W25Qxx flash: CS on GPIO 10, 8 MHz, mode 0.
var flash = new SpiDevice(0, 10, 8000000, 0);
byte[] id = flash.WriteRead(new byte[] { 0x9F }, 3);
Console.WriteLine($"JEDEC id {id[0]:X2} {id[1]:X2} {id[2]:X2}" + (id[0] == 0xEF ? " (Winbond)" : ""));
byte[] st = flash.WriteRead(new byte[] { 0x05 }, 1);
Console.WriteLine($"status register 0x{st[0]:X2}");
