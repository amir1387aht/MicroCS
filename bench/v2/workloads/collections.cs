using System.Collections.Generic;
class Point {
    public int X, Y;
    public Point(int x, int y) { X = x; Y = y; }
    public int Len2() => X * X + Y * Y;
}
var points = new List<Point>();
for (int i = 0; i < 200; i++) points.Add(new Point(i % 10, i % 3));
int total = 0;
foreach (var point in points) total += point.Len2();
Console.WriteLine(total);
