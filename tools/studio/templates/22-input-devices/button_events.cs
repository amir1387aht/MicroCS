// Button gestures from one input: click, double-click and long press
var btn = new Pin(0, GPIO.InputPullUp);         // BOOT button on most ESP32 boards
long downAt = 0, lastClick = -1000;
bool wasDown = false, pendingClick = false;

for (int i = 0; i < 500; i++)                   // 10 s
{
    bool down = !btn.Read();
    long now = Environment.TickCount;
    if (down && !wasDown) downAt = now;
    if (!down && wasDown)
    {
        long held = now - downAt;
        if (held > 800) Console.WriteLine("long press");
        else if (now - lastClick < 350) { Console.WriteLine("double click"); pendingClick = false; lastClick = -1000; }
        else { pendingClick = true; lastClick = now; }
    }
    if (pendingClick && now - lastClick >= 350) { Console.WriteLine("click"); pendingClick = false; }
    wasDown = down;
    Thread.Sleep(20);
}
