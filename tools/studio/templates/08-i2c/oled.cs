// SSD1306 128x64 OLED at 0x3C.
I2C.Open(0, 400000);
var oled = new I2cDevice(0, 0x3C);
void Cmd(params int[] c) { foreach (int x in c) oled.Write(new byte[] { 0x00, (byte)x }); }

Cmd(0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8,
    0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF);
Cmd(0x21, 0, 127, 0x22, 0, 7);                         // whole screen
for (int page = 0; page < 8; page++)
{
    var row = new byte[129];
    row[0] = 0x40;                                     // data
    for (int x = 0; x < 128; x++) row[x + 1] = (byte)(((x + page) & 8) != 0 ? 0xAA : 0x55);
    oled.Write(row);
}
Console.WriteLine("pattern drawn");
