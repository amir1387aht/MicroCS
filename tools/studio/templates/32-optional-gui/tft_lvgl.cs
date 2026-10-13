// Real synchronous backend; native RGB565, no retained buffer and no DMA.
if (!Hal.Has("TFT") || !Hal.Has("LVGL"))
{
    Console.WriteLine("This firmware needs both optional TFT_eSPI and LVGL capabilities.");
    return;
}
var display = new Tft();
LvGui gui = null;
try
{
    display.Begin();
    display.AttachLVGL();
    gui = new LvGui(display.Width, display.Height);
    var label = gui.Screen.Create("label");
    label.Text = "MicroCS + TFT + LVGL";
    label.SetPos(8, 8);
    for (int frame = 0; frame < 5; frame++)
        gui.Pump(20);
    Console.WriteLine("LVGL TFT drawing calls finished; inspect the physical display.");
}
finally
{
    if (gui != null)
        gui.Dispose();
    display.Dispose(); // includes detach after GUI teardown
}
