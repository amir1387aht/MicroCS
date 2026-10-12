// /boot.cs - the first script after power-up / reset (before /jobs.cfg and /main.cs).
// Keep it short and safe: set up pins and buses, print a banner, load settings.
// Globals do not survive into main.cs - each script is its own program.
Console.WriteLine($"MicroCS on {Hal.Board} - boot.cs");

GPIO.Mode(2, GPIO.Output);              // status LED (change 2 to your LED pin)
GPIO.Write(2, true);

if (Hal.Has("I2C")) I2C.Open(0, 400000);    // buses stay configured for later scripts

// count boots in a file
int boots = File.Exists("/boots.txt") ? int.Parse(File.ReadAllText("/boots.txt").Trim()) : 0;
boots++;
File.WriteAllText("/boots.txt", boots.ToString());
Console.WriteLine($"boot #{boots}, {GC.GetTotalMemory(false)} bytes in use");
