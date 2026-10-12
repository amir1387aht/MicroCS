// Restart the board in 3 s (boot.cs, jobs.cfg and main.cs run again).
for (int i = 3; i > 0; i--) { Console.WriteLine($"reset in {i}..."); Thread.Sleep(1000); }
Hal.Reset();
