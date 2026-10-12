// oled_animation.cs - redraw the whole buffer every frame; the display only gets SendBuffer.
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x64_noname");
d.SetBusClock(400_000);                               // fast I2C: more frames per second
d.Begin();
d.SetFont("5x7_tr");
int bx = 20, by = 20, dx = 3, dy = 2, frames = 0;
var sw = Stopwatch.StartNew();
for (int i = 0; i < 60; i++)
{
    d.ClearBuffer();
    d.DrawFrame(0, 0, 128, 64);
    d.DrawDisc(bx, by, 5);
    d.DrawStr(4, 10, $"{frames * 1000 / Math.Max(1, sw.ElapsedMilliseconds)} fps");
    d.SendBuffer();
    frames++;
    bx += dx; by += dy;
    if (bx < 7 || bx > 120) dx = -dx;
    if (by < 7 || by > 56) dy = -dy;
}
Console.WriteLine($"{frames} frames in {sw.ElapsedMilliseconds} ms");

// partial update: only the tiles (8x8 blocks) that changed - here the top-left 4x2 tiles
d.SetDrawColor(0); d.DrawBox(1, 1, 31, 15); d.SetDrawColor(1);
d.DrawStr(4, 10, "done");
d.UpdateDisplayArea(0, 0, 4, 2);                      // tile x, tile y, tiles wide, tiles high
