// oled_wiring.cs - pick the bus your display uses. Pins are numbers or names ("GP4", "PA5").
// The display is not touched before Begin(), so creating one never fails on wiring.
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

int wiring = 0;          // change to try another bus
U8g2 d;
switch (wiring)
{
    case 0:  d = U8g2.I2C("ssd1306_i2c_128x64_noname", 0, 0x3C); break;          // hardware I2C bus 0
    case 1:  d = U8g2.I2C("ssd1306_i2c_128x64_noname", 0, 0x3D, 16); break;      // other address, reset pin 16
    case 2:  d = U8g2.SoftI2C("ssd1306_i2c_128x64_noname", 22, 21); break;       // bit-banged: SCL 22, SDA 21
    case 3:  d = U8g2.SPI("ssd1306_128x64_noname", 0, 5, 4, 16); break;          // SPI bus 0: CS 5, DC 4, RESET 16
    case 4:  d = U8g2.SoftSPI("ssd1306_128x64_noname", 18, 23, 5, 4); break;     // clock 18, data 23, CS 5, DC 4
    case 5:  d = U8g2.Soft3WireSPI("ssd1306_128x64_noname", 18, 23, 5); break;   // 9-bit SPI, no DC pin
    case 6:  d = U8g2.Parallel8080("ssd1306_128x64_noname", new int[] { 0, 1, 2, 3, 4, 5, 6, 7 }, 8, 9, 10); break;  // WR, CS, DC
    case 7:  d = U8g2.Parallel6800("ssd1306_128x64_noname", new int[] { 0, 1, 2, 3, 4, 5, 6, 7 }, 8, 9, 10); break;  // E, CS, DC
    default: d = U8g2.KS0108("ks0108_128x64", new int[] { 0, 1, 2, 3, 4, 5, 6, 7 }, 8, 9, 10, 11, null); break;    // E, DC, CS0..2
}
Console.WriteLine($"{d.Display}: bus {d.Bus}, {d.Width}x{d.Height}, {d.Cols}x{d.Rows} tiles");
d.SetBusClock(400_000);                 // used by Begin(); I2C address: d.SetI2CAddress(0x3D)
d.Begin();
d.SetFont("6x10_tf");
d.ClearBuffer();
d.DrawStr(0, 12, "Bus: " + d.Bus);
d.DrawStr(0, 26, $"clock {d.BusClock} Hz");
d.SendBuffer();
