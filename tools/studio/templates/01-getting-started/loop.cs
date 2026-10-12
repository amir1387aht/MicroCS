// The classic firmware shape: setup() once, then loop() forever.
// Press Stop (or Ctrl+C in the console) to end it.
var led = new Pin(2, GPIO.Output);      // 2 = your LED's GPIO (ESP32-S3 DevKit: 48, Pico: 25)
int count = 0;

void Setup()
{
    Console.WriteLine("setup done");
}

void Loop()
{
    led.Toggle();
    count++;
    if (count % 10 == 0) Console.WriteLine($"loop #{count}");
    Thread.Sleep(100);
}

Setup();
while (true) Loop();
