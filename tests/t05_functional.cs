using System;
using System.Collections.Generic;
using System.Linq;

Func<int, Func<int, int>> adder = x => y => x + y;
var add5 = adder(5);
Console.WriteLine(add5(10));

Func<int> MakeCounter()
{
    int count = 0;
    return () => ++count;
}
var c1 = MakeCounter(); var c2 = MakeCounter();
c1(); c1();
Console.WriteLine(c1() + " " + c2());

var actions = new List<Action>();
for (int i = 0; i < 3; i++) { int copy = i; actions.Add(() => Console.Write(copy + " ")); }
foreach (var a in actions) a();
Console.WriteLine();

int Fib(int n) => n < 2 ? n : Fib(n - 1) + Fib(n - 2);
Console.WriteLine(Fib(20));

long Fact(int n) { long r = 1; for (int i = 2; i <= n; i++) r *= i; return r; }
Console.WriteLine(Fact(12));

Func<double, double> sq = Math.Sqrt;
Console.WriteLine(sq(16));
Func<int, int, int> mul = (a, b) => a * b;
Console.WriteLine(mul(6, 7) + " " + mul.Invoke(2, 3));
Predicate<string> isEmpty = s => s.Length == 0;
Console.WriteLine(isEmpty("") + " " + isEmpty("x"));
Action<string> log = msg => Console.WriteLine("[log] " + msg);
log("hi");
Action noop = null;
noop?.Invoke();

event_demo();
void event_demo()
{
    Action<int> handlers = null;
    handlers += v => Console.WriteLine("h1 " + v);
    handlers += v => Console.WriteLine("h2 " + v * 2);
    handlers(21);
}

int Apply(Func<int, int> f, int times, int v) { for (int i = 0; i < times; i++) v = f(v); return v; }
Console.WriteLine(Apply(x => x * 2, 10, 1));

var memo = new Dictionary<int, long>();
long FibM(int n)
{
    if (n < 2) return n;
    if (memo.ContainsKey(n)) return memo[n];
    long r = FibM(n - 1) + FibM(n - 2);
    memo[n] = r;
    return r;
}
Console.WriteLine(FibM(40));


var btn = new Button();
Action<string> h = s => Console.WriteLine("clicked " + s);
btn.Click += h;
btn.Click += s => Console.WriteLine("second " + s);
btn.Press("A");
btn.Click -= h;
btn.Press("B");
Func<int, int> chain = x => x + 1;
chain += x => x * 100;
Console.WriteLine(chain.Invoke(1));

var sorted = new List<string> { "kiwi", "apple", "fig" };
sorted.Sort((a, b) => a.Length.CompareTo(b.Length));
Console.WriteLine(string.Join(",", sorted));
Comparison<int> desc = (a, b) => b - a;
var nums = new List<int> { 3, 1, 2 };
nums.Sort(desc);
Console.WriteLine(string.Join(",", nums));
static int Square(int x) => x * x;
Console.WriteLine(string.Join(",", nums.Select(Square)));

class Button
{
    public event Action<string> Click;
    public void Press(string id) { Click?.Invoke(id); }
}
