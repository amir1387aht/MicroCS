var readings = new[] { 21.5, 22.1, 35.0, 21.9, 22.4, -5.0, 22.0 };
var valid = readings.Where(r => r > 0 && r < 30).ToList();
Console.WriteLine($"valid {valid.Count}/{readings.Length}, avg {valid.Average():F2}, max {valid.Max()}");
Console.WriteLine("sorted: " + string.Join(" ", valid.OrderByDescending(r => r).Select(r => r.ToString("F1"))));

var words = new[] { "apple", "avocado", "banana", "blueberry", "cherry" };
foreach (var g in words.GroupBy(w => w[0]))
    Console.WriteLine($"{g.Key}: {string.Join(", ", g.Value)}");
Console.WriteLine($"any long word: {words.Any(w => w.Length > 8)}, first b: {words.First(w => w.StartsWith("b"))}");
