// Dedicated real-library fixture; does not require a HAL module.
if (!LvGui.HasFont("Default") || LvGui.HasFont("not_a_font"))
    throw new Exception("configuration probe");
var gui = new LvGui(160, 80);
try
{
    var label = gui.Screen.Create("label");
    label.SetFont("Default");
    label.Text = "Actual static font";
    label.SetSize(150, 60);
    if (label.FontHeight <= 0)
        throw new Exception("native height");
    GC.Collect();
    gui.Pump(40);
    try { label.SetFont("not_a_font"); throw new Exception("unknown accepted"); }
    catch (NotSupportedException e) {}
    if (LvGui.HasFont("lv_font_montserrat_14"))
        label.SetFont("lv_font_montserrat_14");
    gui.Pump(40);
}
finally
{
    gui.Dispose();
}
Console.WriteLine("static fonts ok");
