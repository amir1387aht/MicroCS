// Review the firmware's TFT_eSPI controller/pins/voltage before using hardware.
if (!Hal.Has("TFT_eSPI"))
{
    Console.WriteLine("This firmware lacks TFT_eSPI. Enable the optional Arduino driver and reviewed TFT setup.");
    return;
}
var display = new Tft();
try
{
    display.Begin();
    display.Fill(0x0000);
    display.DrawRect(8, 8, 80, 40, 0xF800);
    display.FillCircle(110, 30, 15, 0x07E0);
    display.TextColor(0xFFFF, 0x0000);
    display.TextSize(1);
    display.DrawText("MicroCS", 8, 60, 1); // LOAD_GLCD must be enabled in TFT setup.
    Console.WriteLine("TFT drawing calls finished; inspect the physical display.");
}
finally
{
    display.Dispose();
}
