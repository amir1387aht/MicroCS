// Real LVGL rendering; default sink is headless unless firmware installs a backend.
if (!Hal.Has("LVGL"))
{
    Console.WriteLine("This firmware lacks LVGL. Enable the optional real LVGL8.4/9.4 build.");
    return;
}
var gui = new LvGui(160, 80);
try
{
    var label = gui.Screen.Create("label");
    label.Text = "MicroCS + LVGL";
    label.SetPos(8, 8);
    gui.Screen.SetStyle("bg_color", 0x202020);
    label.SetStyle("text_color", 0xFFFFFF);
    for (int frame = 0; frame < 5; frame++)
        gui.Pump(20);
    Console.WriteLine("LVGL label rendered; physical display depends on the installed backend.");
}
finally
{
    gui.Dispose();
}
