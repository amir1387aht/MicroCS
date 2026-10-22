// Method syntax is released after codegen; signatures/defaults stay available.
interface IProbe { int Read(); }
class Root {
    protected int value;
    public Root(int n) { value = n; }
    public virtual int Read() { return value; }
}
class Probe : Root, IProbe {
    public (int x, int y) Pair;
    public Probe(int n = 3) : base(n) { Pair = (4, 5); }
    public override int Read() { return base.Read() + Pair.x; }
    public int Read(int more) => Read() + more;
    public Func<int> Make(int n = 2) => () => { return Read(n); };
    public int TupleInput((int left, int right) p) { p = (7, 8); return p.left + p.right; }
    public (int x, int y) Named() { return (10, 11); }
    public string Braces() {
        // } ; => not a method boundary
        string s = @"}; {";
        Func<int> local = () => { return 6; };
        int Nested(int n) { return n + local(); }
        return $"{s}:{Nested(1)}";
    }
    public static Probe operator +(Probe a, Probe b) { return new Probe(a.Read() + b.Read()); }
}
var p = new Probe();
Console.WriteLine(p.Read());
Console.WriteLine(p.Read(2));
Console.WriteLine(p.Make()());
Console.WriteLine(p.TupleInput((1, 2)));
Console.WriteLine(p.Named().y);
Console.WriteLine(p.Braces());
Console.WriteLine((p + new Probe(1)).Read());
