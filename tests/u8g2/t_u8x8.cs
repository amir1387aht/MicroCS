// args: --oled=sh1106
// U8x8: text only, no frame buffer (8x8 tiles straight to the display), on a simulated SH1106
var x = new U8x8("U8X8_SH1106_128X64_NONAME_HW_I2C");   // Arduino class names work too
Console.WriteLine($"{x.Display} {x.Cols}x{x.Rows} font={x.Font}");
x.Begin();
x.SetFont("chroma48medium8_r");
x.DrawString(0, 0, "U8x8 on SH1106");
x.Draw2x2String(0, 2, "BIG");
x.SetFont("8x13_1x2_r");
x.DrawString(8, 2, "1x2");
x.SetFont("pxplusibmcga_f");                // /fonts/u8x8_font_pxplusibmcga_f.bin
x.SetInverseFont(true);
x.DrawString(0, 5, " inverse ");
x.SetInverseFont(false);
x.DrawUTF8(10, 5, "ÄÖÜ");
x.DrawTile(15, 7, 1, new byte[] { 0xFF, 0x81, 0xBD, 0xA5, 0xA5, 0xBD, 0x81, 0xFF });
x.SetCursor(0, 6);
x.Print("n=");
x.Print(42);
x.Println("");
Console.WriteLine("cursor " + x.CursorX + "," + x.CursorY + " utf8len=" + x.GetUTF8Len("ÄÖÜ"));
x.SetPowerSave(true);
x.SetPowerSave(false);
x.SetContrast(128);
x.ClearLine(0);
x.DrawString(0, 0, "line 0 again");
