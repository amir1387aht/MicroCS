// Write a CSV, read it back and compute per-column statistics.
var rows = new List<string> { "name,value" };
var rnd = new Random();
for (int i = 0; i < 10; i++) rows.Add($"s{i},{rnd.Next(100)}");
File.WriteAllLines("/data.csv", rows.ToArray());

var values = File.ReadAllLines("/data.csv").Skip(1).Select(l => int.Parse(l.Split(',')[1])).ToList();
Console.WriteLine($"{values.Count} rows, min {values.Min()}, max {values.Max()}, avg {values.Average():F1}");
