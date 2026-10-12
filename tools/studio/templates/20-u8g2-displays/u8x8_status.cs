// u8x8_status.cs - fixed labels drawn once, values redrawn in place (no flicker, no buffer).
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var t = new U8x8("ssd1306_i2c_128x64_noname");
t.Begin();
t.SetFont("chroma48medium8_r");
t.SetInverseFont(true);
t.DrawString(0, 0, "  MicroCS node  ");
t.SetInverseFont(false);
t.DrawString(0, 2, "A0 mV:");
t.DrawString(0, 3, "uptime:");
t.DrawString(0, 4, "loops:");
t.SetFont("8x13_1x2_r");                              // a 1x2 tile font for the big line
t.DrawString(0, 6, "status: OK");
t.SetFont("chroma48medium8_r");
for (int n = 1; n <= 30; n++)
{
    t.DrawString(8, 2, $"{ADC.ReadMillivolts(0),6}");
    t.DrawString(8, 3, $"{Environment.TickCount / 1000,5} s");
    t.DrawString(8, 4, $"{n,6}");
    Thread.Sleep(50);
}
