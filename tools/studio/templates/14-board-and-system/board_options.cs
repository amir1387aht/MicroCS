// Options only one chip family has are set by name (see Hal.Options("SPI")).
foreach (var o in Hal.Options("SPI")) Console.WriteLine(o);
var fast = new SpiConfig(40_000_000, 0);
try { fast.Set("queue_size", 8); }
catch (ArgumentException e) { Console.WriteLine("no such option here: " + e.Message); }
Console.WriteLine(fast);
fast.Frequency = 26_000_000;                 // objects are plain values: change and open again
fast.Dma = null;                             // null = back to the board default
SPI.Open(0, fast);
Console.WriteLine($"Frequency = {fast.Get("Frequency")}");
fast.Reset();
Console.WriteLine(fast);
