// oled_bitmaps.cs - 1-bit images. XBM: LSB = leftmost pixel (GIMP "export as XBM", image2cpp).
// DrawBitmap: MSB = leftmost pixel (the Adafruit / u8glib format).
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin();

byte[] heart = { 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00 };        // 8x8 XBM
byte[] smiley =                                                            // 16x16 XBM, 2 bytes per row
{
    0xE0, 0x07, 0x18, 0x18, 0x04, 0x20, 0x02, 0x40, 0x32, 0x4C, 0x31, 0x8C, 0x01, 0x80, 0x01, 0x80,
    0x01, 0x80, 0x09, 0x90, 0x12, 0x48, 0xE2, 0x47, 0x04, 0x20, 0x18, 0x18, 0xE0, 0x07, 0x00, 0x00,
};
byte[] arrow = { 0x18, 0x3C, 0x7E, 0xFF, 0x18, 0x18, 0x18, 0x18 };        // 8x8, MSB first

d.ClearBuffer();
for (int i = 0; i < 6; i++) d.DrawXBM(i * 12, 0, 8, 8, heart);
d.DrawXBMP(0, 14, 16, 16, smiley);                   // P = data in flash on AVR; same here
d.DrawBitmap(24, 14, 1, 8, arrow);                   // bytes per row, height

d.DrawBox(40, 12, 40, 24);
d.SetBitmapMode(1);                                  // 1 = transparent: only set bits are drawn
d.SetDrawColor(0);
d.DrawXBM(52, 16, 16, 16, smiley);
d.SetDrawColor(1);
d.SetBitmapMode(0);

d.SetFont("open_iconic_embedded_1x_t");              // 8x8 icons, built in
for (int i = 0; i < 12; i++) d.DrawGlyph(i * 10, 52, 0x40 + i);
d.SetFont("5x7_tr");
d.DrawStr(0, 63, "open_iconic_embedded_1x_t");
d.SendBuffer();
