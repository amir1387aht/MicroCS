// TMP102 at 0x48: 12-bit temperature, 0.0625 C per bit (LM75: same layout, 9 bits).
I2C.Open(0, 100000);
var sensor = new I2cDevice(0, 0x48);
for (int i = 0; i < 5; i++)
{
    byte[] b = sensor.ReadRegisters(0x00, 2);
    int raw = ((b[0] << 8) | b[1]) >> 4;
    if (raw > 0x7FF) raw -= 4096;                     // negative temperatures
    Console.WriteLine($"{raw * 0.0625:F2} C");
    Thread.Sleep(500);
}
