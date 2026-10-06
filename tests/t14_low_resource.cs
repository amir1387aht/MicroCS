// Regression tests for the 1.3 memory work: exceptions derived from built-in
// exception types (shared field layouts, base(...) into native constructors),
// many globals (slot stored in the interned name), interned-string churn,
// small tables growing from MCS_TABLE_MIN_CAP. Output is checked against .NET 8.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

static void Fail(int which) {
    switch (which) {
        case 0: throw new DeviceError("bus fault", 5);
        case 1: throw new SensorTimeout("i2c0");
        case 2: throw new ConfigError("rate");
        case 3: throw new Plain();
        default: throw new InvalidOperationException("other " + which);
    }
}

for (int i = 0; i < 5; i++) {
    try { Fail(i); }
    catch (SensorTimeout e) { Console.WriteLine($"timeout {e.Bus} code={e.Code} msg={e.Message}"); }
    catch (DeviceError e) { Console.WriteLine($"device code={e.Code} msg={e.Message}"); }
    catch (IOException e) { Console.WriteLine("io " + e.Message); }
    catch (ArgumentException e) when (e is ConfigError) { Console.WriteLine("config " + ((ConfigError)e).Key + " / " + e.Message); }
    catch (Exception e) { Console.WriteLine(e.GetType().Name + ": " + e.Message); }
}
Exception ex = new SensorTimeout("spi1");
Console.WriteLine(ex is IOException);
Console.WriteLine(ex is DeviceError d && d.Code == 110);
Console.WriteLine(new IOException("base still works").Message);
Console.WriteLine(new ArgumentException("arg").Message);

// many top-level locals become globals
int g0 = 0, g1 = 1, g2 = 2, g3 = 3, g4 = 4, g5 = 5, g6 = 6, g7 = 7, g8 = 8, g9 = 9;
int h0 = 10, h1 = 11, h2 = 12, h3 = 13, h4 = 14, h5 = 15, h6 = 16, h7 = 17, h8 = 18, h9 = 19;
int Sum() => g0 + g1 + g2 + g3 + g4 + g5 + g6 + g7 + g8 + g9 + h0 + h1 + h2 + h3 + h4 + h5 + h6 + h7 + h8 + h9;
Console.WriteLine(Sum());
g5 = 100; h9 = 1000;
Console.WriteLine(Sum());

// string churn: build, intern-compare and drop many distinct strings
var seen = new HashSet<string>();
int dup = 0;
for (int i = 0; i < 3000; i++) {
    string k = "key" + (i % 700);
    if (!seen.Add(k)) dup++;
}
Console.WriteLine($"{seen.Count} unique, {dup} duplicates");
var sb = new StringBuilder();
for (int i = 0; i < 200; i++) sb.Append((char)('a' + i % 26));
string big = sb.ToString();
Console.WriteLine(big.Length + " " + big.Substring(190) + " " + (big.Substring(0, 3) == "abc"));

// small dictionaries grow from the minimum capacity
var tables = new List<Dictionary<string, int>>();
for (int t = 0; t < 50; t++) {
    var dct = new Dictionary<string, int>();
    for (int j = 0; j <= t % 9; j++) dct["k" + j] = j * t;
    tables.Add(dct);
}
int total = 0;
foreach (var dct in tables) foreach (var kv in dct) total += kv.Value;
Console.WriteLine(total + " " + tables[17].Count + " " + tables[17]["k8"]);
var removed = new Dictionary<int, int>();
for (int i = 0; i < 100; i++) removed[i] = i;
for (int i = 0; i < 100; i += 2) removed.Remove(i);
for (int i = 0; i < 10; i++) removed[i * 1000] = i;
Console.WriteLine(removed.Count + " " + removed.ContainsKey(4) + " " + removed[9000]);

// compact dictionary index: crosses the 1-byte -> 2-byte slot width at 192 entries,
// removal keeps insertion order, deleted slots are reused
var big2 = new Dictionary<int, string>();
for (int i = 0; i < 1000; i++) big2[i * 7] = "v" + i;
for (int i = 0; i < 1000; i += 3) big2.Remove(i * 7);
for (int i = 0; i < 50; i++) big2[-i] = "n" + i;
long ks = 0; foreach (var kv in big2) ks += kv.Key;
Console.WriteLine(big2.Count + " " + ks + " " + big2[7] + " " + big2.ContainsKey(0) + " " + big2[-49]);
var firstKeys = new List<int>();
foreach (var k in big2.Keys) { firstKeys.Add(k); if (firstKeys.Count == 5) break; }
Console.WriteLine(string.Join(",", firstKeys));
var hs = new HashSet<int>();
for (int r = 0; r < 4; r++) { for (int i = 0; i < 300; i++) hs.Add(i); for (int i = 0; i < 300; i += 2) hs.Remove(i); }
Console.WriteLine(hs.Count + " " + hs.Contains(1) + " " + hs.Contains(2) + " " + string.Join(",", hs.Where(x => x < 12)));
var other = new HashSet<int>(new[] { 1, 3, 5, 1000 });
other.IntersectWith(hs);
Console.WriteLine(string.Join(",", other) + " " + hs.IsSupersetOf(other));
hs.Clear(); hs.Add(42);
Console.WriteLine(hs.Count + " " + hs.Contains(42));
var counts = new Dictionary<string, int>();
foreach (var w in "the quick brown fox jumps over the lazy dog the end".Split(' '))
    counts[w] = counts.TryGetValue(w, out var c) ? c + 1 : 1;
Console.WriteLine(string.Join(" ", counts.Select(kv => kv.Key + ":" + kv.Value)));

// every built-in collection works as a LINQ source and as a sequence argument
var stk = new Stack<int>(new[] { 1, 2, 3 });
var que = new Queue<int>(new HashSet<int> { 3, 1, 2 });
Console.WriteLine(string.Join(",", stk) + " " + string.Join(",", que) + " " + string.Concat(stk));
Console.WriteLine(stk.Where(x => x > 1).Sum() + " " + que.First() + " " + que.Skip(1).Count() + " " + stk.Max());
var merged = new List<int>(stk); merged.AddRange(que);
Console.WriteLine(string.Join(",", merged) + " " + merged.Take(3).SequenceEqual(stk) + " " + string.Join(";", que.Zip(stk)));
var scores = new Dictionary<string, int> { ["ann"] = 7, ["bob"] = 3, ["cy"] = 9 };
Console.WriteLine(string.Join(",", scores.Skip(1).Take(1).Select(kv => kv.Key)) + " " + scores.Average(kv => kv.Value) + " " + scores.Last().Key);
var sa = new HashSet<int> { 1, 2, 3 };
Console.WriteLine(sa.SetEquals(new[] { 3, 2, 1, 1 }) + " " + sa.IsSupersetOf(new List<int> { 1, 3 }) + " " + sa.IsSupersetOf(new[] { 4 }));
sa.SymmetricExceptWith(new[] { 3, 4 });
Console.WriteLine(string.Join(",", sa.OrderBy(x => x)));

class DeviceError : IOException {
    public int Code;
    public DeviceError(string msg, int code) : base(msg) { Code = code; }
}
class SensorTimeout : DeviceError {
    public string Bus;
    public SensorTimeout(string bus) : base("timeout on " + bus, 110) { Bus = bus; }
}
class ConfigError : ArgumentException {
    public string Key;
    public ConfigError(string key) : base("bad key " + key) { Key = key; }
}
class Plain : Exception {
    public Plain() : base("plain") { }
}
