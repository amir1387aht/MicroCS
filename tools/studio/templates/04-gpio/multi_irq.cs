// One handler for several inputs: the 2-argument callback gets the pin and its level.
int[] pins = { 4, 5, 6 };
foreach (int p in pins)
{
    GPIO.Mode(p, GPIO.InputPullUp);
    GPIO.OnChange(p, GPIO.Both, (int pin, bool level) => Console.WriteLine($"GPIO {pin} -> {(level ? "high" : "low")}"));
}
Console.WriteLine("watching GPIO 4, 5, 6 for 30 s");
Hal.Run(30000);                       // dispatch events for 30 s, then end
foreach (int p in pins) GPIO.Off(p);
