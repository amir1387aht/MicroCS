// Scheduler jobs run while the script waits in Thread.Sleep (cooperative main loop)
int ticks = 0;
int id = Scheduler.Every(100, () => ticks++);
var sw = Stopwatch.StartNew();
while (sw.ElapsedMilliseconds < 1050) Thread.Sleep(20);
Scheduler.Cancel(id);
Console.WriteLine(ticks >= 5 && ticks <= 11 ? "ticks ok" : $"ticks {ticks}");
// a job that sleeps does not start other jobs inside itself
int depth = 0, maxDepth = 0;
int a = Scheduler.Every(10, () => { depth++; maxDepth = Math.Max(maxDepth, depth); Thread.Sleep(30); depth--; });
Thread.Sleep(200);
Scheduler.Cancel(a);
Console.WriteLine($"max depth {maxDepth}");
