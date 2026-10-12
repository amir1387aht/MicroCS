// Start a fast blink, slow it down after 3 s, and stop everything after 8 s.
var led = new Pin(2, GPIO.Output);
int fast = Scheduler.Every(100, () => led.Toggle());

Scheduler.After(3000, () =>
{
    Scheduler.Cancel(fast);
    Scheduler.Every(500, () => led.Toggle());
    Console.WriteLine("slow blink");
});
Scheduler.After(8000, () =>
{
    Scheduler.CancelAll();
    led.Low();
    Console.WriteLine("all jobs stopped");
});
