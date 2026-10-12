File.WriteAllText("/a.txt", "hello");
File.Copy("/a.txt", "/b.txt", true);                     // overwrite
File.Move("/b.txt", "/c.txt");
Console.WriteLine($"a:{File.Exists("/a.txt")} b:{File.Exists("/b.txt")} c:{File.Exists("/c.txt")}");
Console.WriteLine($"{Path.GetFileNameWithoutExtension("/c.txt")} has extension {Path.GetExtension("/c.txt")}");
Console.WriteLine(Path.Combine("/data", "logs", "today.csv"));
File.Delete("/a.txt"); File.Delete("/c.txt");
