// Native RAM image test, no physical display initialization or transfer claim.
if (!Hal.Has("TFT.Image"))
{
    Console.WriteLine("Update firmware with optional TFT.Image support.");
    return;
}
var display = new Tft();
TftSprite sprite = null;
try
{
    sprite = new TftSprite(display, 3, 2);
    var pixels = new int[] { 0xF800, 0x07E0, 0x001F, 0, 0xFFFF, 0xFFE0 };
    sprite.DrawImage(0, 0, 3, 2, pixels);
    Console.WriteLine($"Last RAM RGB565 pixel: {sprite.ReadPixel(2, 1)}");
    // Tft.DrawImage uses this same checked row-major format after physical Begin.
    // No decoding/files/DMA here. Image must fit the canvas and have <=4096 pixels.
}
finally
{
    if (sprite != null)
        sprite.Dispose();
    display.Dispose();
}
