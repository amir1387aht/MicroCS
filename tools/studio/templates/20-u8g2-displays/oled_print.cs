// oled_print.cs - print values at a cursor: U8g2 positions are pixels (baseline), U8x8 tiles.
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x64_noname");
d.Begin();
d.SetFont("6x10_tf");
for (int i = 0; i < 10; i++)
{
    d.ClearBuffer();
    d.Home();                                        // cursor to (0, 0)
    d.SetCursor(0, 10);
    d.Print("Count: ");
    d.Println(i);                                    // numbers, bools, strings ...
    d.Println($"uptime {Environment.TickCount / 1000.0:F1} s");
    d.Print("temp ");
    d.Print(21.5 + i * 0.1);
    d.Println(" C");
    d.Print($"cursor at {d.CursorX},{d.CursorY}");
    d.SendBuffer();
    Thread.Sleep(100);
}
