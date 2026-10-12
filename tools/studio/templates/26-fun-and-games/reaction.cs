// Reaction game: wait for the LED, then press the button
var led = new Pin(2, GPIO.Output);
var btn = new Pin(0, GPIO.InputPullUp);
var rnd = new Random();
var times = new List<long>();

for (int round = 1; round <= 3; round++)
{
    Console.WriteLine($"round {round}: get ready...");
    Thread.Sleep(1000 + rnd.Next(2000));
    if (!btn.Read()) { Console.WriteLine("too early!"); continue; }
    led.High();
    var sw = Stopwatch.StartNew();
    while (btn.Read() && sw.ElapsedMilliseconds < 3000) { }
    led.Low();
    if (sw.ElapsedMilliseconds >= 3000) { Console.WriteLine("too slow (3 s)"); continue; }
    times.Add(sw.ElapsedMilliseconds);
    Console.WriteLine($"{sw.ElapsedMilliseconds} ms");
}
if (times.Count > 0) Console.WriteLine($"best {times.Min()} ms, average {times.Average():F0} ms");
