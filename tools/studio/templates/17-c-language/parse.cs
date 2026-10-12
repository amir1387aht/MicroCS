Console.WriteLine(int.Parse("123") + 1);
Console.WriteLine(double.TryParse("3.75", out double d) ? $"double {d}" : "bad");
Console.WriteLine(int.TryParse("12x", out _) ? "ok" : "12x is not a number");
Console.WriteLine($"hex FF = {Convert.ToInt32("FF", 16)}, 255 in binary = {Convert.ToString(255, 2)}");
Console.WriteLine($"char code of A = {(int)'A'}, code 66 = {(char)66}");
byte[] b = Encoding.UTF8.GetBytes("Hi!");
Console.WriteLine($"{BitConverter.ToString(b)} -> {Encoding.UTF8.GetString(b)}");
