// Optional real TFT_eSPI RAM sprites; no display transfer or hardware Begin.
if (!Hal.Has("TFT.SpriteScroll"))
{
    Console.WriteLine("Enable the TFT_eSPI module with sprite scrolling support.");
    return;
}
var display = new Tft();
var sprite = new TftSprite(display, 9, 7, 4);
try
{
    sprite.Fill(0);
    sprite.DrawPixel(2, 2, 3);
    sprite.ScrollRect(1, 1, 7, 5, 1, 1, 0);
    if (sprite.ReadValue(3, 3) != 3)
        throw new Exception("Unexpected shifted palette index");
    Console.WriteLine(sprite.ReadValue(3, 3)); // 3: shifted palette index.
}
finally
{
    sprite.Dispose();
    display.Dispose();
}
