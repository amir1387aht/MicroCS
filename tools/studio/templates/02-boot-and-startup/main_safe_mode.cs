// /main.cs with a "safe mode": hold the BOOT button (GPIO 0, to GND) while the
// board starts and the application is skipped - handy when a script misbehaves.
var button = new Pin(0, GPIO.InputPullUp);
if (!button.Read())
{
    Console.WriteLine("safe mode: button held - application not started");
    return;
}

Console.WriteLine("starting the application");
var led = new Pin(2, GPIO.Output);
Scheduler.Every(1000, () => led.Toggle());
