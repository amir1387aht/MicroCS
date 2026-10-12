// oled_text.cs - text with u8g2 fonts. U8g2.Fonts lists the fonts built into the firmware.
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin();
Console.WriteLine("fonts in flash: " + string.Join(", ", U8g2.Fonts));

d.ClearBuffer();
d.SetFont("helvB10_tr");
string title = "Centred";
int w = d.GetStrWidth(title);
d.DrawStr((d.Width - w) / 2, 12, title);           // y = baseline by default
Console.WriteLine($"{d.Font}: ascent {d.Ascent}, descent {d.Descent}, max {d.MaxCharWidth}x{d.MaxCharHeight}, width('{title}') = {w}");

d.SetFont("6x10_tf");
d.DrawUTF8(0, 26, "UTF-8: 25°C ±1 µs");             // _tf fonts have Latin-1 characters
d.DrawStrX2(0, 46, "X2");                            // double-size text
d.DrawGlyph(40, 46, 'A');                            // one character by code
Console.WriteLine($"glyph 'A' width {d.GetGlyphWidth('A')}, has 'é': {d.IsGlyph(0xE9)}, x offset {d.GetXOffsetGlyph('A')}");

d.SetFontPosTop();                                   // y = top of the text from now on
d.DrawStr(64, 30, "top");
d.SetFontPosCenter();
d.DrawStr(64, 46, "center");
d.SetFontPosBottom();
d.DrawStr(64, 63, "bottom");
d.SetFontPosBaseline();

d.SetFontDirection(3);                               // 0 = normal, 1 = down, 2 = upside down, 3 = up
d.DrawStr(124, 63, "up");
d.SetFontDirection(0);
d.SendBuffer();
Thread.Sleep(800);

// transparent text over a filled box, and a big digit font
d.ClearBuffer();
d.DrawBox(0, 0, 128, 20);
d.SetFontMode(1);                                    // 1 = transparent background
d.SetDrawColor(0);
d.DrawStr(4, 14, "inverted text");
d.SetDrawColor(1);
d.SetFontMode(0);
d.SetFont("logisoso24_tn");                          // digits only
d.DrawStr(10, 56, "12:34");
d.SetFontRefHeightText();                            // what Ascent/Descent measure (also ...All, ...ExtendedText)
d.SendBuffer();
