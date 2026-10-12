// oled_two_displays.cs - every display is its own object. Two SSD1306 on one bus need different
// addresses (the 0x3D board has its address resistor moved).
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var left = U8g2.I2C("ssd1306_i2c_128x64_noname", 0, 0x3C);
var right = U8g2.I2C("ssd1306_i2c_128x64_noname", 0, 0x3D);
// or on SPI: var right = U8g2.SPI("ssd1306_128x64_noname", 0, 5, 4, 16);
left.Begin();
left.SetFont("helvB10_tr");
left.ClearBuffer();
left.DrawStr(0, 20, "left");
left.SendBuffer();
try
{
    right.Begin();
    right.SetFont("helvB10_tr");
    right.ClearBuffer();
    right.DrawStr(0, 20, "right");
    right.SendBuffer();
}
catch (IOException e)
{
    Console.WriteLine("second display: " + e.Message);   // not connected
}
right.Dispose();                                     // frees its frame buffer (the picture stays)
Console.WriteLine($"left: {left.Display} at 0x{left.I2CAddress:X2}");
