// oled_shapes.cs - all U8g2 shapes, one screen at a time (full frame buffer).
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var g = new U8g2("ssd1306_i2c_128x64_noname");
g.Begin();
g.SetFont("5x7_tr");

// 1. pixels and lines
g.ClearBuffer();
for (int x = 0; x < 128; x += 4) g.DrawPixel(x, 0);
g.DrawHLine(0, 4, 128);                    // x, y, length
g.DrawVLine(127, 4, 60);
g.DrawHVLine(0, 8, 40, 0);                 // direction 0 right, 1 down, 2 left, 3 up
g.DrawHVLine(0, 63, 50, 3);
for (int i = 0; i < 8; i++) g.DrawLine(10, 60, 10 + i * 14, 12);
g.DrawStr(70, 30, "lines");
g.SendBuffer();
Thread.Sleep(600);

// 2. boxes and frames (plain and rounded)
g.ClearBuffer();
g.DrawFrame(0, 0, 40, 28);
g.DrawBox(44, 0, 40, 28);
g.DrawRFrame(88, 0, 40, 28, 6);            // corner radius 6
g.DrawRBox(0, 34, 60, 28, 8);
g.SetDrawColor(0); g.DrawStr(8, 52, "RBox"); g.SetDrawColor(1);
g.DrawStr(70, 50, "boxes");
g.SendBuffer();
Thread.Sleep(600);

// 3. circles, discs and quadrants
g.ClearBuffer();
g.DrawCircle(16, 16, 14);
g.DrawDisc(50, 16, 14);
g.DrawCircle(84, 16, 14, U8g2.DrawUpperRight | U8g2.DrawLowerLeft);
g.DrawDisc(116, 16, 10, U8g2.DrawUpperLeft);
g.DrawEllipse(24, 48, 22, 12);
g.DrawFilledEllipse(76, 48, 22, 12);
g.DrawFilledEllipse(116, 48, 10, 14, U8g2.DrawLowerRight);
g.SendBuffer();
Thread.Sleep(600);

// 4. arcs, triangles, polygons
g.ClearBuffer();
g.DrawArc(20, 32, 18, 0, 64);              // angles 0..255 = a full turn (64 = 90 degrees)
g.DrawArc(20, 32, 12, 128, 255);
g.DrawTriangle(44, 60, 64, 4, 84, 60);
g.DrawPolygon(new int[] { 94, 10, 124, 20, 116, 58, 96, 50, 100, 30 });  // x, y pairs, filled
g.SendBuffer();
Console.WriteLine("4 screens drawn");
