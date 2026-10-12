// Button between GPIO 9 and GND: internal pull-up, input hysteresis.
// Unset properties stay at the board default; unsupported ones throw with the reason.
var cfg = new GpioConfig(GPIO.Input, GPIO.PullUp) { Schmitt = true };
var button = new Pin(9, cfg);
Console.WriteLine(cfg);                       // GpioConfig { Pull = 2, Schmitt = true }
button.OnChange(GPIO.Both, level => Console.WriteLine(level ? "released" : "pressed"));
Console.WriteLine($"now {(button.Value ? "released" : "pressed")} - waiting 2 s for presses");
Hal.Run(2000);
