// 24C02-style EEPROM at 0x50: 1 address byte, 8-byte pages.
I2C.Open(0, 100000);
var rom = new I2cDevice(0, 0x50);
byte[] text = Encoding.UTF8.GetBytes("MicroCS");
var w = new List<byte> { 0x10 };                       // start address 0x10
w.AddRange(text);
rom.Write(w.ToArray());
Thread.Sleep(10);                                      // write cycle time
byte[] back = rom.WriteRead(new byte[] { 0x10 }, text.Length);
Console.WriteLine($"read back: {Encoding.UTF8.GetString(back)}");
