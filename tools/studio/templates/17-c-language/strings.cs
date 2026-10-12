string s = "  MicroCS runs C# on microcontrollers  ";
Console.WriteLine($"[{s.Trim()}] upper: {s.Trim().ToUpper()}");
Console.WriteLine($"contains C#: {s.Contains("C#")}, index of runs: {s.IndexOf("runs")}");
Console.WriteLine(s.Trim().Replace("microcontrollers", "MCUs"));
Console.WriteLine(string.Join("-", s.Trim().Split(' ')));
Console.WriteLine($"[{"7".PadLeft(3, '0')}] [{s.Trim().Substring(0, 7)}] [{s.Trim()[^3..]}]");

var sb = new StringBuilder();
for (int i = 1; i <= 5; i++) sb.Append(i).Append(i < 5 ? "," : "");
sb.AppendLine().Append("done");
Console.WriteLine(sb.ToString());
