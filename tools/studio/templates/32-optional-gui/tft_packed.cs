// Native RAM operations only: no Begin, physical readback or transfer success claim.
if (!Hal.Has("TFT.Palette") || !Hal.Has("TFT.Bitmap"))
{
    Console.WriteLine("This firmware lacks native TFT palette/bitmap support; update its optional driver.");
    return;
}
var display = new Tft();
TftSprite palette = null;
TftSprite bitmap = null;
try
{
    palette = new TftSprite(display, 3, 5, 4);
    palette.SetPaletteColor(3, 0xF800);
    palette.Fill(0);
    palette.DrawPixel(2, 4, 3); // Palette index, not RGB565.
    Console.WriteLine($"Palette RAM index {palette.ReadValue(2, 4)}, RGB565 {palette.ReadPixel(2, 4)}");
    bitmap = new TftSprite(display, 9, 3, 1);
    bitmap.BitmapColors(0x07E0, 0xF800); // Parent-wide mapping for every1bit sprite.
    bitmap.Fill(0);
    bitmap.DrawPixel(8, 2, 1); // Bit index, not RGB565.
    Console.WriteLine($"Bitmap RAM bit {bitmap.ReadValue(8, 2)}, RGB565 {bitmap.ReadPixel(8, 2)}");
    Console.WriteLine($"Reserved native pixel+palette requests: {display.SpriteBytes} bytes");
}
finally
{
    if (bitmap != null)
        bitmap.Dispose();
    if (palette != null)
        palette.Dispose();
    display.Dispose();
}
