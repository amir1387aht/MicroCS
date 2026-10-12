// WS2812 / WS2812B / SK6812 ("NeoPixel") LEDs on ANY GPIO with the native LedStrip class.
// The bit timing is done by the firmware: RP2040/RP2350 PIO, ESP32 RMT, a tight loop on STM32,
// the led-strip driver on Zephyr. LedStrip comes from the firmware's "ws2812" driver, which
// can be left out of a build: Hal.Has("LedStrip") tells whether this firmware has it.
// On-board RGB LEDs: Waveshare RP2040-Zero / RP2040-Matrix GP16, ESP32-S3-DevKitC GPIO48
// (some v1.0 boards: 38), ESP32-C3/C6 DevKits GPIO8 - or pass "NEOPIXEL" where the port knows it.
// Wiring a strip: DIN -> PIN (330 ohm in series helps), GND -> GND, 5 V -> a supply that can
// feed the LEDs (up to 60 mA per LED at full white).
const int PIN = 16;                   // RP2040-Zero on-board LED
const int COUNT = 1;                  // number of LEDs (1 for an on-board LED)

var strip = new LedStrip(PIN, COUNT); // add LedStrip.RGB for RGB-ordered LEDs, LedStrip.GRBW for SK6812 RGBW
strip.Brightness = 100;                // 0..255, applied in Show() (keeps current and eyes safe)

// 1. colour wipe: red, green, blue
foreach (int color in new[] { LedStrip.Rgb(255, 0, 0), LedStrip.Rgb(0, 255, 0), LedStrip.Rgb(0, 0, 255) })
{
    for (int i = 0; i < COUNT; i++) { strip[i] = color; strip.Show(); Thread.Sleep(COUNT > 1 ? 50 : 400); }
}

// 2. rainbow running along the strip (hue 0..359)
for (int step = 0; step < 360; step += 5)
{
    for (int i = 0; i < COUNT; i++) strip[i] = LedStrip.Hsv((i * 360 / COUNT + step) % 360);
    strip.Show();
    Thread.Sleep(20);
}

// 3. theater chase in warm white (on a single LED: blinks)
for (int round = 0; round < 15; round++)
{
    for (int i = 0; i < COUNT; i++) strip[i] = i % 3 == round % 3 ? LedStrip.Rgb(255, 160, 60) : 0;
    strip.Show();
    Thread.Sleep(100);
}

strip.Clear();                        // all off
strip.Show();
Console.WriteLine($"done: {strip.Count} LEDs on pin {strip.Pin}");
