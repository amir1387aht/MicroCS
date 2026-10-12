// A shared open-drain line (e.g. a 1-wire style or interrupt line) with an external pull-up.
var line = new Pin(4, new GpioConfig(GPIO.OpenDrain) { Pull = GPIO.PullNone, Initial = 1 });
line.Low(); Thread.Sleep(1); line.High();   // a 1 ms low pulse
Console.WriteLine($"line reads {line.Read()}");
// reconfigure the same pin as an input with a pull-down
line.Configure(new GpioConfig(GPIO.Input, GPIO.PullDown));
Console.WriteLine($"as input with pull-down: {line.Read()}");
