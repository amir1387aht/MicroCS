// Nested native LINQ calls consume C stack as well as VM frames.
int Depth(int n) => n == 0 ? 0 : new List<int> { n }.Select(x => Depth(x - 1)).OrderBy(x => x).First() + 1;
try { Console.WriteLine(Depth(100000)); }
catch (StackOverflowException) { Console.WriteLine("stack caught"); }
Console.WriteLine("stack survived");
