// 02 · Button with an interrupt (no polling loop).
// The handler runs in script context (never inside the ISR), so it may allocate,
// print or call any API. A 1-argument handler receives the new level.
// Run: ./mcs --sim --run-for 1000 examples/hardware/02_button_interrupt.cs
const int ButtonPin = 2;
int presses = 0;

var button = new Pin(ButtonPin, GPIO.InputPullUp);
button.OnChange(GPIO.Falling, (bool level) =>
{
    presses++;
    Console.WriteLine($"button pressed ({presses})");
});

// On the simulator nobody presses the button, so fake two presses by
// driving the pin as an output (a real board would just wait here).
button.SetMode(GPIO.Output);
for (int i = 0; i < 2; i++) { button.High(); button.Low(); Thread.Sleep(50); }
Console.WriteLine($"total presses: {presses}");
