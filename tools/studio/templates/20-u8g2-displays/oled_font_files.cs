// oled_font_files.cs - fonts not built into the firmware are loaded from the board's /fonts folder.
// Make the files on the PC and upload them (Studio Files panel, or --fs on the PC):
//   python3 tools/u8g2.py fonts profont                    (search names and sizes)
//   python3 tools/u8g2.py extract profont12_tr unifont_t_symbols open_iconic_weather_2x_t -o myfs/fonts
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin();
d.ClearBuffer();
d.SetFont("6x10_tf");                                // built in: no file needed
d.DrawStr(0, 10, "6x10_tf (flash)");

bool TryFont(string name)
{
    try { d.SetFont(name); return true; }            // built-in first, else /fonts/u8g2_font_<name>.bin
    catch (ArgumentException e) { Console.WriteLine(e.Message); return false; }
}

if (TryFont("profont12_tr")) d.DrawStr(0, 24, "profont12 (file)");
if (TryFont("unifont_t_symbols")) d.DrawUTF8(0, 42, "☀ ☁ ☂ ★");
if (TryFont("open_iconic_weather_2x_t")) d.DrawGlyph(100, 62, 0x45);   // icon fonts: glyph codes

string path = "/fonts/u8g2_font_profont12_tr.bin";
if (File.Exists(path))
{
    byte[] raw = File.ReadAllBytes(path);
    d.SetFont(raw);                                  // a font from bytes (download, SD card, ...)
    Console.WriteLine($"from bytes: {raw.Length} B, width('abc') = {d.GetStrWidth("abc")}");
}
d.SetFont("6x10_tf");
d.DrawStr(0, 62, "font: " + d.Font);
d.SendBuffer();
