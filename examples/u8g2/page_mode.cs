// Page mode: a "_1" display keeps only one 8-pixel page (128 bytes for 128x64) instead of the
// whole frame (1 KB) - the drawing code runs once per page. Good for MCUs with little RAM.
//   ./build/mcs_u8g2 --oled examples/u8g2/page_mode.cs
var d = new U8g2("ssd1306_i2c_128x64_noname_1");          // _1: 1 page, _2: 2 pages, _f: full
Console.WriteLine($"frame buffer: {d.BufferSize} bytes");
d.Begin();
d.SetFont("ncenB14_tr");

for (int n = 3; n >= 0; n--)
{
    d.Draw(() =>                                           // = FirstPage(); do { ... } while (NextPage());
    {
        d.DrawStr(0, 20, "Countdown");
        d.DrawStr(56, 50, n.ToString());
        d.DrawRFrame(40, 28, 48, 30, 6);
    });
    Thread.Sleep(500);
}

// the same with the explicit loop, rotated by 180 degrees
d.SetDisplayRotation(U8g2.R2);
d.FirstPage();
do
{
    d.DrawStr(0, 20, "upside");
    d.DrawStr(0, 44, "down");
} while (d.NextPage());
