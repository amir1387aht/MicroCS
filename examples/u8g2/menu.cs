// The u8g2 menus with three buttons to GND (inputs with pull-ups): SELECT, NEXT, PREV.
// u8g2 reads the pins itself while a menu is open (Begin(select, next, prev[, up, down, home])).
// On the PC nobody presses buttons, so the simulator run just shows the first menu until the
// time limit:   ./build/mcs_u8g2 --oled --time-limit 2000 examples/u8g2/menu.cs
// Buttons from somewhere else (a rotary encoder, a remote): d.SetMenuInput(() => U8g2.EventNext ...)
var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin(2, 3, 4);                             // GPIO 2 = select, 3 = next, 4 = prev
d.SetFont("6x10_tf");

int brightness = 5;
while (true)
{
    int choice = d.UserInterfaceSelectionList("Settings", 1, "Brightness\nAbout\nReset");
    if (choice == 1)
        brightness = d.UserInterfaceInputValue("Brightness", "", brightness, 0, 9, 1, "");
    else if (choice == 2)
        d.UserInterfaceMessage("MicroCS", "C# for MCUs", "with u8g2", " OK ");
    else if (choice == 3 && d.UserInterfaceMessage("Reset?", "", "", " Yes \n No ") == 1)
        brightness = 5;
    d.SetContrast(brightness * 28);
}
