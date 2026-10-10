// A scrolling log on the display (u8log): Log() prints like a terminal, the screen scrolls.
//   ./build/mcs_u8g2 --oled --sim examples/u8g2/sensor_log.cs
var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin();
d.SetFont("5x7_tr");
d.LogBegin(25, 8);                  // 25 columns x 8 lines; redraws the display on each newline
for (int i = 1; i <= 12; i++)
{
    d.Log($"#{i,2} A0 {ADC.ReadMillivolts(0),4} mV\n");
    Thread.Sleep(100);
}
d.LogLine("done.");
