// GPIO speed from C#: static call vs. Pin object (measure the pin with a scope too)
const int P = 2, N = 20_000;
GPIO.Mode(P, GPIO.Output);
var pin = new Pin(P, GPIO.Output);

var sw = Stopwatch.StartNew();
for (int i = 0; i < N; i++) GPIO.Toggle(P);
double a = sw.ElapsedMilliseconds;
sw.Restart();
for (int i = 0; i < N; i++) pin.Toggle();
double b = sw.ElapsedMilliseconds;
Console.WriteLine($"GPIO.Toggle: {N / Math.Max(a, 1):F0} kHz toggles   Pin.Toggle: {N / Math.Max(b, 1):F0} kHz toggles");
