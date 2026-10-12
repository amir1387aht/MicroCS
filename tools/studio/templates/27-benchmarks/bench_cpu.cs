// Quick speed test - compare boards (and builds) with the same script.
// Sizes fit the heap of small boards (RP2040: 160 KB); double math is slow on chips without a double FPU (RP2040, Cortex-M0/M4).
long Time(string name, Action body)
{
    GC.Collect();
    var sw = Stopwatch.StartNew();
    body();
    Console.WriteLine($"{name,-22} {sw.ElapsedMilliseconds,6} ms");
    return sw.ElapsedMilliseconds;
}

long total = 0;
total += Time("integer loop 100k", () => { int s = 0; for (int i = 0; i < 100_000; i++) s += i & 7; });
total += Time("double math 20k", () => { double s = 0; for (int i = 1; i < 20_000; i++) s += Math.Sqrt(i) * Math.Sin(i); });
total += Time("method calls 50k", () => { int F(int x) => x + 1; int s = 0; for (int i = 0; i < 50_000; i++) s = F(s); });
total += Time("string builder 5k", () => { var sb = new StringBuilder(); for (int i = 0; i < 5000; i++) sb.Append(i); });
total += Time("List<int> 4k", () => { var l = new List<int>(); for (int i = 0; i < 4_000; i++) l.Add(4_000 - i); l.Sort(); });
total += Time("Dictionary 2k", () => { var d = new Dictionary<int, int>(); for (int i = 0; i < 2000; i++) d[i] = i; for (int i = 0; i < 2000; i++) _ = d[i]; });
Console.WriteLine($"{"total",-22} {total,6} ms  ({Hal.Board}, {Hal.CpuHz / 1_000_000} MHz)");
