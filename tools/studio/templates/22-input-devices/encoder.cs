// KY-040 / EC11: A (CLK), B (DT), optional SW; common terminal to GND.
// Change pins for your board: BOTH A and B must support GPIO interrupts.
// Arduino Uno-style interrupt mapping, for example, uses A=2 and B=3.
// Typical EC11: 4 transitions/count; use 2 or 1 if your encoder requires it.
if (!Hal.Has("RotaryEncoder"))
{
    Console.WriteLine("RotaryEncoder requires firmware with encoder-enabled GPIO interrupts.");
    return;
}
var encoder = new RotaryEncoder(32, 33, 4, 25);
try
{
    int last = encoder.Position;
    bool wasPressed = false;
    int stableButton = 0;
    for (int i = 0; i < 1000; i++) // 10 seconds; counting happens in the ISR
    {
        // Require 30 ms stable low before acting; reset once until released.
        if (encoder.ButtonPressed) { if (stableButton < 3) stableButton++; }
        else { stableButton = 0; wasPressed = false; }
        if (stableButton == 3 && !wasPressed)
        {
            encoder.Reset();
            wasPressed = true;
            Console.WriteLine("reset");
        }
        int position = encoder.Position;
        if (position != last) { Console.WriteLine($"position:{position}"); last = position; }
        Thread.Sleep(10);
    }
}
finally { encoder.Dispose(); }
