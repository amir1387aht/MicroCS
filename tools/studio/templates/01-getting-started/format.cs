// Console output and string formatting
int n = 42;
double v = 3.14159;
Console.Write("no newline, ");
Console.WriteLine("then a newline");
Console.WriteLine($"int {n}, hex 0x{n:X4}, padded [{n,6}] [{n,-6}]");
Console.WriteLine($"fixed {v:F2}, percent {0.256:P1}, thousands {1234567:N0}");
Console.WriteLine(string.Format("{0} + {1} = {2}", 2, 3, 2 + 3));
Console.WriteLine("a,b,c".Split(',').Length + " parts");
Console.WriteLine(string.Join(" | ", new[] { "left", "middle", "right" }));
