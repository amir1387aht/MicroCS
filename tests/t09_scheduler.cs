// Scheduler: jobs run from the host loop after the script returns.
// Jobs due in the same poll run in registration order, so the output is stable.
Scheduler.After(0, () => Console.WriteLine("one-shot"));
int ticks = 0;
int fast = Scheduler.Every(10, () => {
    ticks++;
    Console.WriteLine("tick " + ticks);
    if (ticks == 3) { Scheduler.Cancel(fast); Console.WriteLine("cancelled, active " + Scheduler.Count); }
});
int fails = 0;
Scheduler.Every(10, () => { fails++; throw new InvalidOperationException("bad job " + fails); }, 2);
Console.WriteLine("scheduled " + Scheduler.Count);
