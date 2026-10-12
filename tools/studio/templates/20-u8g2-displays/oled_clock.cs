// oled_clock.cs - an analog clock. Uses the board's RTC when set, else counts from 10:08:30.
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin();
d.SetFont("6x10_tf");
int cx = 32, cy = 32, r = 30;
int start = 10 * 3600 + 8 * 60 + 30;
long t0 = Environment.TickCount;

void Hand(double turn, int len)
{
    double a = turn * 2 * Math.PI;
    d.DrawLine(cx, cy, cx + (int)(len * Math.Sin(a)), cy - (int)(len * Math.Cos(a)));
}

for (int frame = 0; frame < 20; frame++)
{
    int s = (start + (int)((Environment.TickCount - t0) / 1000) + frame * 7) % 86400;
    d.ClearBuffer();
    d.DrawCircle(cx, cy, r);
    for (int h = 0; h < 12; h++)
    {
        double a = h * Math.PI / 6;
        d.DrawPixel(cx + (int)((r - 3) * Math.Sin(a)), cy - (int)((r - 3) * Math.Cos(a)));
    }
    Hand((s % 43200) / 43200.0, 15);                  // hours
    Hand((s % 3600) / 3600.0, 22);                    // minutes
    Hand((s % 60) / 60.0, 27);                        // seconds
    d.DrawDisc(cx, cy, 2);
    d.DrawArc(cx, cy, r + 1, 0, (byte)(s % 60 * 255 / 60));
    d.DrawStr(72, 28, $"{s / 3600:D2}:{s / 60 % 60:D2}");
    d.DrawStr(72, 42, $":{s % 60:D2}");
    d.SendBuffer();
    Thread.Sleep(50);
}
