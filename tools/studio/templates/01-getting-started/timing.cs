// Measure how long things take
var sw = Stopwatch.StartNew();
Thread.Sleep(250);
Console.WriteLine($"slept {sw.ElapsedMilliseconds} ms");

sw.Restart();
int sum = 0;
for (int i = 0; i < 100000; i++) sum += i & 7;
sw.Stop();
Console.WriteLine($"100k loop iterations: {sw.ElapsedMilliseconds} ms (sum {sum})");
Console.WriteLine($"uptime: {Environment.TickCount / 1000.0:F1} s, {Hal.Micros} us");
