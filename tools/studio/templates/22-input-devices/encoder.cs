// KY-040 rotary encoder: CLK (A), DT (B), SW. Counts detents with an interrupt on A.
const int A = 32, B = 33, SW = 25;
var pinA = new Pin(A, GPIO.InputPullUp);
var pinB = new Pin(B, GPIO.InputPullUp);
var sw = new Pin(SW, GPIO.InputPullUp);
int position = 0, last = 0;

pinA.OnChange(GPIO.Falling, (bool level) => { position += pinB.Read() ? 1 : -1; });
sw.OnChange(GPIO.Falling, (bool level) => { position = 0; Console.WriteLine("reset"); });

for (int i = 0; i < 100; i++)                  // 10 s
{
    if (position != last) { Console.WriteLine($"position:{position}"); last = position; }
    Thread.Sleep(100);                          // callbacks run during Sleep
}
