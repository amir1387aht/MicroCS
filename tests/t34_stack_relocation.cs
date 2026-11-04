// The value stack and the frame array grow (and move) on demand. Every slow path that can run
// script code in the middle of an instruction (operator overloads, ToString, GetEnumerator,
// properties, indexers, callbacks) is exercised here with a callee that recurses deeply, so the
// stack is reallocated while the outer instruction is suspended. The outer instruction must then
// keep using the *moved* stack: locals around the call must stay intact.
using System;
using System.Collections.Generic;

static int Deep(int n) { if (n == 0) return 0; int r = Deep(n - 1); return r + 1; }

class V {
    public int X;
    public V(int x) { X = x; }
    public static int Val(object o) { if (o is V) return ((V)o).X; return (int)o; }   // operand: V or int
    public static V operator +(V a, object b) => new V(a.X + Val(b) + Deep(60) - 60);
    public static V operator -(V a, object b) => new V(a.X - Val(b) + Deep(60) - 60);
    public static V operator *(V a, object b) => new V(a.X * Val(b) + Deep(60) - 60);
    public static V operator /(V a, object b) => new V(a.X / Val(b) + Deep(60) - 60);
    public static V operator %(V a, object b) => new V(a.X % Val(b) + Deep(60) - 60);
    public static V operator -(V a) => new V(-a.X + Deep(60) - 60);
    public static bool operator ==(V a, object b) => a.X == Val(b) + Deep(60) - 60;
    public static bool operator !=(V a, object b) => a.X != Val(b) + Deep(60) - 60;
    public static bool operator <(V a, object b) => a.X < Val(b) + Deep(60) - 60;
    public static bool operator >(V a, object b) => a.X > Val(b) + Deep(60) - 60;
    public static bool operator <=(V a, object b) => a.X <= Val(b) + Deep(60) - 60;
    public static bool operator >=(V a, object b) => a.X >= Val(b) + Deep(60) - 60;
    public override string ToString() => "V" + X + Deep(60) * 0;
    public int Prop { get { return X + Deep(60) - 60; } set { X = value + Deep(60) - 60; } }
    public int this[int i] { get { return X + i + Deep(60) - 60; } set { X = value + i + Deep(60) - 60; } }
    public int Field = 5;
}
class Bag {
    public List<int> Items = new List<int>();
    public IEnumerable<int> GetEnumerator() { Deep(60); return Items; }
}

static string Ops(int base0) {
    int p = base0, q = base0 + 1;                      // locals live across every slow path below
    var a = new V(10); var b = new V(3);
    var r1 = a + b; var r2 = a - b; var r3 = a * 2; var r4 = a / 2; var r5 = a % 4; var r6 = -a;
    var r7 = a + 5; var r8 = a - 5;
    a += b; a += 2; a -= 1;                            // accumulate forms
    var s = "";
    s += (a == b) + "," + (a != b) + "," + (a < 100) + "," + (a > 100) + "," + (a <= 100) + "," + (a >= 100) + "," + (a < b) + "," + (a > b);
    if (a < 100) s += "L"; if (a > 100) s += "G"; if (a <= 5) s += "LE"; if (a >= 5) s += "GE"; if (a == b) s += "E"; if (a != b) s += "N";
    int n = 0;
    while (a > 1000) { n++; }                          // backward jump forms with a slow compare
    for (var c = new V(0); c < 3; c += 1) n++;
    for (var c = new V(0); c < b; c += 1) n++;
    var x = new V(1);
    int ov = x.Prop; x.Prop = 4; ov += x.Prop; ov += x[2]; x[3] = 9; ov += x[1];
    int i3 = 3, i4 = 4; x[i3] = 8; ov += x[i4]; x[i4] = x[i3] + 1; ov += x.X;
    var t = "[" + x + "|" + r1 + "|" + $"{r2}:{p}:{q}" + "]";
    return p + "/" + q + "/" + r1.X + "," + r2.X + "," + r3.X + "," + r4.X + "," + r5.X + "," + r6.X + "," + r7.X + "," + r8.X + "/" + a.X + "/" + s + "/" + n + "/" + ov + "/" + t;
}

static string SetP(V v, int keep) { v.Prop = 4; v.Field = 7; v.Prop = keep; return v.X + "/" + v.Field + "/" + keep; }

static string Iter() {
    var bag = new Bag(); bag.Items.Add(1); bag.Items.Add(2); bag.Items.Add(3);
    int sum = 0; int k = 77;
    foreach (var i in bag) sum += i * k;
    return sum + "/" + k;
}

static string Callbacks() {
    var l = new List<int>();
    for (int i = 0; i < 40; i++) l.Add((i * 37) % 41);
    l.Sort((x, y) => { Deep(40); return x - y; });
    int local = 5;
    var m = l.FindAll(x => { Deep(40); return x % 2 == 0; });
    string str = string.Join(",", l.ConvertAll(x => { Deep(30); return x + local; }));
    return l[0] + "/" + l[39] + "/" + m.Count + "/" + local + "/" + str.Length;
}

class Node { public Node Next; public int V; }
static string Recurse() {
    Node head = null;
    for (int i = 0; i < 50; i++) { var nd = new Node(); nd.V = i; nd.Next = head; head = nd; }
    int total = 0;
    for (var cur = head; cur != null; cur = cur.Next) total += cur.V + Deep(5);
    var arr = new int[8]; for (int i = 0; i < 8; i++) arr[i] = Deep(i * 7);
    return total + "/" + arr[7];
}

Console.WriteLine(Ops(100));
Console.WriteLine(Ops(200));
Console.WriteLine(SetP(new V(1), 33));
Console.WriteLine(Iter());
Console.WriteLine(Callbacks());
Console.WriteLine(Recurse());
