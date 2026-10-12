// The static GPIO API: no objects, just pin numbers.
const int Led = 2, Button = 0;
GPIO.Mode(Led, GPIO.Output);
GPIO.Mode(Button, GPIO.InputPullUp);

GPIO.Write(Led, 1);
Thread.Sleep(200);
GPIO.Toggle(Led);
Console.WriteLine($"LED is {GPIO.Read(Led)}, button is {(GPIO.Read(Button) ? "released" : "pressed")}");
