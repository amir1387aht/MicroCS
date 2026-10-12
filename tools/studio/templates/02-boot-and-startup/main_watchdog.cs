// A main loop guarded by the hardware watchdog: if the loop hangs for more than
// 3 s the board resets and starts again. Feed it from the loop, never from a timer.
Watchdog.Start(3000);
Console.WriteLine("watchdog armed (3 s)");

int i = 0;
while (true)
{
    // ... the real work ...
    if (++i % 20 == 0) Console.WriteLine($"working, loop {i}");
    Watchdog.Feed();
    Thread.Sleep(100);
}
