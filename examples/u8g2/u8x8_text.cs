// U8x8: text-only output without a frame buffer - 8x8 pixel tiles go straight to the display,
// so it needs almost no RAM (16 columns x 8 rows on a 128x64 panel).
//   ./build/mcs_u8g2 --oled examples/u8g2/u8x8_text.cs
var t = new U8x8("ssd1306_i2c_128x64_noname");
t.Begin();
t.SetFont("chroma48medium8_r");
t.DrawString(0, 0, "U8x8 terminal");
t.Draw2x2String(0, 2, "BIG");
t.SetInverseFont(true);
t.DrawString(0, 5, " inverted ");
t.SetInverseFont(false);
for (int i = 0; i <= 10; i++)
{
    t.SetCursor(0, 7);
    t.Print($"count {i,3}");
    Thread.Sleep(100);
}
