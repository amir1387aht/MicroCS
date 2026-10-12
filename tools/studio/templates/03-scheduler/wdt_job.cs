// Feed the watchdog from a job: if the scheduler stops running (a script
// hangs in a tight loop), the board resets.
Watchdog.Start(5000);
Scheduler.Every(1000, () => Watchdog.Feed());
Console.WriteLine("watchdog armed, fed every second by a job");
