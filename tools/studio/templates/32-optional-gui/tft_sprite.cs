// Review TFT_eSPI controller/pins/voltage before Begin or Push.
if (!Hal.Has("TFT.Sprite"))
{
    Console.WriteLine("This firmware lacks optional native TFT sprite support.");
    return;
}
var display = new Tft();
TftSprite sprite = null;
try
{
    sprite = new TftSprite(display, 32, 16);
    sprite.Fill(0x001F);
    sprite.DrawPixel(0, 0, 0xF800);
    Console.WriteLine($"RAM pixel: {sprite.ReadPixel(0, 0)}");
    display.Begin();
    sprite.Push(10, 10);
    sprite.PushTransparent(10, 30, 0x001F);
    Console.WriteLine("Push calls finished; inspect the physical display.");
}
finally
{
    if (sprite != null)
        sprite.Dispose();
    display.Dispose();
}
