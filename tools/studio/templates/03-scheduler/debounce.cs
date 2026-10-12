// Handle a bouncy button: the interrupt only records the time, a 20 ms job
// later decides whether it was a real press.
var button = new Pin(0, GPIO.InputPullUp);     // button between GPIO 0 and GND
int lastEdge = 0, presses = 0;
bool pending = false;

button.OnChange(GPIO.Falling, (bool level) =>
{
    lastEdge = Environment.TickCount;
    if (pending) return;
    pending = true;
    Scheduler.After(20, () =>
    {
        pending = false;
        if (!button.Read()) Console.WriteLine($"pressed ({++presses})");
    });
});
Console.WriteLine("press the button");
