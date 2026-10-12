enum Mode { Off, Idle, Running = 10, Error }
[System.Flags] enum Opt { None = 0, Log = 1, Led = 2, Beep = 4 }
struct Point { public int X, Y; public Point(int x, int y) { X = x; Y = y; } public override string ToString() => $"({X},{Y})"; }

Mode m = Mode.Running;
Console.WriteLine($"mode {m} = {(int)m}, next {m + 1}");
Opt o = Opt.Log | Opt.Beep;
Console.WriteLine($"options {(int)o}, has beep {(o & Opt.Beep) != 0}, has led {(o & Opt.Led) != 0}");
var p = new Point(3, 4);
Console.WriteLine($"point {p}, distance {Math.Sqrt(p.X * p.X + p.Y * p.Y)}");
