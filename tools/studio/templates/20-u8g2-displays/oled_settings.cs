// oled_settings.cs - everything you can change on a running display.
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin();
d.SetFont("6x10_tf");

void Screen(string text)
{
    d.ClearBuffer();
    d.DrawFrame(0, 0, d.Width, d.Height);
    d.DrawStr(4, 14, text);
    d.DrawStr(4, 28, $"{d.Width}x{d.Height}");
    d.SendBuffer();
    Thread.Sleep(150);
}

// rotation: R0, R1 (90), R2 (180), R3 (270), Mirror, MirrorVertical - Width/Height follow
int[] rot = { U8g2.R1, U8g2.R2, U8g2.R3, U8g2.Mirror, U8g2.R0 };
foreach (int r in rot) { d.SetDisplayRotation(r); Screen($"rotation {r}"); }

d.SetFlipMode(true);  Screen("flip mode");           // 180 degrees in the display controller
d.SetFlipMode(false);

for (int c = 255; c >= 0; c -= 85) { d.SetContrast(c); Screen($"contrast {c}"); }
d.SetContrast(255);

d.Sleep();  Thread.Sleep(150);                       // display off, picture kept (SetPowerSave(true))
d.Wake();                                            // SetPowerSave(false)

// draw colours: 0 clear, 1 set, 2 XOR
d.ClearBuffer();
d.DrawBox(10, 10, 60, 30);
d.SetDrawColor(U8g2.ColorXor);
d.DrawBox(40, 20, 60, 30);                           // the overlap is inverted
d.SetDrawColor(U8g2.ColorSet);
Console.WriteLine($"draw colour {d.DrawColor}");

// clip window: drawing outside it is ignored
d.SetClipWindow(0, 50, 64, 64);
d.DrawStr(0, 62, "clipped text, clipped");
d.SetMaxClipWindow();
Console.WriteLine($"box (0,0)-(20,20) visible: {d.IsIntersection(0, 0, 20, 20)}");
d.SendBuffer();
