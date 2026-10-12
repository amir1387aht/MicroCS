// oled_menu.cs - u8g2's built-in menus. Real buttons: Begin(select, next, prev[, up, down, home]),
// pins to GND (u8g2 enables the pull-ups). Here SetMenuInput feeds key presses from code so the
// menus also run without buttons - remove it and press the buttons on your board.
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin(2, 3, 4);                                    // GPIO 2 = select, 3 = next, 4 = prev
d.SetFont("6x10_tf");

int[] keys = { U8g2.EventSelect,                    // menu: "Brightness" (the first entry)
               U8g2.EventNext, U8g2.EventNext, U8g2.EventSelect,   // value 5 -> 7
               U8g2.EventNext, U8g2.EventNext, U8g2.EventSelect,   // menu: "Reset"
               U8g2.EventNext, U8g2.EventSelect,     // message: "No"
               U8g2.EventHome };                     // menu: leave
int k = 0;
d.SetMenuInput(() => { Thread.Sleep(50); return k < keys.Length ? keys[k++] : U8g2.EventHome; });

int brightness = 5;
while (true)
{
    int choice = d.UserInterfaceSelectionList("Settings", 1, "Brightness\nAbout\nReset");   // 1-based, 0 = home
    if (choice == 0) break;
    if (choice == 1)
        brightness = d.UserInterfaceInputValue("Brightness", "level ", brightness, 0, 9, 1, "");
    else if (choice == 2)
        d.UserInterfaceMessage("MicroCS", "C# for MCUs", "with u8g2", " OK ");
    else if (d.UserInterfaceMessage("Reset?", "all settings", "", " Yes \n No ") == 1)
        brightness = 5;
    d.SetContrast(brightness * 28);
    Console.WriteLine($"choice {choice}, brightness {brightness}");
}
d.SetMenuInput(null);                                // back to the button pins
d.ClearBuffer();
d.DrawStr(0, 12, $"brightness = {brightness}");
d.SendBuffer();
