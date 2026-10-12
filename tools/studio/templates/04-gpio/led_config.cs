// Initial = 1: the output level is set before the pin starts driving.
// DriveMilliamps / Speed: where the chip has them (ESP32, RP2: 2/4/8/12 mA).
var cfg = new GpioConfig(GPIO.Output) { Initial = 1, DriveMilliamps = 12, Speed = GPIO.SpeedHigh };
Pin led;
try { led = new Pin("LED", cfg); }
catch (NotSupportedException e)              // e.g. STM32: no drive strength setting
{
    Console.WriteLine("board says: " + e.Message);
    led = new Pin("LED", new GpioConfig(GPIO.Output) { Initial = 1 });
}
Console.WriteLine($"LED {led.Number} starts at {led.Value}");
for (int i = 0; i < 6; i++) { led.Toggle(); Thread.Sleep(150); }
