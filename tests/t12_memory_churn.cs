// args: --heap 131072
// Long-running churn under a small heap: temporary strings (weak intern table),
// dictionary add/remove (tombstones) and tuple keys must not grow memory.
int n = 0;
for (int t = 1; t <= 30000; t++) { string s = "tick " + t; n += s.Length; }
Console.WriteLine(n);
var d = new Dictionary<int, string>();
for (int i = 0; i < 20000; i++) { d[i] = "v" + i; d.Remove(i); }
Console.WriteLine(d.Count);
var grid = new Dictionary<(int, int), int>();
for (int i = 0; i < 5000; i++) { grid[(i % 7, i % 5)] = i; }
Console.WriteLine(grid.Count + " " + grid[(3, 4)]);
var sb = new StringBuilder();
for (int i = 0; i < 2000; i++) { sb.Clear(); sb.Append("line ").Append(i); }
Console.WriteLine(sb.ToString());
GC.Collect();
Console.WriteLine(GC.GetTotalMemory(false) < 100000);
