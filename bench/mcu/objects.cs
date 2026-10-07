// classes, fields, method calls, List<T>
using System.Collections.Generic;
class P { public int X, Y; public P(int x, int y) { X = x; Y = y; } public int Len2() => X * X + Y * Y; }
int acc = 0;
for (int r = 0; r < 4; r++) {
    var list = new List<P>();
    for (int i = 0; i < 500; i++) list.Add(new P(i % 100, r));
    foreach (var p in list) acc = (acc + p.Len2()) % 1000003;
}
Console.WriteLine(acc);
