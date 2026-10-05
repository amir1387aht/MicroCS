int Fib(int n) => n < 2 ? n : Fib(n - 1) + Fib(n - 2);
var sw = Stopwatch.StartNew();
Console.WriteLine(Fib(30));
Console.WriteLine($"fib(30): {sw.ElapsedMilliseconds} ms");
