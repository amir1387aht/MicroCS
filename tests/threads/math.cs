// functions run on other threads by t_threads.cs
int Fib(int n) { return n < 2 ? n : Fib(n - 1) + Fib(n - 2); }
string Greet(string who, int times) { var s = ""; for (int i = 0; i < times; i++) s += who; return s; }
int[] Squares(int n) { var a = new int[n]; for (int i = 0; i < n; i++) a[i] = i * i; return a; }
void Boom() { throw new InvalidOperationException("boom in a thread"); }
int Spin() { int x = 0; while (true) { x++; } return x; }
int Tick() { var ch = new Channel("ticks"); ch.Send(Thread.Id > 0); return 0; }
