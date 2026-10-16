// Cooperative VM delay: unlike hardware sleep, Studio Stop still works.
if (!Hal.Has("Power.Sleep")) return;
Console.WriteLine("Pausing for 250 ms");
Power.Sleep(250);
Console.WriteLine("Resumed without losing VM state");
