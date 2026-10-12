// Analog joystick: X/Y on two ADC channels, button to GND
const int AX = 0, AY = 1, BTN = 5;
var button = new Pin(BTN, GPIO.InputPullUp);
int center = 1 << (ADC.Resolution - 1);

int Axis(int ch)
{
    int v = ADC.ReadAverage(ch, 8) - center;
    if (Math.Abs(v) < center / 10) return 0;               // dead zone
    return (int)Math.Clamp(v * 100L / center, -100, 100);   // -100..100
}

for (int i = 0; i < 40; i++)
{
    Console.WriteLine($"x:{Axis(AX)} y:{Axis(AY)} btn:{(button.Read() ? 0 : 1)}");
    Thread.Sleep(100);
}
