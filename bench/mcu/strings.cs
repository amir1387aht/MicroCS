// string building and parsing
using System.Text;
var sb = new StringBuilder();
for (int i = 0; i < 200; i++) { sb.Append(i); sb.Append(','); }
string s = sb.ToString();
int total = 0;
foreach (var part in s.Split(',')) if (part.Length > 0) total += int.Parse(part);
Console.WriteLine($"{s.Length} {total}");
