// oled_log.cs - u8log keeps the last lines of text and draws them like a terminal.
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin();
d.SetFont("5x7_tr");

// 1. automatic: the whole display shows the log and redraws on every newline
d.LogBegin(25, 8);                                   // 25 columns x 8 lines = 200 bytes
for (int i = 1; i <= 10; i++)
{
    d.Log($"#{i,2} A0 = {ADC.ReadMillivolts(0),4} mV\n");
    Thread.Sleep(80);
}
d.LogLine("done.");

// 2. manual: the log is one part of your screen
d.LogBegin(20, 5, false);                            // false = collect only, draw with DrawLog
d.LogLineHeightOffset = 1;                           // 1 px more between lines
for (int i = 1; i <= 8; i++)
{
    d.LogLine($"event {i}");
    d.ClearBuffer();
    d.DrawStr(0, 7, "Events");
    d.DrawHLine(0, 9, 128);
    d.DrawLog(0, 18);                                // x, y of the first baseline
    d.DrawFrame(104, 12, 22, 50);
    d.DrawBox(106, 60 - i * 6, 18, i * 6);
    d.SendBuffer();
    Thread.Sleep(80);
}
