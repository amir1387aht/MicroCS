// .NET semantics: MinValue / -1 overflows, and foreach notices modified collections.
// Output is byte-identical to .NET 8 (tools/verify_dotnet.sh).
static void Try(string name, Action a) {
    try { a(); Console.WriteLine(name + " ok"); }
    catch (Exception e) { Console.WriteLine(name + " " + e.GetType().Name + ": " + e.Message); }
}

// ---- integer division overflow (both / and % throw, like .NET)
int min = int.MinValue, m1 = -1;
Try("div", () => Console.WriteLine(min / m1));
Try("mod", () => Console.WriteLine(min % m1));
Try("div=", () => { int x = min; x /= m1; });
Try("DivRem", () => Console.WriteLine(Math.DivRem(min, m1)));
for (int d = -2; d <= 2; d++) {
    int dd = d;
    Try("loop " + d, () => Console.WriteLine((min / dd) + " " + (min % dd)));
}
Console.WriteLine(7 / -1 + " " + 7 % -1 + " " + (-7) / 2 + " " + (-7) % 2 + " " + (min + 1) / -1);

// ---- List<T>: any change during foreach throws on the next step
var l = new List<int> { 1, 2, 3 };
Try("Add", () => { foreach (var x in l) if (x == 2) l.Add(9); });
Try("Remove", () => { foreach (var x in l) if (x == 1) l.Remove(9); });
Try("RemoveAt", () => { foreach (var x in l) l.RemoveAt(0); });
Try("Insert", () => { foreach (var x in l) l.Insert(0, 0); });
Try("indexer", () => { foreach (var x in l) l[0] = x; });
Try("+=", () => { foreach (var x in l) l[1] += 1; });
Try("Sort", () => { foreach (var x in l) l.Sort(); });
Try("Reverse", () => { foreach (var x in l) l.Reverse(); });
Try("Clear", () => { foreach (var x in l) l.Clear(); });
l = new List<int> { 1, 2, 3 };
Try("add after last", () => { foreach (var x in l) if (x == 3) l.Add(4); });
Try("no-op Remove", () => { foreach (var x in l) l.Remove(100); });
Try("RemoveAll none", () => { foreach (var x in l) l.RemoveAll(v => v > 100); });
Try("RemoveRange 0", () => { foreach (var x in l) l.RemoveRange(0, 0); });
Try("break then Add", () => { foreach (var x in l) break; l.Add(5); });
Try("read only", () => { int s = 0; foreach (var x in l) s += l[0] + l.Count; Console.WriteLine(s); });
var copy = new List<int>(l);
Try("modify copy", () => { foreach (var x in copy) l.Add(x); Console.WriteLine(l.Count); });
int[] arr = { 1, 2, 3 };
Try("array write", () => { foreach (var x in arr) arr[0] = x; Console.WriteLine(arr[0]); });
int n = 0; foreach (var a in l) foreach (var b in l) n++;
Console.WriteLine("nested " + n);
var outer = new List<List<int>> { new List<int> { 1 }, new List<int> { 2 } };
foreach (var inner in outer) inner.Add(0);   // modifies the elements, not the list
Console.WriteLine("inner " + outer[0].Count + outer[1].Count);

// ---- Dictionary: adding a key throws; Remove, Clear and overwriting values are allowed
Dictionary<string, int> D() => new Dictionary<string, int> { ["a"] = 1, ["b"] = 2, ["c"] = 3, ["d"] = 4, ["e"] = 5 };
var dict = D();
foreach (var kv in dict) if (kv.Value % 2 == 0 || kv.Key == "c") dict.Remove(kv.Key);
Console.WriteLine("remove some: " + string.Join(",", dict.Keys));
dict = D(); var seen = new List<string>();
foreach (var kv in dict) { seen.Add(kv.Key); dict.Remove(kv.Key); }
Console.WriteLine("remove all: " + string.Join(",", seen) + " left " + dict.Count);
dict = D(); seen.Clear();
foreach (var kv in dict) { seen.Add(kv.Key); if (kv.Key == "b") dict.Remove("a"); dict[kv.Key] = 0; }
Console.WriteLine("remove earlier: " + string.Join(",", seen));
dict = D(); seen.Clear();
foreach (var kv in dict) { seen.Add(kv.Key); if (kv.Key == "b") { dict.Remove("c"); dict.Remove("d"); } }
Console.WriteLine("remove later: " + string.Join(",", seen));
Try("dict add", () => { foreach (var kv in dict) dict["z"] = 1; });
Try("dict Add", () => { foreach (var kv in dict) dict.Add(kv.Key + "!", 1); });
Try("dict Clear", () => { foreach (var kv in dict) dict.Clear(); });
Try("empty Clear", () => { foreach (var kv in dict) { } dict.Clear(); foreach (var kv in dict) dict.Clear(); });
dict = D();
Try("Keys copy", () => { foreach (var k in dict.Keys) dict.Remove(k); Console.WriteLine(dict.Count); });
