// BME280 (id 0x60) / BMP280 (id 0x58) at 0x76 or 0x77.
I2C.Open(0, 400000);
int addr = I2C.Scan(0).Contains(0x77) ? 0x77 : 0x76;
var bme = new I2cDevice(0, addr);
int id = bme.ReadRegister(0xD0);
Console.WriteLine($"chip id 0x{id:X2} at 0x{addr:X2} ({(id == 0x60 ? "BME280" : id == 0x58 ? "BMP280" : "unknown")})");
bme.WriteRegister(0xF4, 0x27);                         // normal mode, x1 oversampling
Thread.Sleep(50);
byte[] t = bme.ReadRegisters(0xFA, 3);
Console.WriteLine($"raw temperature {(t[0] << 12) | (t[1] << 4) | (t[2] >> 4)} (apply the calibration from 0x88..)");
