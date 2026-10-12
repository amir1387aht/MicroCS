(int min, int max, double avg) Stats(List<int> v) => (v.Min(), v.Max(), v.Average());

var (lo, hi, mean) = Stats(new List<int> { 4, 8, 15, 16, 23, 42 });
Console.WriteLine($"min {lo}, max {hi}, avg {mean:F2}");

int a = 1, b = 2;
(a, b) = (b, a);
Console.WriteLine($"swapped: a={a} b={b}");

var points = new List<(int x, int y)> { (1, 2), (3, 4) };
foreach (var (x, y) in points) Console.WriteLine($"({x}, {y})");
