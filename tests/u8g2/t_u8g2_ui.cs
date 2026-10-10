// args: --oled
// the u8g2 user interface (selection list, message, number input) driven by SetMenuInput, and u8log
var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin();
d.SetFont("6x10_tf");
int[] keys = { U8g2.EventNext, U8g2.EventNext, U8g2.EventSelect,       // list -> "Gamma"
               U8g2.EventNext, U8g2.EventSelect,                        // message -> 2nd button
               U8g2.EventNext, U8g2.EventNext, U8g2.EventNext, U8g2.EventSelect };   // value 5 -> 8
int k = 0;
d.SetMenuInput(() => k < keys.Length ? keys[k++] : U8g2.EventNone);
Console.WriteLine("list -> " + d.UserInterfaceSelectionList("Menu", 1, "Alpha\nBeta\nGamma\nDelta"));
Console.WriteLine("message -> " + d.UserInterfaceMessage("Save?", "settings", "changed", " Yes \n No "));
Console.WriteLine("value -> " + d.UserInterfaceInputValue("Volume", "", 5, 0, 10, 2, " %"));
k = keys.Length;
Console.WriteLine("event -> " + d.GetMenuEvent());
d.SetMenuInput(null);
// u8log: a scrolling text terminal in a small character buffer
d.SetFont("5x7_tr");
d.LogBegin(24, 7, false);                   // false: drawn by DrawLog only (true: redraws the screen itself)
for (int i = 1; i <= 9; i++) d.Log($"reading {i}: {i * 7 % 10}.{i}\n");
d.LogLine("done");
d.ClearBuffer();
d.DrawLog(0, 8);
d.SendBuffer();
// auto redraw: the whole screen shows the log after every line
d.LogBegin(21, 8);
d.LogRedrawOnNewline = true;
d.SetFont("5x7_tr");
d.Log("auto 1\nauto 2\n");
