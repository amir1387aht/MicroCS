// Wait up to 5 s for a button press, otherwise time out.
var button = new Pin(0, GPIO.InputPullUp);
bool done = false;

int timeout = Scheduler.After(5000, () =>
{
    if (done) return;
    done = true;
    Console.WriteLine("timed out - no press");
});
button.OnChange(GPIO.Falling, (bool level) =>
{
    if (done) return;
    done = true;
    Scheduler.Cancel(timeout);
    Console.WriteLine("pressed in time!");
});
Console.WriteLine("press the button within 5 s");
