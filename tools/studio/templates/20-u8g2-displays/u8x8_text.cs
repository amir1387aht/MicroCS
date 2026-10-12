// u8x8_text.cs - text-only output: 16 columns x 8 rows on a 128x64 panel, no frame buffer.
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var t = new U8x8("ssd1306_i2c_128x64_noname");
t.Begin();
t.SetFont("chroma48medium8_r");
Console.WriteLine($"{t.Cols}x{t.Rows} tiles, font {t.FontWidth}x{t.FontHeight}; U8x8 fonts: {string.Join(", ", U8x8.Fonts)}");
t.DrawString(0, 0, "U8x8 terminal");
t.Draw2x2String(0, 1, "BIG");                         // 2 x 2 tiles per character
t.Draw1x2String(8, 1, "tall");                        // 1 x 2 tiles
t.SetInverseFont(true);
t.DrawString(0, 4, " inverted ");
t.SetInverseFont(false);
t.DrawGlyph(12, 4, '*');
t.DrawUTF8(0, 5, "25°C");
Console.WriteLine($"UTF-8 length of '25°C': {t.GetUTF8Len("25°C")} tiles");

byte[] tiles = { 0x00, 0x7E, 0x42, 0x5A, 0x5A, 0x42, 0x7E, 0x00,      // a custom 8x8 tile: one byte per
                 0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18 };    // column, LSB = top pixel
t.DrawTile(14, 0, 2, tiles);

for (int i = 0; i <= 20; i++)
{
    t.ClearLine(7);
    t.SetCursor(0, 7);                                // tiles, not pixels
    t.Print($"count {i,3}");
    Thread.Sleep(50);
}
t.FillDisplay(); Thread.Sleep(200);                   // every pixel on
t.ClearDisplay();
t.DrawString(0, 3, "done");
