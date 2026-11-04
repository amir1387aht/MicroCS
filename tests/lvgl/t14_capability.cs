// args: --lvgl=320x240 --sim
// Hal.Has("Lvgl") tells Studio and scripts that the LVGL binding is in the firmware
Console.WriteLine(Hal.Has("Lvgl"));
Console.WriteLine(lv_version_major() >= 8);
