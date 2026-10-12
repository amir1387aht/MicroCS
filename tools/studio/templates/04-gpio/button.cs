// Count presses of a button between GPIO 0 (BOOT on most ESP32 boards) and GND.
int presses = 0;
var button = new Pin(0, GPIO.InputPullUp);
button.OnChange(GPIO.Falling, (bool level) =>
{
    presses++;
    Console.WriteLine($"pressed {presses}x");
});
Console.WriteLine("Press the button... (Stop ends the script)");
while (true) Thread.Sleep(100);
