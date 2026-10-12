// BH1750 ambient light sensor at 0x23
I2C.Open(0);
var bh = new I2cDevice(0, 0x23);
bh.Write(new byte[] { 0x01 });           // power on
bh.Write(new byte[] { 0x10 });           // continuous high-resolution mode (1 lx)
Thread.Sleep(180);
for (int i = 0; i < 10; i++)
{
    byte[] r = bh.Read(2);
    double lux = ((r[0] << 8) | r[1]) / 1.2;
    Console.WriteLine($"lux:{lux:F0}");
    Thread.Sleep(500);
}
