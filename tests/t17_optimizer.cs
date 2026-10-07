// Optimizer / superinstruction edge cases: every line must print the same
// from source (unoptimized) and from an optimized image (run_tests.sh runs both).
using System;
using System.Collections.Generic;

class Money {
    public int Cents;
    public Money(int c) { Cents = c; }
    public static Money operator +(Money a, int b) => new Money(a.Cents + b * 100);
    public static Money operator -(Money a, Money b) => new Money(a.Cents - b.Cents);
    public static bool operator <(Money a, int b) => a.Cents < b * 100;
    public static bool operator >(Money a, int b) => a.Cents > b * 100;
    public override string ToString() => "$" + (Cents / 100) + "." + (Cents % 100);
}

class Temp {
    double c;
    public List<string> Log = new List<string>();
    public double Celsius { get => c; set { if (value < -273.15) throw new ArgumentException("below absolute zero"); c = value; Log.Add("set " + value); } }
    public static int Count { get; set; }
    public void Set(double v) { this.Celsius = v; Count = Count + 1; }
}

class Animal { public virtual string Speak() => "..."; public string Name = "animal"; }
class Dog : Animal { public override string Speak() => "woof"; }
class Cat : Animal { public override string Speak() => "meow"; }
class Robot { public Func<string> Speak = () => "beep"; }

class Point {
    public int X, Y;
    public Point(int x, int y) { X = x; Y = y; }
}
class Point3 : Point {
    public int Z;
    public Point3(int x, int y, int z) : base(x, y) { Z = z; }
}

static int Arith(int n) {
    int a = 0, b = 1, c = 0;
    for (int i = 0; i < n; i++) {
        a += i % 7;            // LI_MOD + ACC_ADD
        b = b * 3 % 1000;      // LI_MUL / SI_MOD
        c -= i & 3;            // ACC_SUB
        c ^= i << 2;           // ACC (xor)
        if ((i & 1) == 0) a -= 1;
        if (i >= 5 && i <= 7) c += 100;
    }
    return a * 1000000 + b * 1000 + (c & 0xFFFF);
}

static double FloatLoop() {
    double x = 0.5, s = 0;
    for (double t = 0; t < 3; t += 0.5) { s += t * 2 + 1; x = x + 1; }   // float through int-immediate forms
    if (x > 3) s += 0.25;
    return s + x;
}

Console.WriteLine(Arith(50));
Console.WriteLine(FloatLoop());

// int overflow, shifts, negative division by immediates
int big = int.MaxValue;
big += 1;
Console.WriteLine(big);
int neg = -17;
Console.WriteLine($"{neg / 4} {neg % 4} {neg >> 2} {neg >>> 28} {neg << 3} {1 << 33}");
int zero = 0;
try { Console.WriteLine(neg / zero); } catch (DivideByZeroException) { Console.WriteLine("div by zero"); }
try { int q = 5; q %= zero; Console.WriteLine(q); } catch (DivideByZeroException) { Console.WriteLine("mod by zero"); }

// strings and chars through the fused forms
string s = "";
for (int i = 0; i < 5; i++) s += i;
s += "!";
Console.WriteLine(s);
char ch = 'a';
int code = ch + 1;
Console.WriteLine($"{code} {(char)(ch + 2)}");
string t = null;
if (t == null) Console.WriteLine("null ok");
string u = "x";
if (u != null && u.Length < 2) Console.WriteLine("short");

// operator overloads hit the slow paths of LI_*/JFLI_*/ACC
Money m = new Money(150);
m = m + 2;
Console.WriteLine(m);
var mm = m;
mm -= new Money(25);
Console.WriteLine(mm);
int loops = 0;
for (Money k = new Money(0); k < 3; k = k + 1) loops++;
Console.WriteLine(loops);
if (m > 3) Console.WriteLine("rich");

// property setters / static properties through SETF_L
var tmp = new Temp();
tmp.Set(21.5);
tmp.Set(-4);
try { tmp.Set(-300); } catch (ArgumentException e) { Console.WriteLine("caught: " + e.Message); }
Console.WriteLine($"{tmp.Celsius} {Temp.Count} {string.Join("|", tmp.Log)}");

// polymorphic call sites (method cache keyed by class), field delegates
var zoo = new List<object> { new Dog(), new Cat(), new Animal(), new Dog() };
string said = "";
foreach (var z in zoo) { var an = (Animal)z; said += an.Speak() + " "; }
Console.WriteLine(said.Trim());
var robo = new Robot();
Console.WriteLine(robo.Speak());
object[] things = { "abc", 42, 2.5, 'q', true };
foreach (var th in things) Console.Write(th.ToString() + ";");
Console.WriteLine();

// constructor cache with inheritance
int sum = 0;
for (int i = 0; i < 20; i++) {
    Point p = i % 2 == 0 ? new Point(i, 1) : new Point3(i, 2, 3);
    sum += p.X + p.Y;
    if (p is Point3 p3) sum += p3.Z;
}
Console.WriteLine(sum);

// captured locals keep their read order
int acc = 1;
Func<int> peek = () => acc;
for (int i = 0; i < 4; i++) acc += i % 3;
Console.WriteLine($"{acc} {peek()}");
int w = 10;
Action bump = () => w += 100;
for (int i = 0; i < 3; i++) { w -= i & 1; bump(); }
Console.WriteLine(w);

// branch-heavy code: nested loops, break/continue, early returns
static int Search(int[] a, int v) { for (int i = 0; i < a.Length; i++) if (a[i] == v) return i; return -1; }
int[] arr = { 5, 3, 9, 1, 7 };
Console.WriteLine($"{Search(arr, 9)} {Search(arr, 4)}");
int pairs = 0;
for (int i = 0; i < 6; i++) {
    if (i == 4) continue;
    for (int j = i; j < 6; j++) { if (j - i > 2) break; pairs += j; }
}
Console.WriteLine(pairs);
int wl = 0;
while (wl < 100) { wl += 7; if (wl % 5 == 0) break; }
Console.WriteLine(wl);
int dw = 3;
do { dw--; } while (dw > -2);
Console.WriteLine(dw);
