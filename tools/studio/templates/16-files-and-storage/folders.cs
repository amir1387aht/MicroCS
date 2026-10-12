// Directories: create, list recursively, clean up.
Directory.CreateDirectory("/demo/sub");
File.WriteAllText("/demo/a.txt", "A");
File.WriteAllText("/demo/sub/b.txt", "B");

void Walk(string dir, string indent)
{
    foreach (var d in Directory.GetDirectories(dir)) { Console.WriteLine($"{indent}{Path.GetFileName(d)}/"); Walk(d, indent + "  "); }
    foreach (var f in Directory.GetFiles(dir)) Console.WriteLine($"{indent}{Path.GetFileName(f)} ({File.GetLength(f)} B)");
}
Walk("/demo", "");
Directory.Delete("/demo", true);                         // true = recursive
Console.WriteLine($"exists after delete: {Directory.Exists("/demo")}");
