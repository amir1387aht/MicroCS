// 11 · Hardware timers: a periodic tick and a one-shot timeout.
// Callbacks are queued from the timer ISR and run between script statements.
// Run: ./mcs --sim examples/hardware/11_hardware_timer.cs
int ticks = 0;
Timer.Start(0, 100_000, () => ticks++);                    // every 100 ms
Timer.Once(1, 350_000, () => Console.WriteLine($"one-shot fired after {ticks} ticks"));

Thread.Sleep(1050);
Timer.Stop(0);
Console.WriteLine($"periodic timer ticked {ticks} times in ~1 s");
