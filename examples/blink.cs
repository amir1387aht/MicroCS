// Blink an LED and report button presses — the "hello world" of firmware.
// Run on the host:  ./mcs --sim --run-for 3000 examples/blink.cs
const int Led = 13, Button = 2;

GPIO.Mode(Led, GPIO.Output);
GPIO.Mode(Button, GPIO.InputPullUp);

int blinks = 0;
Scheduler.Every(250, () => {
    GPIO.Toggle(Led);
    blinks++;
});

bool last = true;
Scheduler.Every(20, () => {                 // 20 ms debounce poll
    bool now = GPIO.Read(Button);
    if (last && !now) Console.WriteLine($"button pressed at {Environment.TickCount} ms");
    last = now;
});

Scheduler.After(2000, () => Console.WriteLine($"{blinks} LED toggles in 2 s on board '{Hal.Board}'"));
