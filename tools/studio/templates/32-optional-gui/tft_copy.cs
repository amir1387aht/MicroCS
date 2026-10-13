// Real RAM sprites only. Copy preserves the target's palette/bitmap mapping.
if (!Hal.Has("TFT.SpriteCopy") || !Hal.Has("TFT.Image"))
{
    Console.WriteLine("Update firmware with optional TFT image and sprite-copy APIs.");
    return;
}
var display = new Tft();
TftSprite source = null;
TftSprite target = null;
try
{
    source = new TftSprite(display, 3, 2);
    target = new TftSprite(display, 5, 4, 8);
    source.DrawImage(0, 0, 3, 2, new int[] { 0xF800, 0x07E0, 0x001F, 0, 0xFFFF, 0xFFE0 });
    target.Fill(0);
    source.CopyTo(target, 1, 1); // Native RGB565->RGB332 quantization.
    Console.WriteLine($"Copied RAM pixel: {target.ReadPixel(3, 2)}");
    // Whole source must fit, <=4096pixels, source and target must differ.
    // Supported depths:16->16/8,8->8,4->4,1->1; no clipping or physical IO.
}
finally
{
    if (target != null)
        target.Dispose();
    if (source != null)
        source.Dispose();
    display.Dispose();
}
