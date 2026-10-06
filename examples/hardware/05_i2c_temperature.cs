// 05 · Read a TMP102 temperature sensor with an I2cDevice object.
// Run: ./mcs --sim examples/hardware/05_i2c_temperature.cs
class Tmp102
{
    readonly I2cDevice dev;
    public Tmp102(int bus, int address = 0x48) { dev = new I2cDevice(bus, address); }

    // register 0 holds a 12-bit two's-complement value, 0.0625 °C per bit
    public double Celsius()
    {
        byte[] r = dev.ReadRegisters(0x00, 2);
        int raw = (r[0] << 4) | (r[1] >> 4);
        if (raw > 0x7FF) raw -= 4096;
        return raw * 0.0625;
    }
}

I2C.Open(0, 400000);
var sensor = new Tmp102(0);
for (int i = 0; i < 3; i++)
{
    double c = sensor.Celsius();
    Console.WriteLine($"temperature {c:F2} °C = {c * 9 / 5 + 32:F1} °F");
    Thread.Sleep(100);
}
