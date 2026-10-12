// Hangman - type one letter per line, 6 misses allowed ("q" quits)
string[] words = { "microcontroller", "voltage", "resistor", "firmware", "interrupt", "oscilloscope", "breadboard", "sensor" };
string word = words[new Random().Next(words.Length)];
var guessed = new List<char>();
int misses = 0;

string Masked()
{
    var sb = new StringBuilder();
    foreach (char c in word) sb.Append(guessed.Contains(c) ? c : '_').Append(' ');
    return sb.ToString();
}

while (misses < 6 && Masked().Contains('_'))
{
    Console.WriteLine($"\n{Masked()}   misses: {misses}/6  {new string('X', misses)}");
    Console.Write("letter: ");
    string line = Console.ReadLine();
    if (line == null || line.Trim() == "q") break;
    line = line.Trim().ToLower();
    if (line.Length != 1 || line[0] < 'a' || line[0] > 'z') { Console.WriteLine("type one letter"); continue; }
    char g = line[0];
    if (guessed.Contains(g)) { Console.WriteLine("already tried"); continue; }
    guessed.Add(g);
    if (!word.Contains(g)) misses++;
}
Console.WriteLine(Masked().Contains('_') ? $"\nthe word was '{word}'" : $"\n{word} - you got it!");
