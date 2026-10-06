// A quick tour of the C# subset MicroCS supports. Pure language — runs the
// same on the host, a device, and real .NET.
//   ./mcs examples/tour.cs
using System;
using System.Collections.Generic;
using System.Linq;

// records-style data with classes, interfaces and virtual dispatch
interface IShape { double Area(); }
class Rect : IShape {
    public double W { get; } public double H { get; }
    public Rect(double w, double h) { W = w; H = h; }
    public virtual double Area() => W * H;
    public override string ToString() => $"Rect {W}x{H}";
}
class Square : Rect {
    public Square(double s) : base(s, s) { }
    public override string ToString() => $"Square {W}";
}

class Program {
    static (int q, int r) DivMod(int a, int b) => (a / b, a % b);

    static bool TryParsePoint(string s, out (int x, int y) p) {
        var parts = s.Split(',');
        if (parts.Length == 2 && int.TryParse(parts[0], out var x) && int.TryParse(parts[1], out var y)) {
            p = (x, y);
            return true;
        }
        p = (0, 0);
        return false;
    }

    static void Main() {
        // tuples and deconstruction
        var (q, r) = DivMod(17, 5);
        Console.WriteLine($"17 = 5*{q} + {r}");
        if (TryParsePoint("3,4", out var pt)) Console.WriteLine($"point {pt} -> x={pt.x}");

        // collections, LINQ and lambdas
        var shapes = new List<IShape> { new Rect(2, 3), new Square(4), new Rect(1, 10) };
        foreach (var s in shapes.OrderByDescending(s => s.Area()))
            Console.WriteLine($"{s,-10} area {s.Area(),5:F1}");
        var words = "the quick brown fox jumps over the lazy dog".Split(' ');
        var counts = new Dictionary<char, int>();
        foreach (var w in words) counts[w[0]] = counts.TryGetValue(w[0], out var n) ? n + 1 : 1;
        Console.WriteLine(string.Join(" ", counts.Where(kv => kv.Value > 1).Select(kv => $"{kv.Key}:{kv.Value}")));
        Console.WriteLine(string.Join("|", words.Zip(words.Skip(1), (a, b) => a[0] + "" + b[0])));

        // indices, ranges and patterns
        int[] data = { 5, 8, 13, 21, 34, 55 };
        Console.WriteLine($"last={data[^1]} middle=[{string.Join(",", data[1..^1])}]");
        foreach (var x in data)
            Console.Write(x switch { < 10 => "s", >= 10 and < 30 => "m", _ => "L" });
        Console.WriteLine();

        // closures and exceptions
        Func<int> Counter() { int c = 0; return () => ++c; }
        var next = Counter();
        next(); next();
        Console.WriteLine($"counter={next()}");
        try { Console.WriteLine(data[10]); }
        catch (IndexOutOfRangeException) when (data.Length < 10) { Console.WriteLine("caught out-of-range"); }
        finally { Console.WriteLine("done"); }
    }
}
