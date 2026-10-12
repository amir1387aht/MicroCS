// Write, read and list files on the device's flash
File.AppendAllText("/log.txt", $"boot at {Environment.TickCount} ms\n");
Console.WriteLine(File.ReadAllText("/log.txt"));

foreach (var f in Directory.GetFiles("/"))
    Console.WriteLine($"{f,-20} {File.GetLength(f),8} bytes");

var d = new DriveInfo("/");
Console.WriteLine($"{d.AvailableFreeSpace / 1024} KB free of {d.TotalSize / 1024} KB ({d.DriveFormat})");
