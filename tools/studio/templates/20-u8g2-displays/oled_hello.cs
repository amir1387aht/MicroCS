// oled_hello.cs - an SSD1306 / SH1106 OLED on I2C bus 0 (address 0x3C) with u8g2.
// U8g2 needs firmware built with u8g2 (docs/U8G2.md). PC: mcs_u8g2 --oled oled_hello.cs
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var oled = new U8g2("ssd1306_i2c_128x64_noname");   // SH1106: "sh1106_i2c_128x64_noname"
oled.Begin();                                        // init, clear, wake the display
oled.ClearBuffer();
oled.SetFont("helvB10_tr");
oled.DrawStr(0, 14, "Hello, MicroCS!");
oled.SetFont("6x10_tf");
oled.DrawStr(0, 28, $"{oled.Width}x{oled.Height}, {oled.BufferSize} B");
oled.DrawCircle(100, 46, 14);
oled.DrawFrame(0, 34, 70, 30);
oled.SendBuffer();                                   // the frame goes to the display
Console.WriteLine($"{oled.Display} on {oled.Bus}, font {oled.Font}");
Console.WriteLine("compiled-in displays: " + string.Join(", ", U8g2.Displays));
