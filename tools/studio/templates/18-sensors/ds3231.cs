// DS3231 / DS1307 at 0x68: time in BCD registers 0x00..0x06
I2C.Open(0);
var rtc = new I2cDevice(0, 0x68);
int FromBcd(int b) => (b >> 4) * 10 + (b & 0x0F);
int ToBcd(int v) => ((v / 10) << 4) | (v % 10);

void SetTime(int year, int month, int day, int h, int m, int s)
{
    rtc.Write(new byte[] { 0x00, (byte)ToBcd(s), (byte)ToBcd(m), (byte)ToBcd(h), 1,
                           (byte)ToBcd(day), (byte)ToBcd(month), (byte)ToBcd(year % 100) });
}

// SetTime(2025, 1, 31, 12, 0, 0);              // run once to set the clock
byte[] r = rtc.ReadRegisters(0x00, 7);
Console.WriteLine($"20{FromBcd(r[6]):D2}-{FromBcd(r[5] & 0x1F):D2}-{FromBcd(r[4]):D2} " +
                  $"{FromBcd(r[2] & 0x3F):D2}:{FromBcd(r[1]):D2}:{FromBcd(r[0] & 0x7F):D2}");
byte[] t = rtc.ReadRegisters(0x11, 2);          // DS3231 temperature sensor
Console.WriteLine($"chip temperature {(sbyte)t[0] + (t[1] >> 6) * 0.25} C");
