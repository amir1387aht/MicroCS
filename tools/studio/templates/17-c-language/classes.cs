interface IShape { double Area(); string Name { get; } }

abstract class Shape : IShape
{
    public abstract double Area();
    public virtual string Name => "shape";
    public override string ToString() => $"{Name} with area {Area():F2}";
}
class Circle : Shape
{
    public double R { get; }
    public Circle(double r) { R = r; }
    public override double Area() => Math.PI * R * R;
    public override string Name => "circle";
}
class Rect : Shape
{
    public double W { get; set; }
    public double H { get; set; }
    public override double Area() => W * H;
    public override string Name => "rectangle";
}

var shapes = new List<IShape> { new Circle(1.5), new Rect { W = 2, H = 3 } };
foreach (var s in shapes) Console.WriteLine(s);
Console.WriteLine($"total area {shapes.Sum(s => s.Area()):F2}");
