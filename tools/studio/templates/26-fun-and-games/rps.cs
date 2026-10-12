// Rock, paper, scissors - type r, p or s ("q" quits)
var rnd = new Random();
string[] names = { "rock", "paper", "scissors" };
int me = 0, you = 0;
while (me < 3 && you < 3)
{
    Console.Write($"[you {you} : {me} board]  r/p/s? ");
    string line = Console.ReadLine();
    if (line == null || line.Trim() == "q") break;
    int p = "rps".IndexOf(line.Trim().ToLower().Length > 0 ? line.Trim().ToLower()[0] : '?');
    if (p < 0) { Console.WriteLine("type r, p or s"); continue; }
    int c = rnd.Next(3);
    Console.Write($"{names[p]} vs {names[c]}: ");
    if (p == c) Console.WriteLine("draw");
    else if ((p + 1) % 3 == c) { Console.WriteLine("board wins"); me++; }
    else { Console.WriteLine("you win"); you++; }
}
Console.WriteLine(you == 3 ? "\nyou won the match!" : me == 3 ? "\nthe board won the match" : "\nbye");
