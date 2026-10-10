// Fonts: the ones in MCS_U8G2_FONTS are in flash; every other u8g2 font loads from a file.
// Make the files on the PC and copy them to the board's /fonts folder:
//   python3 tools/u8g2.py fonts helv            (names and sizes; 2000+ fonts)
//   python3 tools/u8g2.py extract profont12_tr unifont_t_symbols open_iconic_weather_2x_t -o myfs/fonts
//   ./build/mcs_u8g2 --oled --fs myfs examples/u8g2/fonts.cs
var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin();
Console.WriteLine("in flash: " + string.Join(", ", U8g2.Fonts));

d.ClearBuffer();
d.SetFont("6x10_tf");                       // built in
d.DrawStr(0, 10, "6x10_tf (flash)");
try
{
    d.SetFont("profont12_tr");              // /fonts/u8g2_font_profont12_tr.bin
    d.DrawStr(0, 24, "profont12 (file)");
    d.SetFont("unifont_t_symbols");         // UTF-8 symbols
    d.DrawUTF8(0, 42, "☀ ☁ ☂ ☃ ★");
    d.SetFont("open_iconic_weather_2x_t");  // icon fonts: glyph codes
    d.DrawGlyph(96, 62, 0x45);
}
catch (ArgumentException e)
{
    Console.WriteLine(e.Message);           // the font file is not there
    d.DrawStr(0, 40, "font files missing");
}
d.SendBuffer();
Console.WriteLine("current font: " + d.Font);
