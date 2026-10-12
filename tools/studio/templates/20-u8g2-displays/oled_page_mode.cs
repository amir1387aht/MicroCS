// oled_page_mode.cs - "_1" keeps one 8-pixel row of the screen in RAM (128 bytes), "_2" two,
// "_f" (the default) the whole screen (1 KB). In page mode your drawing code runs once per page.
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x64_noname_1");
d.Begin();
d.SetFont("6x10_tf");
Console.WriteLine($"buffer {d.BufferSize} B, full buffer: {d.FullBuffer}, {d.BufferTileHeight} tile row(s) per page");

int pages = 0;
d.Draw(() =>                                         // runs the FirstPage / NextPage loop for you
{
    pages++;
    d.DrawStr(0, 12, "page mode");
    d.DrawFrame(0, 20, 128, 44);
    d.DrawDisc(64, 42, 16);
});
Console.WriteLine($"drawn in {pages} pages");

for (int n = 0; n <= 100; n += 25)
{
    d.Draw(g =>                                      // the display is passed in as well
    {
        g.DrawStr(0, 12, $"progress {n} %");
        g.DrawFrame(0, 24, 128, 12);
        g.DrawBox(2, 26, n * 124 / 100, 8);
    });
    Thread.Sleep(150);
}

d.FirstPage();                                       // the same loop written out
do
{
    d.DrawStr(0, 40, $"tile row {d.BufferCurrTileRow}");
} while (d.NextPage());
