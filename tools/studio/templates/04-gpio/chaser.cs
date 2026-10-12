// LEDs on GPIO 4..9 light up one after another, back and forth.
var leds = new List<Pin>();
for (int p = 4; p <= 9; p++) leds.Add(new Pin(p, GPIO.Output));

for (int round = 0; round < 5; round++)
{
    for (int i = 0; i < leds.Count; i++) { leds[i].High(); Thread.Sleep(60); leds[i].Low(); }
    for (int i = leds.Count - 2; i > 0; i--) { leds[i].High(); Thread.Sleep(60); leds[i].Low(); }
}
