// ADS1115 at 0x48: 16-bit ADC, +-4.096 V range, single-shot per channel
I2C.Open(0, 400000);
var adc = new I2cDevice(0, 0x48);

double ReadVolts(int channel)
{
    int mux = 4 + channel;                                   // AINx vs GND
    int config = 0x8000 | (mux << 12) | (1 << 9) | (1 << 8) | (4 << 5) | 3;   // start, PGA 4.096 V, single, 128 SPS
    adc.Write(new byte[] { 0x01, (byte)(config >> 8), (byte)config });
    Thread.Sleep(9);                                          // conversion time at 128 SPS
    byte[] r = adc.ReadRegisters(0x00, 2);
    short raw = (short)((r[0] << 8) | r[1]);
    return raw * 4.096 / 32768.0;
}

for (int i = 0; i < 10; i++)
{
    Console.WriteLine($"a0:{ReadVolts(0):F4} a1:{ReadVolts(1):F4} a2:{ReadVolts(2):F4} a3:{ReadVolts(3):F4}");
    Thread.Sleep(500);
}
