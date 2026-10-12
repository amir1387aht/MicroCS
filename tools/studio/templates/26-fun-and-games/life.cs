// Game of Life on a 16x16 torus - prints a few generations
const int N = 16;
bool[][] NewGrid() { var g = new bool[N][]; for (int i = 0; i < N; i++) g[i] = new bool[N]; return g; }
var grid = NewGrid();
var rnd = new Random(42);
for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) grid[y][x] = rnd.Next(4) == 0;

int Neighbours(int y, int x)
{
    int n = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if ((dy != 0 || dx != 0) && grid[(y + dy + N) % N][(x + dx + N) % N]) n++;
    return n;
}

for (int gen = 0; gen < 5; gen++)
{
    var sb = new StringBuilder($"generation {gen}\n");
    for (int y = 0; y < N; y++) { for (int x = 0; x < N; x++) sb.Append(grid[y][x] ? "#" : "."); sb.Append('\n'); }
    Console.WriteLine(sb.ToString());
    var next = NewGrid();
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) { int n = Neighbours(y, x); next[y][x] = n == 3 || (n == 2 && grid[y][x]); }
    grid = next;
}
