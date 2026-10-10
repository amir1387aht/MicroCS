// args: --oled=128x32
// page buffer (_1), the Draw loop, rotation, fonts from /fonts (tools/u8g2.py extract), UTF-8, glyphs, 128x32 panel
var d = U8g2.I2C("ssd1306_i2c_128x32_univision_1");
Console.WriteLine($"{d.Display} {d.Width}x{d.Height} buf={d.BufferSize} rows/page={d.BufferTileHeight}");
d.Begin();
d.SetFont("profont12_tr");                 // not built in: /fonts/u8g2_font_profont12_tr.bin
Console.WriteLine("font=" + d.Font + " h=" + d.MaxCharHeight);
int pages = 0;
d.Draw(() => {
    pages++;
    d.DrawStr(0, 10, "page mode");
    d.DrawLine(0, 14, 127, 31);
    d.DrawEllipse(100, 10, 20, 8);
});
Console.WriteLine("pages=" + pages);
d.SetDisplayRotation(U8g2.R2);
d.SetFont("unifont_t_symbols");
d.Draw(g => { g.DrawUTF8(0, 14, "☀☁☂ ★☆"); g.DrawFrame(0, 0, 128, 32); });
d.SetDisplayRotation(U8g2.R0);
d.SetFont("open_iconic_weather_2x_t");
d.FirstPage();
do { d.DrawGlyph(0, 16, 0x40); d.DrawGlyph(20, 16, 0x41); d.DrawGlyph(40, 16, 0x42); } while (d.NextPage());
byte[] raw = File.ReadAllBytes("/fonts/u8g2_font_profont12_tr.bin");
d.SetFont(raw);                             // a font from bytes
Console.WriteLine("font=" + d.Font + " w=" + d.GetStrWidth("abc"));
d.SetFont("logisoso24_tn");
d.Draw(() => d.DrawStr(10, 28, "12:34"));
try { d.SetFont(new byte[] { 1, 2, 3 }); } catch (ArgumentException e) { Console.WriteLine(e.Message); }
