// Phase 3 library additions: Zip, Chunk, TryPop/TryPeek/TryDequeue,
// tuple-returning helpers. Output is checked against .NET 8.
using System;
using System.Collections.Generic;
using System.Linq;

var names = new List<string> { "a", "b", "c", "d" };
var nums = new int[] { 1, 2, 3 };
foreach (var p in names.Zip(nums))
    Console.WriteLine($"{p.First}={p.Second}");
foreach (var (n, v) in names.Zip(nums)) Console.Write(n + v + " ");
Console.WriteLine();
Console.WriteLine(string.Join(",", nums.Zip(names, (x, str) => str + x * 10)));
Console.WriteLine(names.Zip(nums).First());

int[] data = { 1, 2, 3, 4, 5, 6, 7 };
foreach (var chunk in data.Chunk(3))
    Console.WriteLine("[" + string.Join(" ", chunk) + "] len=" + chunk.Length);
Console.WriteLine(data.Chunk(10).Count());

var st = new Stack<int>();
st.Push(4); st.Push(9);
while (st.TryPop(out var top)) Console.Write(top + " ");
Console.WriteLine(st.TryPeek(out var none) + " " + st.Count);
st.Push(1);
Console.WriteLine(st.TryPeek(out var pk) + " " + pk + " " + st.Count);

var q = new Queue<string>();
q.Enqueue("x"); q.Enqueue("y");
Console.WriteLine(q.TryPeek(out var head) + " " + head);
while (q.TryDequeue(out var item)) Console.Write(item + ";");
Console.WriteLine(q.Count);

(int min, int max) MinMax(int[] xs) => (xs.Min(), xs.Max());
var mm = MinMax(data);
Console.WriteLine($"min={mm.min} max={mm.max} {mm}");
var pairs = data.Select(x => (x, x * x)).Where(t => t.Item2 > 10).ToList();
Console.WriteLine(string.Join(" ", pairs));
var byParity = data.Zip(data.Skip(1)).Select(t => t.Second - t.First).Sum();
Console.WriteLine(byParity);

// out var / tuple swaps inside unbraced loop bodies must not leak stack slots
int k = 0;
bool F(out int z) { z = k; return true; }
while (k < 3) { k++; if (k > 0) F(out var w); }
for (int j = 0; j < 100000; j++) if (F(out var q2)) F(out var r2);
int s = 0;
for (int j = 0; j < 5; j++) if (j % 2 == 0) (s, k) = (k, s + j);
Console.WriteLine($"{k} {s}");

// relational / and / or patterns in is-expressions, switch arms and case labels
double tc = 30.5;
Console.WriteLine(tc is < 18.0 or > 26.5);
int nn = 7;
Console.WriteLine(nn is >= 0 and < 10);
Console.WriteLine(nn is > 0 and < 5 or 7);
Console.WriteLine(nn is not > 5);
Console.WriteLine(nn is 1 or 2 or 3);
Console.WriteLine(nn is not (7));
object o = 12;
Console.WriteLine(o is int and > 10);
string sn = null;
Console.WriteLine(sn is null or "");
sn = "x";
Console.WriteLine(sn is null or "");
char ch = 'q';
Console.WriteLine(ch is >= 'a' and <= 'z' or >= 'A' and <= 'Z');
for (int ii = 0; ii < 6; ii++) Console.Write(ii switch { > 0 and < 3 => "a", 3 or 4 => "b", _ => "c" });
Console.WriteLine();
switch (nn) { case > 5 and < 8: Console.WriteLine("mid"); break; default: Console.WriteLine("other"); break; }
int[] arr = { 1, 5, 9 };
Console.WriteLine(arr.Length is > 2 and < 4 && arr[0] is 1);
if (o is int kk and > 3) Console.WriteLine("kk=" + kk);

// tuple names survive later assignments, out parameters and projection
(int a, int b) ta = (1, 2);
ta = (3, 4);
Console.WriteLine(ta.a + ta.b);
bool TryPoint(string str, out (int x, int y) p) { p = (str.Length, 2); return true; }
if (TryPoint("abc", out var tp)) Console.WriteLine($"{tp.x},{tp.y} {tp}");
var (pm, pn) = (7, 8);
var proj = (pm, pn, nums.Length, sum: pm + pn);
Console.WriteLine($"{proj.pm} {proj.pn} {proj.Length} {proj.sum} {proj}");
var dup = (pm, pm);
Console.WriteLine(dup.Item1 + dup.Item2);

// tuple-typed fields and properties keep their element names
var tma = new TupleHolder(); tma.Q = ("k", 3);
Console.WriteLine(tma.P.x + " " + tma.Q.n + " " + tma.R.hi);
var tmb = new TupleHolder { Q = ("z", 1) };
Console.WriteLine(tmb.Q.n + " " + tmb.M.second);
tmb.Set();
Console.WriteLine(tmb.Q.v + " " + tmb.Q);
class TupleHolder {
    public (int x, int y) P = (1, 2);
    public (string n, int v) Q { get; set; }
    public (int lo, int hi) R => (0, 9);
    public (int, int second) M = (4, 5);
    public void Set() { Q = ("set", 77); }
}
