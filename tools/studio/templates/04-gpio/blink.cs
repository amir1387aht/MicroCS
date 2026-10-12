// Blink the on-board LED. Use a GPIO number (ESP32: 2, ESP32-S3 DevKit: 48 (RGB), Pico: 25)
// or a board pin name such as "LED", "PA5", "GPIO21", "GP15".
var led = new Pin(2, GPIO.Output);

for (int i = 0; i < 10; i++)
{
    led.Toggle();
    Console.WriteLine($"LED {(led.Value ? "on" : "off")}");
    Thread.Sleep(250);
}
led.Low();
