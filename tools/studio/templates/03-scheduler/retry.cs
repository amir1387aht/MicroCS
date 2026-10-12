// The 3rd argument of Scheduler.Every is the number of consecutive failures
// after which the job is stopped (default: the first exception stops it).
int tries = 0;
Scheduler.Every(1000, () =>
{
    tries++;
    Console.WriteLine($"attempt {tries}");
    if (tries % 2 == 1) throw new IOException("sensor did not answer");
    Console.WriteLine("  ok");
}, 3);
