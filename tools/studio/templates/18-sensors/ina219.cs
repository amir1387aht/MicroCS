// INA219 at 0x40 with a 0.1 ohm shunt
I2C.Open(0);
var ina = new I2cDevice(0, 0x40);
void Write16(int reg, int v) => ina.Write(new byte[] { (byte)reg, (byte)(v >> 8), (byte)v });
int Read16(int reg) { byte[] b = ina.ReadRegisters(reg, 2); return (short)((b[0] << 8) | b[1]); }

Write16(0x00, 0x399F);                 // 32 V range, +-320 mV shunt, 12-bit, continuous
for (int i = 0; i < 10; i++)
{
    double shuntMv = Read16(0x01) * 0.01;
    double busV = (Read16(0x02) >> 3) * 0.004;
    double mA = shuntMv / 0.1;
    Console.WriteLine($"V:{busV:F3} mA:{mA:F1} mW:{busV * mA:F0}");
    Thread.Sleep(500);
}
