// /report.cs - started by jobs.cfg ("every 1m /report.cs restart=3")
var d = new DriveInfo("/");
string line = $"{Environment.TickCount / 1000}s heap={GC.GetTotalMemory(false)} free={d.AvailableFreeSpace / 1024}KB";
File.AppendAllText("/report.log", line + "\n");
Console.WriteLine(line);
