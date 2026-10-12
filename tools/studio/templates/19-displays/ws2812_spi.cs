// WS2812 / WS2812B / SK6812 ("NeoPixel") LEDs through the SPI bus. Each LED bit becomes
// 3 SPI bits at 2.4 MHz (1 -> 110, 0 -> 100), which gives the 0.4 / 0.8 us pulses the LEDs
// expect, so timing is done by the SPI hardware, not by the script.
// Wiring: strip DIN -> MOSI of SPI bus 0 (Pico: GP19), GND -> GND, 5 V -> a supply that can
// feed the LEDs (up to 60 mA per LED at full white). A 330 ohm resistor in the data line helps.
// One SPI.Write sends at most 256 bytes = 28 LEDs; build the firmware with
// -DMCS_HAL_MAX_XFER=1024 for up to 113 LEDs.
const int BUS = 0;
const int COUNT = 8;                  // number of LEDs
const int BRIGHTNESS = 40;            // 0..255 (keeps current and eyes safe)

SPI.Open(BUS, 2_400_000, 0);
var frame = new byte[1 + COUNT * 9];  // 9 SPI bytes per LED; the leading 0 keeps DIN low first
var pixels = new int[COUNT];          // colours as 0xRRGGBB

int Rgb(int r, int g, int b) => (r << 16) | (g << 8) | b;

void Encode(int at, int value)        // one colour byte -> 24 SPI bits (3 bytes)
{
    int bits = 0;
    for (int i = 7; i >= 0; i--) bits = (bits << 3) | (((value >> i) & 1) == 1 ? 6 : 4);
    frame[at] = (byte)((bits >> 16) & 0xFF);
    frame[at + 1] = (byte)((bits >> 8) & 0xFF);
    frame[at + 2] = (byte)(bits & 0xFF);
}

void Show()
{
    for (int i = 0; i < COUNT; i++)
    {
        int c = pixels[i];
        int r = ((c >> 16) & 0xFF) * BRIGHTNESS / 255;
        int g = ((c >> 8) & 0xFF) * BRIGHTNESS / 255;
        int b = (c & 0xFF) * BRIGHTNESS / 255;
        int at = 1 + i * 9;
        Encode(at, g); Encode(at + 3, r); Encode(at + 6, b);   // WS2812 wants G, R, B
    }
    SPI.Write(BUS, frame);
    Thread.Sleep(1);                  // line low > 280 us = latch the new colours
}

void Fill(int color) { for (int i = 0; i < COUNT; i++) pixels[i] = color; }

int Wheel(int pos)                    // 0..255 -> colour around the rainbow
{
    pos &= 255;
    if (pos < 85) return Rgb(255 - pos * 3, pos * 3, 0);
    if (pos < 170) { pos -= 85; return Rgb(0, 255 - pos * 3, pos * 3); }
    pos -= 170;
    return Rgb(pos * 3, 0, 255 - pos * 3);
}

// 1. colour wipe: red, green, blue
foreach (int color in new[] { Rgb(255, 0, 0), Rgb(0, 255, 0), Rgb(0, 0, 255) })
{
    for (int i = 0; i < COUNT; i++) { pixels[i] = color; Show(); Thread.Sleep(50); }
}

// 2. rainbow running along the strip
for (int step = 0; step < 256; step += 4)
{
    for (int i = 0; i < COUNT; i++) pixels[i] = Wheel(i * 256 / COUNT + step);
    Show();
    Thread.Sleep(20);
}

// 3. theater chase in warm white
for (int round = 0; round < 15; round++)
{
    for (int i = 0; i < COUNT; i++) pixels[i] = i % 3 == round % 3 ? Rgb(255, 160, 60) : 0;
    Show();
    Thread.Sleep(100);
}

Fill(0);                              // all off
Show();
Console.WriteLine($"done: {COUNT} LEDs, {frame.Length} SPI bytes per frame");
