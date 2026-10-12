// Temperature and humidity: AHT20 / AHT21 (0x38) or SHT31 (0x44)
I2C.Open(0);
var found = I2C.Scan(0);

if (found.Contains(0x38))
{
    var aht = new I2cDevice(0, 0x38);
    aht.Write(new byte[] { 0xBE, 0x08, 0x00 });          // calibrate
    Thread.Sleep(10);
    aht.Write(new byte[] { 0xAC, 0x33, 0x00 });          // trigger a measurement
    Thread.Sleep(80);
    byte[] d = aht.Read(6);
    long hum = ((long)d[1] << 12) | ((long)d[2] << 4) | ((long)d[3] >> 4);
    long tmp = (((long)d[3] & 0x0F) << 16) | ((long)d[4] << 8) | d[5];
    Console.WriteLine($"AHT20: {tmp * 200.0 / 1048576 - 50:F1} C, {hum * 100.0 / 1048576:F1} %RH");
}
else if (found.Contains(0x44))
{
    var sht = new I2cDevice(0, 0x44);
    sht.Write(new byte[] { 0x24, 0x00 });                // single shot, high repeatability
    Thread.Sleep(20);
    byte[] d = sht.Read(6);
    int t = (d[0] << 8) | d[1], h = (d[3] << 8) | d[4];
    Console.WriteLine($"SHT31: {-45 + 175.0 * t / 65535:F1} C, {100.0 * h / 65535:F1} %RH");
}
else Console.WriteLine("no AHT20 (0x38) or SHT31 (0x44) found - addresses: " + string.Join(", ", found.Select(a => "0x" + a.ToString("X2"))));
