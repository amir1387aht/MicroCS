// oled_buffer.cs - the frame buffer is yours: read pixels, save and restore screens, screenshots.
if (!Drivers.Has("u8g2")) { Console.WriteLine("This firmware has no U8g2 - see docs/U8G2.md"); return; }

var d = new U8g2("ssd1306_i2c_128x32_univision");     // 128x32: smaller dumps
d.Begin();
d.SetFont("6x10_tf");
d.ClearBuffer();
d.DrawStr(2, 12, "screenshot");
d.DrawDisc(110, 16, 10);
Console.WriteLine($"pixel (110,16) = {d.GetPixel(110, 16)}, (0,31) = {d.GetPixel(0, 31)}");

byte[] saved = d.GetBuffer();                         // a copy (BufferSize bytes)
File.WriteAllBytes("/screen.bin", saved);
Console.WriteLine($"saved {saved.Length} bytes, tiles {d.BufferTileWidth}x{d.BufferTileHeight}");

d.ClearBuffer();
d.DrawStr(2, 12, "other screen");
d.SendBuffer();
Thread.Sleep(200);

d.SetBuffer(File.ReadAllBytes("/screen.bin"));        // back to the saved picture
d.SendBuffer();
d.Dump();                                             // text art of the buffer on the console
d.WriteBufferXBM();                                   // C array, paste into DrawXBM
d.SetAutoPageClear(true);                             // page mode clears each page before drawing (default)
