// Phase 2 language additions: case guards / patterns in switch statements, ref/out/in, TryParse, TryGetValue
using System;
using System.Collections.Generic;
using System.Linq;

object[] items = { 3, -2, "hi", "", 2.5, null, 100 };
foreach (var o in items) {
    switch (o) {
        case int n when n < 0:
            Console.WriteLine($"negative int {n}");
            break;
        case 100:
            Console.WriteLine("hundred");
            break;
        case int n:
            Console.WriteLine($"int {n}");
            break;
        case string s when s.Length == 0:
            Console.WriteLine("empty string");
            break;
        case string s:
            Console.WriteLine($"string {s}");
            break;
        case null:
            Console.WriteLine("null");
            break;
        default:
            Console.WriteLine($"other {o}");
            break;
    }
}
int k = 7;
switch (k) {
    case 1: case 2: Console.WriteLine("small"); break;
    case > 5 when k % 2 == 1: Console.WriteLine("big odd"); break;
    default: Console.WriteLine("?"); break;
}
static void Swap(ref int a, ref int b) { int t = a; a = b; b = t; }
static bool Divide(int x, int y, out int q, out int r) {
    if (y == 0) { q = 0; r = 0; return false; }
    q = x / y; r = x % y;
    return true;
}
static void Bump(ref int n) => n++;
int a = 1, b = 2;
Swap(ref a, ref b);
Console.WriteLine($"{a} {b}");
if (Divide(17, 5, out int q, out var r)) Console.WriteLine($"q={q} r={r}");
Console.WriteLine(Divide(1, 0, out _, out _));
Bump(ref a); Bump(ref a);
Console.WriteLine(a);
int.TryParse("123", out var n1);
Console.WriteLine(n1 + 1);
Console.WriteLine(int.TryParse("12x", out int bad) + " " + bad);
Console.WriteLine(double.TryParse("2.5", out double d) ? d * 2 : -1);
Console.WriteLine(bool.TryParse("True", out var bb) && bb);
var dict = new Dictionary<string, int> { ["one"] = 1 };
if (dict.TryGetValue("one", out var v)) Console.WriteLine($"one={v}");
Console.WriteLine(dict.TryGetValue("two", out var w));
int[] arr = { 5, 9 };
Swap(ref arr[0], ref arr[1]);
Console.WriteLine(string.Join(",", arr));
var p = new P();
Swap(ref p.X, ref p.Y);
Console.WriteLine($"{p.X} {p.Y}");
Console.WriteLine(Parse("42"));
static int Parse(string s) { if (!int.TryParse(s, out var x)) return -1; return x * 2; }
static int P2(string s) => int.TryParse(s, out var v) ? v : -1;
Func<string,int> f = s => int.TryParse(s, out var v) ? v * 10 : 0;
Console.WriteLine(P2("7") + " " + P2("x") + " " + f("3"));
var nums = new[] { "1", "a", "3" }.Select(s => int.TryParse(s, out var k) ? k : 0).Sum();
Console.WriteLine(nums);
class P { public int X = 3; public int Y = 4; }
