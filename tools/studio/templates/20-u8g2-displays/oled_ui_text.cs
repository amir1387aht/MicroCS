// oled_ui_text.cs - u8g2's text helpers for small user interfaces.
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin();
d.SetFont("6x10_tf");
d.ClearBuffer();
// DrawButtonUTF8(x, y, flags, width, padding h, padding v, text): x, y = baseline of the text
d.DrawButtonUTF8(4, 12, U8g2.BtnBW1, 0, 2, 2, "OK");
d.DrawButtonUTF8(34, 12, U8g2.BtnInv | U8g2.BtnBW1, 0, 2, 2, "Inv");
d.DrawButtonUTF8(70, 12, U8g2.BtnShadow1, 0, 2, 2, "Shadow");
d.DrawButtonUTF8(64, 30, U8g2.BtnHCenter | U8g2.BtnBW2, 100, 1, 1, "centred, 100 px");
// DrawUTF8Line(x, y, width, text, border, inverted): a full-width bar
d.DrawUTF8Line(0, 34, 128, "Status: ready", 1, 1);
// DrawUTF8Lines(x, y, width, line height, text): one centred line per \n, returns the height used
int h = d.DrawUTF8Lines(0, 46, 128, 10, "centred line one\nand line two");
Console.WriteLine($"text block height {h} px");
d.SendBuffer();
