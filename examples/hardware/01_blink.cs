// 01 · Blink — the "hello world" of hardware.
// Pin objects accept a number (13) or a board name ("LED", "PA5", "GPIO21").
// Run: ./mcs --sim --run-for 2000 examples/hardware/01_blink.cs
var led = new Pin("LED", GPIO.Output);

for (int i = 0; i < 6; i++)
{
    led.Toggle();
    Console.WriteLine($"LED is {(led.Value ? "on" : "off")}");
    Thread.Sleep(250);                  // sleeping still services interrupts and timers
}
led.Low();
