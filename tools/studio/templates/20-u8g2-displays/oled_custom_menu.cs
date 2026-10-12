// oled_custom_menu.cs - a scrolling menu drawn by you. Keys come from GetMenuEvent(): the
// Begin() button pins, or a SetMenuInput callback (here: an encoder simulated by a counter).
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin();
d.SetFont("6x10_tf");
string[] items = { "Start", "Speed", "Direction", "Light", "Sound", "Info" };

int ticks = 0;                                       // replace with your encoder / button code
d.SetMenuInput(() => ++ticks % 4 == 0 ? (ticks > 30 ? U8g2.EventSelect : U8g2.EventNext) : U8g2.EventNone);

int sel = 0, top = 0;
while (true)
{
    int ev = d.GetMenuEvent();
    if (ev == U8g2.EventNext || ev == U8g2.EventDown) sel = (sel + 1) % items.Length;
    if (ev == U8g2.EventPrev || ev == U8g2.EventUp) sel = (sel + items.Length - 1) % items.Length;
    if (ev == U8g2.EventSelect) break;
    if (sel < top) top = sel;
    if (sel > top + 3) top = sel - 3;

    d.ClearBuffer();
    d.DrawStr(0, 9, "Menu");
    d.DrawHLine(0, 11, 128);
    for (int i = 0; i < 4 && top + i < items.Length; i++)
    {
        int y = 24 + i * 12;
        if (top + i == sel) { d.DrawBox(0, y - 9, 120, 11); d.SetDrawColor(0); }
        d.DrawStr(4, y, items[top + i]);
        d.SetDrawColor(1);
    }
    d.DrawVLine(124, 14 + sel * 48 / items.Length, 8);  // scroll bar
    d.SendBuffer();
    Thread.Sleep(20);
}
Console.WriteLine("selected: " + items[sel]);
