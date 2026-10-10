// args: --oled
// U8g2 on the simulated SSD1306 (I2C 0x3C): names, full-buffer drawing, metrics, buffer export, errors
Console.WriteLine(string.Join(", ", U8g2.Displays));
Console.WriteLine(string.Join(", ", U8g2.Fonts));
var d = new U8g2("ssd1306_128x64_noname");          // I2C bus: finds the _i2c_ variant
Console.WriteLine($"{d.Display} {d.Bus} {d.Width}x{d.Height} {d.Cols}x{d.Rows} buf={d.BufferSize} addr=0x{d.I2CAddress:X2} font={d.Font}");
d.Begin();
d.ClearBuffer();
d.SetFont("helvB10_tr");
d.DrawStr(0, 12, "Hello MicroCS");
d.DrawFrame(0, 16, 128, 48);
d.DrawCircle(24, 40, 14);
d.DrawDisc(60, 40, 9);
d.DrawRBox(80, 24, 40, 32, 5);
d.SetDrawColor(U8g2.ColorClear);
d.DrawBox(90, 34, 20, 12);
d.SetDrawColor(U8g2.ColorSet);
d.DrawTriangle(4, 60, 14, 50, 24, 60);
d.SendBuffer();
Console.WriteLine($"width={d.GetStrWidth("Hello MicroCS")} ascent={d.Ascent} descent={d.Descent} maxh={d.MaxCharHeight}");
Console.WriteLine($"pixels: {d.GetPixel(0, 16)} {d.GetPixel(1, 17)} {d.GetPixel(60, 40)} {d.GetPixel(100, 40)}");
string pbm = d.WriteBufferPBM();
Console.WriteLine(pbm.Substring(0, 9).Replace("\n", "|") + " len=" + pbm.Length);
Console.WriteLine(d.WriteBufferXBM().Split('\n')[0]);
var lines = d.Dump().Split('\n');
Console.WriteLine("dump rows=" + lines.Length);
// a partial update of 2 tiles
d.SetFont("5x7_tr");
d.SetDrawColor(U8g2.ColorXor);
d.DrawBox(0, 0, 16, 8);
d.UpdateDisplayArea(0, 0, 2, 1);
d.SetDrawColor(1);
try { new U8g2("ssd1306_i2c_128x64_noname", 0, 0x3D).Begin(); } catch (IOException e) { Console.WriteLine(e.Message); }
try { new U8g2("st7920_128x64"); } catch (ArgumentException e) { Console.WriteLine(e.Message); }
try { d.SetFont("nope_tf"); } catch (ArgumentException e) { Console.WriteLine(e.Message); }
Console.WriteLine(U8g2.SPI("ssd1306_i2c_128x64_noname", 0, 5, 6).Display);   // SPI bus: the SPI variant
var spi = U8g2.SPI("ssd1306_128x64_noname", 0, 5, 6, 7);
Console.WriteLine($"{spi.Display} {spi.Bus}");
spi.Begin(); spi.ClearBuffer(); spi.DrawBox(0, 0, 8, 8); spi.SendBuffer();
spi.Dispose();
try { spi.Clear(); } catch (ObjectDisposedException) { Console.WriteLine("disposed"); }
