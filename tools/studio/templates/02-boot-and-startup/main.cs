// /main.cs - your application, started at every boot (after /boot.cs and /jobs.cfg).
// It may run forever (a main loop) or start background jobs and return:
// jobs keep running and the shell / REPL stays usable over USB.
Console.WriteLine("main.cs: starting the application");

var led = new Pin(2, GPIO.Output);      // change 2 to your LED pin
Scheduler.Every(500, () => led.Toggle());                      // heartbeat
Scheduler.Every(10000, () => Console.WriteLine($"[{Environment.TickCount / 1000}s] alive, {GC.GetTotalMemory(false)} B used"));

Console.WriteLine($"main.cs: {Scheduler.Count} jobs running - type 'jobs' in the Shell console");
