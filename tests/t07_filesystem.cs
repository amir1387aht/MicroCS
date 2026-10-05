// args: --ramfs 8192
// File/Directory/Path over a RAM filesystem with an 8 KB quota
File.WriteAllText("/config.txt", "rate=10\nname=probe\n");
File.AppendAllText("/config.txt", "mode=fast");
foreach (var line in File.ReadAllLines("/config.txt")) {
    var kv = line.Split('=');
    Console.WriteLine($"{kv[0]} -> {kv[1]}");
}
Console.WriteLine(File.Exists("/config.txt") + " " + File.Exists("/missing") + " " + File.GetLength("/config.txt"));

Directory.CreateDirectory("/logs/2024");
File.WriteAllLines("/logs/2024/a.log", new List<string> { "one", "two" });
File.WriteAllBytes("/logs/raw.bin", new byte[] { 0, 127, 128, 255 });
byte[] raw = File.ReadAllBytes("/logs/raw.bin");
Console.WriteLine(raw.Length + " " + raw[3]);
foreach (var f in Directory.GetFiles("/logs")) Console.WriteLine("file " + f);
foreach (var d in Directory.GetDirectories("/logs")) Console.WriteLine("dir " + d);

File.Copy("/config.txt", "/copy.txt");
File.Move("/copy.txt", "/logs/moved.txt");
Console.WriteLine(File.Exists("/copy.txt") + " " + File.ReadAllText("/logs/moved.txt").Length);

try { File.ReadAllText("/nope.txt"); }
catch (FileNotFoundException e) { Console.WriteLine("FNF: " + e.Message); }
try { Directory.GetFiles("/nodir"); }
catch (DirectoryNotFoundException e) { Console.WriteLine("DNF: " + e.Message); }
try { Directory.Delete("/logs"); }
catch (IOException e) { Console.WriteLine("IO: " + (e is IOException)); }
try { File.Copy("/config.txt", "/logs/moved.txt"); }
catch (IOException e) { Console.WriteLine("exists: " + e.Message); }

// ".." cannot climb above the VFS root
File.WriteAllText("/../../../escape.txt", "x");
Console.WriteLine(File.Exists("/escape.txt"));

// quota: writing past 8 KB fails with IOException (the file stays, empty, as on POSIX)
var big = new StringBuilder();
for (int i = 0; i < 1000; i++) big.Append("0123456789");
try { File.WriteAllText("/big.txt", big.ToString()); Console.WriteLine("no limit?"); }
catch (IOException e) { Console.WriteLine("quota: " + e.Message); }

Directory.Delete("/logs", true);
Console.WriteLine(Directory.Exists("/logs") + " " + string.Join(",", Directory.GetFileSystemEntries("/")));
Console.WriteLine(Path.Combine("/scripts", "app.cs") + " " + Path.GetFileName("/a/b/c.mcsb") + " " +
                  Path.GetExtension("x.tar.gz") + " " + Path.GetDirectoryName("/a/b/c.cs") + " " + Path.GetFullPath("a/./b/../c"));
