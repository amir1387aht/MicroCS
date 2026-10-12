// Feed the watchdog 10 times, then stop: the board resets about 2 s later.
Watchdog.Start(2000);
for (int i = 0; i < 10; i++)
{
    Watchdog.Feed();
    Console.WriteLine($"fed {i + 1}");
    Thread.Sleep(500);
}
Console.WriteLine("not feeding any more - reset in ~2 s");
Thread.Sleep(5000);
