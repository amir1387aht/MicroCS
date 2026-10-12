// Jobs keep running after the script ends; the shell and REPL stay responsive.
var led = new Pin(2, GPIO.Output);
int blinkId = Scheduler.Every(500, () => led.Toggle());
Scheduler.Every(5000, () => Console.WriteLine($"alive, {GC.GetTotalMemory(false)} bytes in use"));
Scheduler.After(2000, () => Console.WriteLine("this runs once, 2 s after start"));
Console.WriteLine($"{Scheduler.Count} jobs started (blink id {blinkId}) - 'jobs' in the Shell lists them");
