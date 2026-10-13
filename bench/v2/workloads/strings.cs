using System.Text;
var text = new StringBuilder();
for (int i = 0; i < 200; i++) {
    text.Append(i);
    text.Append(',');
}
string values = text.ToString();
int total = 0;
foreach (var part in values.Split(',')) {
    if (part.Length > 0) total += int.Parse(part);
}
Console.WriteLine($"{values.Length} {total}");
