// HC-SR04: TRIG on GPIO 5, ECHO on GPIO 18 (use a divider on ECHO for 3.3 V chips).
const int Trig = 5, Echo = 18;
GPIO.Mode(Trig, GPIO.Output);
GPIO.Mode(Echo, GPIO.Input);

for (int i = 0; i < 10; i++)
{
    GPIO.Write(Trig, 1);
    Hal.DelayMicroseconds(10);
    GPIO.Write(Trig, 0);
    int us = GPIO.PulseIn(Echo, true, 30000);    // echo pulse length, 30 ms timeout
    if (us <= 0) Console.WriteLine("no echo");
    else Console.WriteLine($"{us / 58.0:F1} cm");
    Thread.Sleep(200);
}
