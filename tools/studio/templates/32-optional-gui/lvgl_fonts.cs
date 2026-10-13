// Static linked fonts only. Availability is configuration/version dependent.
if (!Hal.Has("LVGL.Font"))
{
    Console.WriteLine("Update firmware with optional LVGL static-font support.");
    return;
}
var gui = new LvGui(160, 80);
try
{
    var label = gui.Screen.Create("label");
    label.SetFont("Default");
    if (LvGui.HasFont("lv_font_montserrat_14"))
        label.SetFont("lv_font_montserrat_14");
    label.Text = $"Static native font height: {label.FontHeight}";
    label.SetSize(150, 60);
    for (int i = 0; i < 4; i++)
    {
        gui.Pump(20);
        Thread.Sleep(20);
    }
}
finally
{
    gui.Dispose();
}
