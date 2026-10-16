// Quick speed test - compare boards (and builds) with the same script.
// Collection workloads changed in 1.15; do not compare totals with older scripts.
// Fixed workloads also fit the smaller release heaps (RP2040 FreeRTOS: 128 KB,
// Pico W: 96 KB). Collection entries are tagged VM values, not native 4-byte ints;
// growing a collection needs room for both the old and new buffers in a fixed pool.
// Double math is slow on chips without a double FPU (RP2040, Cortex-M0/M4).
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
total += Time("string builder 2k", () => { var sb = new StringBuilder(); for (int i = 0; i < 2000; i++) sb.Append(i); });
total += Time("List<int> 1k", () => { var l = new List<int>(1000); for (int i = 0; i < 1000; i++) l.Add(1000 - i); l.Sort(); });
total += Time("Dictionary 500", () => { var d = new Dictionary<int, int>(); for (int i = 0; i < 500; i++) d[i] = i; for (int i = 0; i < 500; i++) _ = d[i]; });
Console.WriteLine($"{"total",-22} {total,6} ms  ({Hal.Board}, {Hal.CpuHz / 1_000_000} MHz)");
