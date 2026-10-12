// Append a line per boot to /boot.log; keep only the last 50 lines.
string line = $"boot at tick {Environment.TickCount}, heap {GC.GetTotalMemory(false)} B, board {Hal.Board}";
File.AppendAllText("/boot.log", line + "\n");

var lines = File.ReadAllLines("/boot.log");
if (lines.Length > 50)
    File.WriteAllLines("/boot.log", lines.Skip(lines.Length - 50).ToArray());
Console.WriteLine($"boot.log: {Math.Min(lines.Length, 50)} entries");
