// oled_dashboard.cs - big numbers (logisoso24_tn), a bar and a scrolling plot.
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin();
int[] hist = new int[64];
for (int n = 0; n < 60; n++)
{
    int mv = ADC.ReadMillivolts(0);
    if (mv <= 0) mv = 1650 + (int)(1200 * Math.Sin(n / 5.0));    // no signal: demo values
    Array.Copy(hist, 1, hist, 0, hist.Length - 1);
    hist[hist.Length - 1] = mv;

    d.ClearBuffer();
    d.SetFont("logisoso24_tn");
    d.DrawStr(0, 26, $"{mv / 1000.0:F2}");
    d.SetFont("5x7_tr");
    d.DrawStr(0, 36, "volts on A0");
    d.DrawFrame(0, 42, 60, 10);                       // bar gauge 0..3.3 V
    d.DrawBox(2, 44, Math.Min(56, mv * 56 / 3300), 6);
    d.DrawFrame(63, 0, 65, 64);                       // graph
    for (int x = 1; x < hist.Length; x++)
        d.DrawLine(63 + x, 62 - hist[x - 1] * 60 / 3300, 64 + x, 62 - hist[x] * 60 / 3300);
    d.SendBuffer();
    Thread.Sleep(40);
}
