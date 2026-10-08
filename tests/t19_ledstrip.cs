// args: --sim --sim-log
// LedStrip: addressable WS2812/SK6812 strips on any pin
var strip = new LedStrip("GP16", 4);
strip[0] = 0xFF0000;
strip.SetPixel(1, 0, 255, 0);
strip.Fill(LedStrip.Hsv(240, 255, 255), 2, 2);
strip.Brightness = 128;
Console.WriteLine($"{strip.Count} {strip.Pin} {strip[0]:X6} {strip.GetPixel(1):X6} {strip[3]:X6} {strip.Brightness} {LedStrip.Rgb(1,2,3):X6} {LedStrip.Hsv(120):X6}");
strip.Show();
var w = new LedStrip(5, 2, LedStrip.GRBW);
w.SetPixel(0, 10, 20, 30, 40); Console.WriteLine($"{w[0]:X8}"); w.Show();
try { strip[4] = 1; } catch (IndexOutOfRangeException e) { Console.WriteLine(e.Message); }
Console.WriteLine(Hal.Has("LedStrip"));
strip.Clear(); Console.WriteLine($"{strip[0]:X6} {strip[2]:X6}");
var c = 0; for (var i = 0; i < 360; i += 60) c ^= LedStrip.Hsv(i); Console.WriteLine($"{c:X6}");
strip.Dispose();
