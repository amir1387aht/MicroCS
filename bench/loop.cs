var sw = Stopwatch.StartNew();
int sum = 0;
for (int i = 0; i < 10000000; i++) { sum += i % 7; }
Console.WriteLine(sum);
Console.WriteLine($"loop 10M: {sw.ElapsedMilliseconds} ms");
