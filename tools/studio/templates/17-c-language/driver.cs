// A reusable driver: wraps an I2C sensor behind a clean API.
class Tmp102 : IDisposable
{
    readonly I2cDevice dev;
    public Tmp102(int bus, int address = 0x48) { I2C.Open(bus); dev = new I2cDevice(bus, address); }
    public double Celsius
    {
        get
        {
            byte[] b = dev.ReadRegisters(0, 2);
            int raw = ((b[0] << 8) | b[1]) >> 4;
            return (raw > 0x7FF ? raw - 4096 : raw) * 0.0625;
        }
    }
    public double Fahrenheit => Celsius * 9 / 5 + 32;
    public void Dispose() => Console.WriteLine("sensor released");
}

using (var t = new Tmp102(0))
    Console.WriteLine($"{t.Celsius:F2} C = {t.Fahrenheit:F1} F");
