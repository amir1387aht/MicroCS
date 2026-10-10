// U8g2 drawing primitives and a small animation (full frame buffer: 1 KB of RAM for 128x64).
//   ./build/mcs_u8g2 --oled examples/u8g2/graphics.cs
var g = new U8g2("ssd1306_i2c_128x64_noname");
g.Begin();
g.SetFont("5x7_tr");

// 1. shapes
g.ClearBuffer();
g.DrawFrame(0, 0, 40, 30);  g.DrawBox(44, 0, 40, 30);  g.DrawRFrame(88, 0, 40, 30, 6);
g.DrawCircle(20, 46, 16);   g.DrawDisc(64, 46, 16);     g.DrawEllipse(106, 46, 20, 10);
g.DrawTriangle(48, 62, 64, 34, 80, 62);
g.SetDrawColor(U8g2.ColorXor);
g.DrawBox(30, 20, 70, 20);                     // XOR: inverts what is below
g.SetDrawColor(U8g2.ColorSet);
g.SendBuffer();
Thread.Sleep(1000);

// 2. lines, arcs, a polygon, a bitmap (XBM: 1 bit per pixel, LSB first)
g.ClearBuffer();
for (int x = 0; x < 128; x += 8) g.DrawLine(64, 63, x, 0);
g.DrawArc(20, 40, 18, 0, 128);
g.DrawPolygon(new int[] { 90, 40, 120, 45, 110, 62, 85, 60 });
byte[] heart = { 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00 };
g.DrawXBM(4, 4, 8, 8, heart);
g.SendBuffer();
Thread.Sleep(1000);

// 3. animation: a bouncing ball, 30 frames
int bx = 10, by = 10, dx = 3, dy = 2;
for (int i = 0; i < 30; i++)
{
    g.ClearBuffer();
    g.DrawFrame(0, 0, 128, 64);
    g.DrawDisc(bx, by, 4);
    g.DrawStr(4, 10, $"frame {i}");
    g.SendBuffer();
    bx += dx; by += dy;
    if (bx < 5 || bx > 122) dx = -dx;
    if (by < 5 || by > 58) dy = -dy;
    Thread.Sleep(30);
}
