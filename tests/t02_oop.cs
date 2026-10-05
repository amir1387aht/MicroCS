using System;
using System.Collections.Generic;

namespace Demo
{
    enum Color { Red, Green = 5, Blue }

    interface IShape { double Area(); string Name { get; } }

    abstract class Shape : IShape
    {
        public static int Count;
        protected string name;
        public Shape(string name) { this.name = name; Count++; }
        public string Name => name;
        public abstract double Area();
        public virtual string Describe() => $"{Name} with area {Area():F2}";
        public override string ToString() => "Shape(" + name + ")";
    }

    class Circle : Shape
    {
        public double Radius { get; set; }
        public Circle(double r) : base("Circle") { Radius = r; }
        public override double Area() => Math.PI * Radius * Radius;
    }

    class Rect : Shape
    {
        public double W { get; }
        public double H { get; }
        public Rect(double w, double h) : base("Rect") { W = w; H = h; }
        public Rect(double side) : this(side, side) { name = "Square"; }
        public override double Area() => W * H;
        public override string Describe() => "[" + base.Describe() + "]";
    }

    struct Vec2
    {
        public int X, Y;
        public Vec2(int x, int y) { X = x; Y = y; }
        public static Vec2 operator +(Vec2 a, Vec2 b) => new Vec2(a.X + b.X, a.Y + b.Y);
        public static bool operator ==(Vec2 a, Vec2 b) => a.X == b.X && a.Y == b.Y;
        public static bool operator !=(Vec2 a, Vec2 b) => !(a == b);
        public int this[int i] => i == 0 ? X : Y;
        public override string ToString() => $"({X}, {Y})";
    }

    class Counter
    {
        private int value = 10;
        private readonly List<string> log = new List<string>();
        public int Value
        {
            get { return value; }
            set { if (value < 0) throw new ArgumentException("negative"); this.value = value; log.Add("set " + value); }
        }
        public int Changes => log.Count;
        public event Action OnChange;
        public static Counter Create() => new Counter();
    }

    static class Util
    {
        public const string Version = "1.0";
        public static int Twice(int x) => x * 2;
        public static int Sum(params int[] xs) { int s = 0; foreach (var x in xs) s += x; return s; }
        public static string Greet(string who = "world", string punct = "!") => "Hello, " + who + punct;
        public static T Pick<T>(T a, T b, bool first) => first ? a : b;
        public static int Over(int x) => 1;
        public static int Over(string s) => 2;
        public static int Over(int a, int b) => 3;
    }

    class Node<T>
    {
        public T Value;
        public Node<T> Next;
        public Node(T v, Node<T> next = null) { Value = v; Next = next; }
    }

    class Program
    {
        static void Main()
        {
            var shapes = new List<Shape> { new Circle(1), new Rect(2, 3), new Rect(4) };
            foreach (var s in shapes) Console.WriteLine(s.Describe());
            Console.WriteLine(Shape.Count);
            Console.WriteLine(shapes[0]);
            IShape sh = shapes[1];
            Console.WriteLine(sh.Name + " " + sh.Area());
            Console.WriteLine(shapes[2] is Rect);
            Console.WriteLine(shapes[0] is IShape);
            Console.WriteLine(shapes[0] is Rect);
            if (shapes[1] is Rect r && r.W > 1) Console.WriteLine("rect w=" + r.W);
            var c = shapes[0] as Circle;
            Console.WriteLine(c?.Radius);
            Console.WriteLine((shapes[1] as Circle)?.Radius ?? -1);

            var v = new Vec2(1, 2) + new Vec2(10, 20);
            Console.WriteLine(v);
            Console.WriteLine(v == new Vec2(11, 22));
            Console.WriteLine(v != new Vec2(11, 22));
            Console.WriteLine(v[1]);

            Color col = Color.Blue;
            Console.WriteLine((int)col);
            Console.WriteLine(col == Color.Blue);
            switch (col) { case Color.Red: Console.WriteLine("red"); break; case Color.Blue: Console.WriteLine("blue"); break; }

            var cnt = Counter.Create();
            cnt.Value = 5;
            cnt.Value += 3;
            Console.WriteLine(cnt.Value + " " + cnt.Changes);
            try { cnt.Value = -1; } catch (ArgumentException e) { Console.WriteLine("caught: " + e.Message); }

            Console.WriteLine(Util.Twice(21) + " " + Util.Version);
            Console.WriteLine(Util.Sum() + " " + Util.Sum(1) + " " + Util.Sum(1, 2, 3));
            Console.WriteLine(Util.Greet() + " " + Util.Greet("C#") + " " + Util.Greet("MCU", "?"));
            Console.WriteLine(Util.Pick("a", "b", false));
            Console.WriteLine(Util.Over(1) + "" + Util.Over("x") + Util.Over(1, 2));

            var list = new Node<int>(1, new Node<int>(2, new Node<int>(3)));
            int total = 0;
            for (var n = list; n != null; n = n.Next) total += n.Value;
            Console.WriteLine(total);

            var p = new Person { Name = "Ada", Age = 36 };
            Console.WriteLine(p);
            Console.WriteLine(p.GetType().Name);
            object o = 42;
            Console.WriteLine(o is int);
            Console.WriteLine(o.GetType().Name);
            string desc = o switch { int i when i > 40 => "big int", int i => "int", string str => "string", _ => "other" };
            Console.WriteLine(desc);
        }
    }

    class Person
    {
        public string Name { get; set; } = "nobody";
        public int Age { get; init; }
        public override string ToString() => $"{Name} ({Age})";
    }
}
