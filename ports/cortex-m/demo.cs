// MicroCS Cortex-M demo / smoke workload.
// Runs twice on the target: as a precompiled bytecode image and (full profile)
// compiled on the device from source. Output must match the host `mcs` run.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

int Fib(int n) => n < 2 ? n : Fib(n - 1) + Fib(n - 2);
Console.WriteLine($"fib(18) = {Fib(18)}");

// collections + LINQ-style helpers
var primes = new List<int>();
for (int i = 2; primes.Count < 25; i++) {
    bool p = true;
    for (int j = 2; j * j <= i; j++) if (i % j == 0) { p = false; break; }
    if (p) primes.Add(i);
}
Console.WriteLine($"primes: {primes.Count}, sum={primes.Sum()}, last={primes[primes.Count - 1]}");
var words = "the quick brown fox jumps over the lazy dog the end".Split(' ');
var freq = new Dictionary<string, int>();
foreach (var w in words) freq[w] = freq.TryGetValue(w, out var c) ? c + 1 : 1;
Console.WriteLine($"'the' x{freq["the"]}, distinct={freq.Count}");
Console.WriteLine(string.Join(",", words.Where(w => w.Length > 3).Select(w => w.ToUpper())));

// classes, interfaces, virtual dispatch
var shapes = new List<IShape> { new Rect(3, 4), new Circle(2), new Rect(1, 1) };
double total = 0;
foreach (var s in shapes) total += s.Area();
Console.WriteLine($"shapes={shapes.Count} area={total:F2}");

// strings
var sb = new StringBuilder();
for (int i = 0; i < 10; i++) sb.Append((char)('a' + i));
Console.WriteLine(sb.ToString() + " " + sb.Length);

// exceptions
try { int[] a = new int[2]; a[3] = 1; }
catch (IndexOutOfRangeException) { Console.WriteLine("caught index error"); }
finally { Console.WriteLine("finally ran"); }

// switch with patterns + ref/out
object o = 42;
switch (o) {
    case int n when n > 40: Console.WriteLine($"big int {n}"); break;
    default: Console.WriteLine("other"); break;
}
int x = 1, y = 2;
Swap(ref x, ref y);
Console.WriteLine($"swap: {x} {y}");

// float math
double r = Math.Sqrt(2.0);
Console.WriteLine($"sqrt2={r:F6} sin={Math.Sin(1.0):F4}");

// GC churn
long acc = 0;
for (int i = 0; i < 300; i++) { var tmp = new List<int> { i, i * 2 }; acc += tmp[1]; }
Console.WriteLine($"churn acc={acc}");

static void Swap(ref int a, ref int b) { int t = a; a = b; b = t; }

interface IShape { double Area(); }
class Rect : IShape {
    int w, h;
    public Rect(int w, int h) { this.w = w; this.h = h; }
    public double Area() => w * h;
}
class Circle : IShape {
    double rad;
    public Circle(double r) { rad = r; }
    public double Area() => Math.PI * rad * rad;
}
