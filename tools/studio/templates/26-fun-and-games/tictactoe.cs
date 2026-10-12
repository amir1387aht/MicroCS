// Tic-tac-toe - you are X, type a cell 1..9 ("q" quits)
//  1 | 2 | 3
//  4 | 5 | 6
//  7 | 8 | 9
char[] b = "         ".ToCharArray();
int[][] lines = { new[] {0,1,2}, new[] {3,4,5}, new[] {6,7,8}, new[] {0,3,6}, new[] {1,4,7}, new[] {2,5,8}, new[] {0,4,8}, new[] {2,4,6} };
var rnd = new Random();

void Draw()
{
    for (int r = 0; r < 3; r++)
    {
        string Cell(int i) => b[i] == ' ' ? (i + 1).ToString() : b[i].ToString();
        Console.WriteLine($" {Cell(r * 3)} | {Cell(r * 3 + 1)} | {Cell(r * 3 + 2)}");
        if (r < 2) Console.WriteLine("---+---+---");
    }
}
char Winner()
{
    foreach (var l in lines)
        if (b[l[0]] != ' ' && b[l[0]] == b[l[1]] && b[l[1]] == b[l[2]]) return b[l[0]];
    return Array.IndexOf(b, ' ') < 0 ? 'D' : ' ';
}
// computer: win if possible, else block, else center, else random
int Pick()
{
    foreach (char who in new[] { 'O', 'X' })
        foreach (var l in lines)
        {
            int n = 0, free = -1;
            foreach (int i in l) { if (b[i] == who) n++; else if (b[i] == ' ') free = i; }
            if (n == 2 && free >= 0) return free;
        }
    if (b[4] == ' ') return 4;
    int k;
    do k = rnd.Next(9); while (b[k] != ' ');
    return k;
}

Draw();
while (Winner() == ' ')
{
    Console.Write("your move (1-9): ");
    string line = Console.ReadLine();
    if (line == null || line.Trim() == "q") { Console.WriteLine("\nbye"); return; }
    if (!int.TryParse(line.Trim(), out int m) || m < 1 || m > 9 || b[m - 1] != ' ') { Console.WriteLine("pick a free cell 1-9"); continue; }
    b[m - 1] = 'X';
    if (Winner() == ' ') { int c = Pick(); b[c] = 'O'; Console.WriteLine($"board plays {c + 1}"); }
    Draw();
}
char w = Winner();
Console.WriteLine(w == 'X' ? "you win!" : w == 'O' ? "the board wins" : "draw");
