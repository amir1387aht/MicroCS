// U8g2 hello world: a 128x64 SSD1306 OLED on I2C (SDA/SCL of bus 0, address 0x3C).
// Needs a firmware built with u8g2 (docs/U8G2.md). On the PC:
//   make mcs-u8g2 && ./build/mcs_u8g2 --oled examples/u8g2/hello.cs
var oled = new U8g2("ssd1306_i2c_128x64_noname");   // also: "U8G2_SSD1306_128X64_NONAME_F_HW_I2C"
oled.Begin();

oled.ClearBuffer();
oled.SetFont("helvB10_tr");
oled.DrawStr(0, 14, "Hello, MicroCS!");
oled.SetFont("6x10_tf");
oled.DrawStr(0, 30, $"{oled.Width}x{oled.Height} {oled.Display}".Substring(0, 21));
oled.DrawFrame(0, 36, 128, 28);
oled.DrawStr(6, 54, "C# on a microcontroller");
oled.SendBuffer();
