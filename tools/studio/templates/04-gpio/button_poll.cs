// Poll a button between GPIO 0 and GND (BOOT button on most ESP32 boards).
var button = new Pin(0, GPIO.InputPullUp);
bool last = true;
int presses = 0;
Console.WriteLine("press the button (Stop ends the script)");
while (true)
{
    bool now = button.Read();
    if (last && !now) Console.WriteLine($"pressed {++presses}x");
    last = now;
    Thread.Sleep(10);
}
