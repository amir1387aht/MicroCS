// Actual LVGL pointer hit testing without pretending an external touch controller exists.
if (!Hal.Has("LVGL"))
{
    Console.WriteLine("This firmware lacks LVGL. Enable the optional real LVGL8.4/9.4 build.");
    return;
}
var gui = new LvGui(160, 80);
try
{
    var button = gui.Screen.Create("button");
    button.SetPos(0, 0);
    button.SetSize(100, 50);
    var label = button.Create("label");
    label.Text = "Click";
    gui.Pump(40);
    gui.Input(40, 25, true);
    gui.Pump(40);
    gui.Input(40, 25, false);
    gui.Pump(40);
    bool clicked = false;
    for (int i = 0; i < 32; i++)
    {
        var item = gui.PollEvent();
        if (item == null)
            break;
        if (item[1] == LvGui.Clicked)
            clicked = true;
    }
    if (!clicked)
        throw new Exception("LVGL did not report the expected button click.");
    Console.WriteLine("LVGL button click received.");
}
finally
{
    gui.Dispose();
}
