using System.Collections.Generic;
class P { public int X, Y; public P(int x, int y) { X = x; Y = y; } public int Len2() => X * X + Y * Y; }
var sw = Stopwatch.StartNew();
int acc = 0;
for (int r = 0; r < 100; r++) {
    var list = new List<P>();
    for (int i = 0; i < 10000; i++) list.Add(new P(i % 100, r));
    foreach (var p in list) acc = (acc + p.Len2()) % 1000003;
}
Console.WriteLine(acc);
Console.WriteLine($"objects 1M: {sw.ElapsedMilliseconds} ms");
