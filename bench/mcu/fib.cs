// recursive calls
int Fib(int n) => n < 2 ? n : Fib(n - 1) + Fib(n - 2);
Console.WriteLine(Fib(18));
