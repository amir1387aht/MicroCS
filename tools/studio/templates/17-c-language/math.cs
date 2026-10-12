Console.WriteLine($"sqrt(2) = {Math.Sqrt(2):F6}, pi = {Math.PI:F6}");
Console.WriteLine($"sin(30 deg) = {Math.Sin(30 * Math.PI / 180):F3}, atan2 = {Math.Atan2(1, 1) * 180 / Math.PI} deg");
Console.WriteLine($"round {Math.Round(2.567, 2)}, floor {Math.Floor(-1.5)}, ceiling {Math.Ceiling(1.2)}");
Console.WriteLine($"clamp {Math.Clamp(150, 0, 100)}, abs {Math.Abs(-7)}, pow {Math.Pow(2, 10)}");

var rnd = new Random(42);                                // seeded = repeatable
Console.WriteLine($"dice: {rnd.Next(1, 7)} {rnd.Next(1, 7)} {rnd.Next(1, 7)}, double {rnd.NextDouble():F3}");
